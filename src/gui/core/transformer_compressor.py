from __future__ import annotations

import io
import json
import logging
import struct
import time
from dataclasses import dataclass

logger = logging.getLogger(__name__)

try:
    import torch
    import torch.nn as nn
    import torch.optim as optim
    HAS_TORCH = True
except ImportError:
    HAS_TORCH = False


@dataclass
class TransformerCompressResult:
    success: bool
    compressed_data: bytes
    original_size: int
    compressed_size: int
    compression_ratio: float
    time_ms: float
    error_message: str = ""
    model_info: str = ""


class ByteTokenizer:
    def __init__(self, chunk_size: int = 256):
        self.chunk_size = chunk_size

    def encode(self, data: bytes) -> torch.Tensor:
        chunks = []
        for i in range(0, len(data), self.chunk_size):
            chunk = data[i:i + self.chunk_size]
            if len(chunk) < self.chunk_size:
                chunk = chunk + b'\x00' * (self.chunk_size - len(chunk))
            chunks.append(list(chunk))
        if not chunks:
            chunks = [[0] * self.chunk_size]
        return torch.tensor(chunks, dtype=torch.float32)

    def decode(self, tensor: torch.Tensor) -> bytes:
        result = bytearray()
        for row in tensor:
            for val in row:
                b = max(0, min(255, round(float(val))))
                result.append(b)
        return bytes(result)


class TinyTransformerAutoEncoder(nn.Module):
    def __init__(self, input_dim: int = 256, latent_dim: int = 32,
                 n_heads: int = 4, n_layers: int = 2, dropout: float = 0.1):
        super().__init__()
        self.input_dim = input_dim
        self.latent_dim = latent_dim

        self.input_proj = nn.Linear(input_dim, latent_dim)
        encoder_layer = nn.TransformerEncoderLayer(
            d_model=latent_dim, nhead=n_heads, dim_feedforward=latent_dim * 4,
            dropout=dropout, batch_first=True)
        self.encoder = nn.TransformerEncoder(encoder_layer, num_layers=n_layers)

        decoder_layer = nn.TransformerDecoderLayer(
            d_model=latent_dim, nhead=n_heads, dim_feedforward=latent_dim * 4,
            dropout=dropout, batch_first=True)
        self.decoder = nn.TransformerDecoder(decoder_layer, num_layers=n_layers)
        self.output_proj = nn.Linear(latent_dim, input_dim)

    def encode(self, x: torch.Tensor) -> torch.Tensor:
        h = self.input_proj(x)
        return self.encoder(h)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        encoded = self.encode(x)
        decoded = self.decoder(encoded, encoded)
        return self.output_proj(decoded)


def _serialize_model(model: TinyTransformerAutoEncoder, original_name: str,
                     original_size: int, config: dict) -> bytes:
    buffer = io.BytesIO()
    magic = b'TFC01'
    buffer.write(magic)
    name_bytes = original_name.encode('utf-8')[:120]
    buffer.write(struct.pack('B', len(name_bytes)))
    buffer.write(name_bytes)
    buffer.write(struct.pack('<Q', original_size))
    config_json = json.dumps(config, ensure_ascii=False).encode('utf-8')
    buffer.write(struct.pack('<H', len(config_json)))
    buffer.write(config_json)
    state_dict = {k: v.cpu().numpy() for k, v in model.state_dict().items()}
    param_parts = []
    for key, arr in state_dict.items():
        key_bytes = key.encode('utf-8')
        part = io.BytesIO()
        part.write(struct.pack('<H', len(key_bytes)))
        part.write(key_bytes)
        shape = arr.shape
        part.write(struct.pack('<B', len(shape)))
        for s in shape:
            part.write(struct.pack('<I', s))
        part.write(arr.tobytes())
        param_parts.append(part.getvalue())
    param_data = b''.join(param_parts)
    buffer.write(struct.pack('<I', len(param_data)))
    buffer.write(param_data)
    return buffer.getvalue()


def _count_parameters(model: nn.Module) -> tuple[int, int]:
    total = sum(p.numel() for p in model.parameters())
    trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
    return total, trainable


