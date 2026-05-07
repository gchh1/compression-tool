from __future__ import annotations

import logging
from pathlib import Path

from gui.core.models import (
    AlgorithmType,
    ALGORITHM_PARAMS,
    CompressionStatus,
    FileRecord,
    FolderRecord,
    get_default_config,
    STREAMING_THRESHOLD_MB,
)
from gui.core.app_config import (
    load_config as _load_app_config,
    save_config as _save_app_config,
    get_algo_config as _get_algo_from_file,
    get_streaming_threshold as _get_threshold_from_file,
)

logger = logging.getLogger(__name__)

_core_engine = None


def _get_engine():
    global _core_engine
    if _core_engine is not None:
        return _core_engine

    import sys

    _base = Path(__file__).resolve().parent.parent.parent.parent
    _candidates = [
        _base / "build" / "src" / "bindings" / "pybind",
        _base / "build" / "src" / "bindings",
        _base / "build_pybind" / "src" / "bindings" / "pybind",
        _base / "build_pybind" / "src" / "bindings",
        _base / "src",
    ]

    _meipass = getattr(sys, '_MEIPASS', None)
    if _meipass:
        _candidates.insert(0, Path(_meipass) / "core_engine")

    for _p in _candidates:
        if _p.is_dir():
            sys.path.insert(0, str(_p))

    try:
        import core_engine
        _core_engine = core_engine
        logger.info("C++ core_engine loaded from %s", core_engine.__file__)
        return _core_engine
    except ImportError:
        logger.warning("core_engine not available, using fallback")
        return None


class CompressionEngine:
    _instance = None
    _config: dict[AlgorithmType, dict[str, int]] = None
    _streaming_threshold_mb: float = STREAMING_THRESHOLD_MB

    def __init__(self):
        self._engine = _get_engine()
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
    def set_streaming_threshold(cls, mb: float, save: bool = True):
        cls._streaming_threshold_mb = mb
        if save:
            cls._save_to_file()

    @classmethod
    def get_streaming_threshold(cls) -> float:
        return cls._streaming_threshold_mb

    @classmethod
    def _save_to_file(cls):
        try:
            from gui.core.app_config import (
                load_config as _load_full,
                save_config as _save_full,
                STREAMING_CHUNK_SIZE_KB,
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

    @property
    def available(self) -> bool:
        return self._engine is not None

    def _create_compressor(self, algorithm: AlgorithmType):
        if algorithm == AlgorithmType.LZSS:
            comp = self._engine.LZSSCompressor()
        elif algorithm == AlgorithmType.LZMINE:
            comp = self._engine.LZMineCompressor()
        elif algorithm == AlgorithmType.DEFLATE:
            comp = self._engine.DeflateCompressor()
        elif algorithm == AlgorithmType.MYFLATE:
            comp = self._engine.MyFlateCompressor()
        elif algorithm == AlgorithmType.GZIP:
            comp = self._engine.GzipCompressor()
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
            from .transformer_compressor import TransformerCompressor, HAS_TORCH
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
            logger.info("[smart_compress] using streaming mode for %d bytes (threshold=%.1f MB)",
                        len(data), self._streaming_threshold_mb)
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
            logger.info("[smart_decompress] using streaming mode for %d bytes (threshold=%.1f MB)",
                        len(data), self._streaming_threshold_mb)
            try:
                return self.pipeline_decompress(data, algorithm)
            except Exception as e:
                logger.warning("[smart_decompress] streaming failed, fallback to normal: %s", e)

        return self.decompress(data, algorithm)

    def smart_compress_file(self, input_path: str, output_path: str,
                             algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        """Streaming compress file-to-file without loading into Python memory."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")
        logger.info("[smart_compress_file] %s -> %s via %s", input_path, output_path, algorithm.value)
        return self._engine.pipeline_compress_file(input_path, output_path, [algo_id])

    def smart_decompress_file(self, input_path: str, output_path: str,
                               algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        """Streaming decompress file-to-file without loading into Python memory."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")
        logger.info("[smart_decompress_file] %s -> %s via %s", input_path, output_path, algorithm.value)
        return self._engine.pipeline_decompress_file(input_path, output_path, [decomp_id])

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
            records.append(FileRecord(
                path=wf.filepath,
                name=Path(wf.filepath).name,
                extension=Path(wf.filepath).suffix.lower(),
                size=len(wf.context),
                raw_data=bytes(wf.context),
            ))
        return records

    _ALGO_TO_PIPELINE_ID = None

    def _get_pipeline_id(self, algorithm: AlgorithmType):
        if CompressionEngine._ALGO_TO_PIPELINE_ID is None:
            eng = self._engine
            CompressionEngine._ALGO_TO_PIPELINE_ID = {
                AlgorithmType.DEFLATE: eng.AlgorithmID.DEFLATE,
                AlgorithmType.LZSS: eng.AlgorithmID.LZSS,
                AlgorithmType.LZMINE: eng.AlgorithmID.LZMINE,
                AlgorithmType.MYFLATE: eng.AlgorithmID.MYFLATE,
            }
        return CompressionEngine._ALGO_TO_PIPELINE_ID.get(algorithm)

    def _get_decompress_pipeline_id(self, algorithm: AlgorithmType):
        mapping = {
            AlgorithmType.DEFLATE: self._engine.AlgorithmID.INFLATE,
            AlgorithmType.LZSS: self._engine.AlgorithmID.LZSS_DECOMPRESS,
            AlgorithmType.LZMINE: self._engine.AlgorithmID.LZMINE_DECOMPRESS,
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

    def pipeline_compress_file(self, input_path: str, output_path: str,
                                algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

        result = self._engine.pipeline_compress_file(input_path, output_path, [algo_id])
        return result

    def pipeline_decompress_file(self, input_path: str, output_path: str,
                                  algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")

        result = self._engine.pipeline_decompress_file(input_path, output_path, [decomp_id])
        return result
