from __future__ import annotations # 延迟求值

import logging
from pathlib import Path

from gui.core.models import (
    AlgorithmType,
    BatchResult,
    CompressionResult,
    CompressionStatus,
    FileRecord,
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

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> CompressionResult:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if algorithm == AlgorithmType.LZSS:
            compressor = self._engine.LZSSCompressor()
        elif algorithm == AlgorithmType.DEFLATE:
            compressor = self._engine.DeflateCompressor()

        result = compressor.compress(list(data))
        return CompressionResult(
            original_size=result.original_size,
            compressed_size=result.compressed_size,
            compression_ratio=result.compression_ratio,
            time_ms=result.time_ms,
            data=bytes(result.data),
        )

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> CompressionResult:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if algorithm == AlgorithmType.LZSS:
            compressor = self._engine.LZSSCompressor()
        elif algorithm == AlgorithmType.DEFLATE:
            compressor = self._engine.DeflateCompressor()

        result = compressor.decompress(list(data))
        return CompressionResult(
            original_size=result.original_size,
            compressed_size=result.compressed_size,
            compression_ratio=result.compression_ratio,
            time_ms=result.time_ms,
            data=bytes(result.data),
        )

    def compress_file(self, record: FileRecord, algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> FileRecord:
        record.algorithm = algorithm
        record.status = CompressionStatus.COMPRESSING
        try:
            result = self.compress(record.raw_data, algorithm)
            record.compressed_data = result.data
            record.compression_ratio = result.compression_ratio
            record.compression_time_ms = result.time_ms
            record.status = CompressionStatus.DONE
        except Exception as e:
            record.status = CompressionStatus.FAILED
            record.error_message = str(e)
            logger.error("compress failed for %s: %s", record.name, e)
        return record

    def compress_batch(self, records: list[FileRecord], algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> BatchResult:
        batch = BatchResult()
        for rec in records:
            result = self.compress_file(rec, algorithm)
            batch.files.append(result)
            if result.status == CompressionStatus.DONE:
                batch.total_original += result.size
                batch.total_compressed += len(result.compressed_data or b"")
                batch.total_time_ms += result.compression_time_ms
            else:
                batch.errors.append(f"{result.name}: {result.error_message}")
        return batch

    def pack_files(self, records: list[FileRecord]) -> bytes:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        web_files = []
        for rec in records:
            wf = self._engine.WebFile()
            wf.name = rec.path
            wf.context = list(rec.raw_data)
            web_files.append(wf)

        packed = self._engine.Archiver.pack(web_files)
        return bytes(packed)

    def unpack_archive(self, data: bytes) -> list[FileRecord]:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        raw_files = self._engine.Archiver.unpack(list(data))
        records = []
        for wf in raw_files:
            records.append(FileRecord(
                path=wf.name,
                name=Path(wf.name).name,
                extension=Path(wf.name).suffix.lower(),
                size=len(wf.context),
                type=_identify_from_ext(Path(wf.name).suffix.lower()),
                raw_data=bytes(wf.context),
            ))
        return records

# 从文件扩展名判断资源类型

def _identify_from_ext(ext: str):
    from gui.core.models import ResourceType, TEXT_EXTENSIONS, IMAGE_EXTENSIONS
    if ext in TEXT_EXTENSIONS:
        return ResourceType.TEXT
    if ext in IMAGE_EXTENSIONS:
        return ResourceType.IMAGE
    return ResourceType.BINARY
