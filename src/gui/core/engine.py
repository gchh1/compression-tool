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
)

logger = logging.getLogger(__name__)

_core_engine = None


def _get_engine():
    global _core_engine
    if _core_engine is not None:
        return _core_engine
    try:
        import core_engine
        _core_engine = core_engine
        logger.info("C++ core_engine loaded")
        return _core_engine
    except ImportError:
        logger.warning("core_engine not available, using fallback")
        return None


class CompressionEngine:
    _instance = None
    _config: dict[AlgorithmType, dict[str, int]] = None

    def __init__(self):
        self._engine = _get_engine()
        if CompressionEngine._config is None:
            CompressionEngine._config = get_default_config()

    @classmethod
    def set_config(cls, config: dict[AlgorithmType, dict[str, int]]):
        cls._config = config

    @classmethod
    def get_config(cls) -> dict[AlgorithmType, dict[str, int]]:
        if cls._config is None:
            cls._config = get_default_config()
        return cls._config

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
        elif algorithm == AlgorithmType.LZCRAZY:
            comp = self._engine.LZCrazyCompressor()
        elif algorithm == AlgorithmType.CRAZYFLATE:
            comp = self._engine.CrazyFlateCompressor()
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
