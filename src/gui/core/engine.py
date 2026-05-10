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

    # AlgorithmType → C++ AlgorithmID
    _COMPRESS_MAP: dict[AlgorithmType, int] = None
    _DECOMPRESS_MAP: dict[AlgorithmType, int] = None

    def __init__(self):
        self._engine = _get_engine()
        if CompressionEngine._config is None:
            try:
                file_cfg = _load_app_config()
                CompressionEngine._config = _get_algo_from_file(file_cfg)
                CompressionEngine._streaming_threshold_mb = _get_threshold_from_file(file_cfg)
                logger.info("[engine] config loaded from file, threshold=%.1fMB",
                            CompressionEngine._streaming_threshold_mb)
            except Exception as e:
                logger.warning("[engine] failed to load config file: %s, using defaults", e)
                CompressionEngine._config = get_default_config()

        if self._engine is not None and CompressionEngine._COMPRESS_MAP is None:
            aid = self._engine.AlgorithmID
            CompressionEngine._COMPRESS_MAP = {
                AlgorithmType.DEFLATE: aid.Deflate,
                AlgorithmType.GZIP: aid.Deflate,  # Gzip uses deflate internally
            }
            CompressionEngine._DECOMPRESS_MAP = {
                AlgorithmType.DEFLATE: aid.Inflate,
            }
            # Optional algorithms (if available in pyd)
            for attr, algo, decomp in [
                ("LZSS", AlgorithmType.LZSS, "LZSSDecompress"),
                ("LZMine", AlgorithmType.LZMINE, "LZMineDecompress"),
                ("MyFlate", AlgorithmType.MYFLATE, None),
            ]:
                algo_id = getattr(aid, attr, None)
                if algo_id is not None:
                    CompressionEngine._COMPRESS_MAP[algo] = algo_id
                    logger.info("[engine] C++ algorithm available: %s", attr)
                if decomp is not None:
                    decomp_id = getattr(aid, decomp, None)
                    if decomp_id is not None:
                        CompressionEngine._DECOMPRESS_MAP[algo] = decomp_id

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
        """Legacy: individual compressor objects are not exposed in current
        pyd build.  The functional API (compress/decompress) is preferred.
        DP visualization for LZMine is not available until the pyd is rebuilt
        with LZMine class bindings."""
        logger.debug("[engine] _create_compressor: not available for %s", algorithm.value)
        return None

    def _algo_id(self, algorithm: AlgorithmType):
        return CompressionEngine._COMPRESS_MAP.get(algorithm)

    def _decomp_id(self, algorithm: AlgorithmType):
        return CompressionEngine._DECOMPRESS_MAP.get(algorithm)

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            from .transformer_compressor import TransformerCompressor, HAS_TORCH
            if not HAS_TORCH:
                raise RuntimeError("PyTorch not available for Transformer compression")
            tc = TransformerCompressor()
            result = tc.compress(data)
            cr = self._engine.CompressResult()
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

        algo_id = self._algo_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Unsupported algorithm: {algorithm.value}")

        return self._engine.compress(list(data), [algo_id])

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            cr = self._engine.CompressResult()
            cr.success = False
            cr.error_message = "Transformer (beta) 暂不支持解压"
            return cr

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        decomp_id = self._decomp_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Unsupported algorithm for decompression: {algorithm.value}")

        return self._engine.decompress(list(data), [decomp_id])

    def should_use_streaming(self, data_size: int) -> bool:
        return data_size > self._streaming_threshold_mb * 1024 * 1024

    def smart_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            return self.compress(data, algorithm)

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if self.should_use_streaming(len(data)):
            logger.info("[smart_compress] data size %d exceeds threshold %.1fMB",
                        len(data), self._streaming_threshold_mb)

        return self.compress(data, algorithm)

    def smart_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            return self.decompress(data, algorithm)

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if self.should_use_streaming(len(data)):
            logger.info("[smart_decompress] data size %d exceeds threshold %.1fMB",
                        len(data), self._streaming_threshold_mb)

        return self.decompress(data, algorithm)

    def smart_compress_file(self, input_path: str, output_path: str,
                             algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        algo_id = self._algo_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported")
        logger.info("[smart_compress_file] %s -> %s via %s", input_path, output_path, algorithm.value)
        return self._engine.compress_file(input_path, output_path, [algo_id])

    def smart_decompress_file(self, input_path: str, output_path: str,
                               algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        decomp_id = self._decomp_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported for decompression")
        logger.info("[smart_decompress_file] %s -> %s via %s", input_path, output_path, algorithm.value)
        return self._engine.decompress_file(input_path, output_path, [decomp_id])

    def pack_files(self, records: list[FileRecord]) -> bytes:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        webfiles = []
        for rec in records:
            wf = self._engine.WebFile()
            wf.name = rec.name
            wf.content = list(rec.raw_data)
            webfiles.append(wf)

        result = self._engine.pack_and_compress(webfiles, [self._engine.AlgorithmID.Deflate])
        return bytes(result)

    def unpack_archive(self, data: bytes) -> list[FileRecord]:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        webfiles = self._engine.decompress_and_unpack(list(data))
        records = []
        for wf in webfiles:
            rec = FileRecord(wf.name)
            rec.raw_data = bytes(wf.content)
            rec.size = len(wf.content)
            records.append(rec)
        return records

    def pipeline_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        return self.compress(data, algorithm)

    def pipeline_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        return self.decompress(data, algorithm)

    def pipeline_compress_file(self, input_path: str, output_path: str,
                                algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        return self.smart_compress_file(input_path, output_path, algorithm)

    def pipeline_decompress_file(self, input_path: str, output_path: str,
                                  algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        return self.smart_decompress_file(input_path, output_path, algorithm)
