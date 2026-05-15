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

    def should_use_streaming(self, data_size: int) -> bool:
        return data_size > self._streaming_threshold_mb * 1024 * 1024

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            from gui.algorithms.transformer_compressor import TransformerCompressor, HAS_TORCH

            if not HAS_TORCH:
                raise RuntimeError("PyTorch not available for Transformer compression")
            tc = TransformerCompressor()
            result = tc.compress(data)
            cr = self._engine.PipelineCompressResult()
            cr.original_size = result.original_size
            cr.compressed_size = result.compressed_size
            cr.compression_ratio = result.compression_ratio
            cr.time_ms = result.time_ms
            cr.data = list(result.compressed_data)
            cr.success = result.success
            cr.error_message = result.error_message or ("OK" if result.success else "Compression failed")
            return cr

        return self.pipeline_compress(data, algorithm)

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            cr = self._engine.PipelineCompressResult()
            cr.success = False
            cr.error_message = "Transformer (beta) 暂不支持解压"
            return cr

        return self.pipeline_decompress(data, algorithm)

    def smart_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        return self.compress(data, algorithm)

    def smart_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
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

    _ALGO_TO_PIPELINE_ID = None

    def _get_pipeline_id(self, algorithm: AlgorithmType):
        if CompressionEngine._ALGO_TO_PIPELINE_ID is None:
            eng = self._engine
            CompressionEngine._ALGO_TO_PIPELINE_ID = {
                AlgorithmType.DEFLATE: eng.AlgorithmID.DEFLATE,
                AlgorithmType.GZIP: eng.AlgorithmID.DEFLATE,
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
            AlgorithmType.GZIP: self._engine.AlgorithmID.INFLATE,
            AlgorithmType.DPFLATE: self._engine.AlgorithmID.INFLATE,
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

    def pipeline_compress_file_viz(
        self,
        input_path: str,
        output_path: str,
        viz_path: str,
        algorithm: AlgorithmType = AlgorithmType.DPFLATE,
    ):
        """Streaming compress + visualization (.viz) output.

        Currently only DPFlate is supported for viz; Deflate/Brotli
        are wrapped in batch-mode adapters that don't expose observers.
        """
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

        result = self._engine.pipeline_compress_file_viz(
            input_path, output_path, viz_path, [algo_id],
        )
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
