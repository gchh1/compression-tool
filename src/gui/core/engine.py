from __future__ import annotations # 延迟求值

import logging
from pathlib import Path

from gui.core.models import (
    AlgorithmType,

    CompressionStatus,
    FileRecord,
    FolderRecord,
)


# 定义一个日志记录器，用于记录压缩引擎的运行时信息
logger = logging.getLogger(__name__) # __name__ 是当前模块的名称，用于日志记录器的名称
# _core_engine 是一个全局变量，用于存储C++ core_engine的实例
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
    def __init__(self):
        self._engine = _get_engine()

    @property
    def available(self) -> bool:
        return self._engine is not None

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> self._engine.CompressorResult:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if algorithm == AlgorithmType.LZSS:
            compressor = self._engine.LZSSCompressor()
        elif algorithm == AlgorithmType.LZMINE:
            compressor = self._engine.LZMineCompressor()
        elif algorithm == AlgorithmType.DEFLATE:
            compressor = self._engine.DeflateCompressor()

        result = compressor.compress(list(data))

        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.compression_ratio = result.compression_ratio
        cr.time_ms = result.time_ms
        cr.data = list(result.data)
        return cr

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> self._engine.CompressorResult:   
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if algorithm == AlgorithmType.LZSS:
            compressor = self._engine.LZSSCompressor()
        elif algorithm == AlgorithmType.LZMINE:
            compressor = self._engine.LZMineCompressor()
        elif algorithm == AlgorithmType.DEFLATE:
            compressor = self._engine.DeflateCompressor()

        result = compressor.decompress(list(data))
        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.time_ms = result.time_ms
        cr.data = list(result.data)
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
                type=_identify_from_ext(Path(wf.filepath).suffix.lower()),
                raw_data=bytes(wf.context),
            ))
        return records

# 从文件扩展名判断资源类型