class TransformerCompressor:
    DEFAULT_CONFIG = {
        'latent_dim': 32,
        'n_heads': 4,
        'n_layers': 2,
        'dropout': 0.1,
        'chunk_size': 256,
        'epochs': 50,
        'lr': 1e-3,
        'batch_size': 16,
    }

    def __init__(self, config: dict | None = None):
        if not HAS_TORCH:
            raise RuntimeError("PyTorch not installed. Install with: pip install torch")
        self.config = {**self.DEFAULT_CONFIG, **(config or {})}

    def compress(self, data: bytes, filename: str = "unknown") -> TransformerCompressResult:
        t0 = time.perf_counter()
        try:
            if len(data) == 0:
                return TransformerCompressResult(
                    success=False, compressed_data=b'', original_size=0,
                    compressed_size=0, compression_ratio=1.0, time_ms=0,
                    error_message="Empty input data")

            tokenizer = ByteTokenizer(chunk_size=self.config['chunk_size'])
            x = tokenizer.encode(data)
            n_chunks = x.shape[0]
            input_dim = x.shape[1]

            if n_chunks < 2:
                return TransformerCompressResult(
                    success=False, compressed_data=b'', original_size=len(data),
                    compressed_size=0, compression_ratio=1.0, time_ms=0,
                    error_message=f"Input too small ({len(data)} bytes), need >= {self.config['chunk_size'] * 2} bytes")

            model = TinyTransformerAutoEncoder(
                input_dim=input_dim,
                latent_dim=self.config['latent_dim'],
                n_heads=self.config['n_heads'],
                n_layers=self.config['n_layers'],
                dropout=self.config['dropout'])

            total_params, trainable_params = _count_parameters(model)
            logger.info("[Transformer] model: %d params (%d trainable), input=%d, latent=%d",
                        total_params, trainable_params, input_dim, self.config['latent_dim'])

            optimizer = optim.Adam(model.parameters(), lr=self.config['lr'])
            criterion = nn.MSELoss()

            dataset = torch.utils.data.TensorDataset(x)
            loader = torch.utils.data.DataLoader(dataset, batch_size=self.config['batch_size'], shuffle=True)

            device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
            model = model.to(device)
            x_device = x.to(device)

            model.train()
            for epoch in range(self.config['epochs']):
                total_loss = 0.0
                for batch_x, in loader:
                    batch_x = batch_x.to(device)
                    optimizer.zero_grad()
                    output = model(batch_x)
                    loss = criterion(output, batch_x)
                    loss.backward()
                    optimizer.step()
                    total_loss += loss.item()
                avg_loss = total_loss / len(loader)
                if (epoch + 1) % 10 == 0 or epoch == 0:
                    logger.info("[Transformer] epoch %d/%d loss=%.6f", epoch + 1, self.config['epochs'], avg_loss)

            model.eval()
            with torch.no_grad():
                reconstructed = model(x_device).cpu()
                mse = criterion(reconstructed, x).item()
                logger.info("[Transformer] final MSE=%.8f", mse)

            compressed = _serialize_model(model, filename, len(data), self.config)
            t1 = time.perf_counter()
            elapsed_ms = (t1 - t0) * 1000

            ratio = len(compressed) / len(data) if len(data) > 0 else 1.0
            model_info = f"params={total_params},chunks={n_chunks},epochs={self.config['epochs']},mse={mse:.6f}"

            logger.info("[Transformer] compressed %d -> %d bytes (%.3f%%) in %.0fms | %s",
                        len(data), len(compressed), ratio * 100, elapsed_ms, model_info)

            return TransformerCompressResult(
                success=True,
                compressed_data=compressed,
                original_size=len(data),
                compressed_size=len(compressed),
                compression_ratio=ratio,
                time_ms=elapsed_ms,
                model_info=model_info)

        except Exception as e:
            t1 = time.perf_counter()
            logger.error("[Transformer] compress error: %s", e, exc_info=True)
            return TransformerCompressResult(
                success=False, compressed_data=b'', original_size=len(data),
                compressed_size=0, compression_ratio=1.0, time_ms=(t1 - t0) * 1000,
                error_message=str(e))

    @staticmethod
    def is_available() -> bool:
        return HAS_TORCH
