from __future__ import annotations

import logging
from pathlib import Path

from gui.config.settings import (
    load_config as _load_app_config,
    save_config as _save_app_config,
    get_algo_config as _get_algo_from_file,
    get_streaming_threshold as _get_threshold_from_file,
    get_streaming_chunk_size as _get_chunk_kb_from_file,
)
from gui.engine.bridge import get_core_engine
from gui.models import (
    AlgorithmType,
    ALGORITHM_PARAMS,
    FileRecord,
    get_default_config,
    STREAMING_THRESHOLD_MB,
)

logger = logging.getLogger(__name__)


class CompressionEngine:
    _instance = None
    _config: dict[AlgorithmType, dict[str, int]] = None
    _streaming_threshold_mb: float = STREAMING_THRESHOLD_MB

    def __init__(self):
        self._engine = get_core_engine()
        if CompressionEngine._config is None:
            try:
                file_cfg = _load_app_config()
                CompressionEngine._config = _get_algo_from_file(file_cfg)
                CompressionEngine._streaming_threshold_mb = _get_threshold_from_file(file_cfg)
                logger.info("[engine] config loaded from file, threshold=%.1fMB", CompressionEngine._streaming_threshold_mb)
            except Exception as e:
                logger.warning("[engine] failed to load config file: %s, using defaults", e)
                CompressionEngine._config = get_default_config()

    @classmethod
    def set_config(cls, config: dict[AlgorithmType, dict[str, int]], save: bool = True):
        cls._config = config
        if save:
            cls._save_to_file()

    @classmethod
    def get_config(cls) -> dict[AlgorithmType, dict[str, int]]:
        if cls._config is None:
            cls._config = get_default_config()
        return cls._config

    @classmethod
    def snapshot_for_algorithm(cls, algorithm: AlgorithmType) -> dict[str, int]:
        """Copy of engine config for ``algorithm`` at call time (for per-record demos)."""
        cfg = cls.get_config().get(algorithm, {})
        return {str(k): int(v) for k, v in cfg.items()}

    def create_compressor_for_visualization(
        self,
        algorithm: AlgorithmType,
        params: dict[str, int] | None = None,
    ):
        """Build compressor with global config, then overlay ``params`` if provided."""
        compressor = self._create_compressor(algorithm)
        if params:
            for key, val in params.items():
                setter = getattr(compressor, f"set_{key}", None)
                if setter:
                    setter(int(val))
        return compressor

    @classmethod
    def set_streaming_threshold(cls, mb: float, save: bool = True):
        cls._streaming_threshold_mb = mb
        if save:
            cls._save_to_file()

    @classmethod
    def get_streaming_threshold(cls) -> float:
        return cls._streaming_threshold_mb

    @property
    def available(self) -> bool:
        return self._engine is not None

    @classmethod
    def _save_to_file(cls):
        try:
            from gui.config.settings import (
                load_config as _load_full,
                save_config as _save_full,
            )

            full = _load_full()
            algo_dict = {}
            for algo, params in cls._config.items():
                algo_dict[algo.value] = {k: v for k, v in params.items()}
            full["algorithms"] = algo_dict
            if "streaming" not in full:
                full["streaming"] = {}
            full["streaming"]["threshold_mb"] = cls._streaming_threshold_mb
            _save_full(full)
            logger.info("[engine] config saved to file")
        except Exception as e:
            logger.error("[engine] failed to save config: %s", e)

    @classmethod
    def reset_to_defaults(cls):
        cls._config = get_default_config()
        cls._streaming_threshold_mb = STREAMING_THRESHOLD_MB
        cls._save_to_file()

    def _create_compressor(self, algorithm: AlgorithmType):
        if algorithm == AlgorithmType.LZSS:
            comp = self._engine.LZSSCompressor()
        elif algorithm == AlgorithmType.LZDP:
            comp = self._engine.LZDPCompressor()
        elif algorithm == AlgorithmType.DEFLATE:
            comp = self._engine.DeflateCompressor()
        elif algorithm == AlgorithmType.DPFLATE:
            comp = self._engine.DPFlateCompressor()
        elif algorithm == AlgorithmType.GZIP:
            comp = self._engine.GzipCompressor()
        elif algorithm == AlgorithmType.BROTLI:
            comp = self._engine.BrotliCompressor()
        elif algorithm == AlgorithmType.ZSTD:
            comp = self._engine.ZstdCompressor()
        else:
            raise ValueError(f"Unsupported algorithm: {algorithm.value}")

        cfg = self._config.get(algorithm, {})
        for key, val in cfg.items():
            setter = getattr(comp, f"set_{key}", None)
            if setter:
                setter(val)
        return comp

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            from gui.algorithms.transformer_compressor import TransformerCompressor, HAS_TORCH

            if not HAS_TORCH:
                raise RuntimeError("PyTorch not available for Transformer compression")
            tc = TransformerCompressor()
            result = tc.compress(data)
            cr = self._engine.CompressorResult()
            cr.original_size = result.original_size
            cr.compressed_size = result.compressed_size
            cr.compression_ratio = result.compression_ratio
            cr.time_ms = result.time_ms
            cr.data = list(result.compressed_data)
            cr.success = result.success
            cr.error_message = result.error_message or ("OK" if result.success else "Compression failed")
            return cr

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        compressor = self._create_compressor(algorithm)
        result = compressor.compress(list(data))

        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.compression_ratio = result.compression_ratio
        cr.time_ms = result.time_ms
        cr.data = list(result.data)
        cr.success = result.success
        cr.error_message = result.error_message
        return cr

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            cr = self._engine.CompressorResult()
            cr.success = False
            cr.error_message = "Transformer (beta) 暂不支持解压"
            return cr

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        compressor = self._create_compressor(algorithm)
        result = compressor.decompress(list(data))

        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.time_ms = result.time_ms
        cr.data = list(result.data)
        cr.success = result.success
        cr.error_message = result.error_message
        return cr

    def should_use_streaming(self, data_size: int) -> bool:
        return data_size > self._streaming_threshold_mb * 1024 * 1024

    def smart_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            return self.compress(data, algorithm)

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if self.should_use_streaming(len(data)):
            logger.info(
                "[smart_compress] using streaming mode for %d bytes (threshold=%.1f MB)",
                len(data),
                self._streaming_threshold_mb,
            )
            try:
                return self.pipeline_compress(data, algorithm)
            except Exception as e:
                logger.warning("[smart_compress] streaming failed, fallback to normal: %s", e)

        return self.compress(data, algorithm)

    def smart_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            return self.decompress(data, algorithm)

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if self.should_use_streaming(len(data)):
            logger.info(
                "[smart_decompress] using streaming mode for %d bytes (threshold=%.1f MB)",
                len(data),
                self._streaming_threshold_mb,
            )
            try:
                return self.pipeline_decompress(data, algorithm)
            except Exception as e:
                logger.warning("[smart_decompress] streaming failed, fallback to normal: %s", e)

        return self.decompress(data, algorithm)

    def smart_compress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
    ):
        """Streaming compress file-to-file without loading into Python memory."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")
        chunk_bytes = int(_get_chunk_kb_from_file(_load_app_config())) * 1024
        logger.info(
            "[smart_compress_file] %s -> %s via %s (chunk_bytes=%d)",
            input_path,
            output_path,
            algorithm.value,
            chunk_bytes,
        )
        return self._engine.pipeline_compress_file(
            input_path, output_path, [algo_id], chunk_bytes
        )

    def smart_decompress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
    ):
        """Streaming decompress file-to-file without loading into Python memory."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")
        chunk_bytes = int(_get_chunk_kb_from_file(_load_app_config())) * 1024
        logger.info(
            "[smart_decompress_file] %s -> %s via %s (chunk_bytes=%d)",
            input_path,
            output_path,
            algorithm.value,
            chunk_bytes,
        )
        return self._engine.pipeline_decompress_file(
            input_path, output_path, [decomp_id], chunk_bytes
        )

    def pack_files(self, records: list[FileRecord]) -> bytes:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        files = []
        for rec in records:
            f = self._engine.File()
            f.filepath = rec.path
            f.context = list(rec.raw_data)
            files.append(f)

        packed = self._engine.Archiver.pack(files)
        return bytes(packed)

    def unpack_archive(self, data: bytes) -> list[FileRecord]:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        raw_files = self._engine.Archiver.unpack(list(data))
        records = []
        for wf in raw_files:
            records.append(
                FileRecord(
                    path=wf.filepath,
                    name=Path(wf.filepath).name,
                    extension=Path(wf.filepath).suffix.lower(),
                    size=len(wf.context),
                    raw_data=bytes(wf.context),
                )
            )
        return records

    _ALGO_TO_PIPELINE_ID = None

    def _get_pipeline_id(self, algorithm: AlgorithmType):
        if CompressionEngine._ALGO_TO_PIPELINE_ID is None:
            eng = self._engine
            CompressionEngine._ALGO_TO_PIPELINE_ID = {
                AlgorithmType.DEFLATE: eng.AlgorithmID.DEFLATE,
                AlgorithmType.LZSS: eng.AlgorithmID.LZSS,
                AlgorithmType.LZDP: eng.AlgorithmID.LZDP,
                AlgorithmType.DPFLATE: eng.AlgorithmID.DPFLATE,
                AlgorithmType.BROTLI: eng.AlgorithmID.BROTLI,
                AlgorithmType.ZSTD: eng.AlgorithmID.ZSTD,
            }
        return CompressionEngine._ALGO_TO_PIPELINE_ID.get(algorithm)

    def _get_decompress_pipeline_id(self, algorithm: AlgorithmType):
        mapping = {
            AlgorithmType.DEFLATE: self._engine.AlgorithmID.INFLATE,
            AlgorithmType.LZSS: self._engine.AlgorithmID.LZSS_DECOMPRESS,
            AlgorithmType.LZDP: self._engine.AlgorithmID.LZMINE_DECOMPRESS,
            AlgorithmType.BROTLI: self._engine.AlgorithmID.BROTLI_DECOMPRESS,
            AlgorithmType.ZSTD: self._engine.AlgorithmID.ZSTD_DECOMPRESS,
        }
        return mapping.get(algorithm)

    def pipeline_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

        result = self._engine.pipeline_compress(list(data), [algo_id])
        return result

    def pipeline_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")

        result = self._engine.pipeline_decompress(list(data), [decomp_id])
        return result

    def pipeline_compress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
    ):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

        result = self._engine.pipeline_compress_file(input_path, output_path, [algo_id])
        return result

    def pipeline_decompress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
    ):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")

        result = self._engine.pipeline_decompress_file(input_path, output_path, [decomp_id])
        return result
