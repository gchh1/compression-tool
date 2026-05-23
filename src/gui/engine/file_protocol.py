from __future__ import annotations

import struct
import logging
import io
from pathlib import Path

from gui.models import AlgorithmType
from gui.engine.bridge import get_core_engine

logger = logging.getLogger(__name__)

MAGIC = b'WCMP'
HEADER_VERSION = 2
UNIFIED_EXTENSION = ".wcx"

ALGO_CODE_STORED = 0

ALGO_CODE_MAP: dict[AlgorithmType, int] = {
    AlgorithmType.NONE: ALGO_CODE_STORED,
    AlgorithmType.DEFLATE: 1,
    AlgorithmType.LZSS: 2,
    AlgorithmType.LZDP: 3,
    AlgorithmType.HUFFMAN: 4,
    AlgorithmType.DPFLATE: 5,
    AlgorithmType.GZIP: 6,
    AlgorithmType.BROTLI: 7,
    AlgorithmType.ZSTD: 8,
    AlgorithmType.JPEG: 10,
    AlgorithmType.WEBP: 11,
}

CODE_TO_ALGO: dict[int, AlgorithmType] = {v: k for k, v in ALGO_CODE_MAP.items()}

FLAG_FOLDER = 1 << 0


class CompressedFileHeader:
    MAGIC: bytes = MAGIC
    VERSION: int = HEADER_VERSION

    def __init__(
        self,
        algorithm: AlgorithmType,
        original_size: int,
        compressed_size: int,
        original_filename: str = "",
        is_folder: bool = False,
    ):
        self.algorithm = algorithm
        self.original_size = original_size
        self.compressed_size = compressed_size
        self.original_filename = original_filename
        self.is_folder = is_folder

    @property
    def algo_code(self) -> int:
        return ALGO_CODE_MAP.get(self.algorithm, 0)

    @property
    def extension(self) -> str:
        return UNIFIED_EXTENSION

    def to_bytes(self) -> bytes:
        filename_bytes = self.original_filename.encode("utf-8")
        filename_len = len(filename_bytes)
        flags = FLAG_FOLDER if self.is_folder else 0
        header = struct.pack(
            "<4sBBIIBHB",
            self.MAGIC,
            self.VERSION,
            self.algo_code,
            self.original_size,
            self.compressed_size,
            flags,
            filename_len,
            0,
        )
        return header + filename_bytes

    @classmethod
    def from_bytes(cls, data: bytes) -> CompressedFileHeader:
        if len(data) < cls.fixed_header_size():
            raise ValueError(f"Header too short: {len(data)} < {cls.fixed_header_size()}")

        magic, version, algo_code, orig_size, comp_size, flags, fname_len, _padding = struct.unpack(
            "<4sBBIIBHB", data[: cls.fixed_header_size()]
        )

        if magic != cls.MAGIC:
            raise ValueError(f"Invalid magic: {magic!r}, expected {cls.MAGIC!r}")
        if version != cls.VERSION:
            raise ValueError(f"Unsupported version: {version}")

        algo = CODE_TO_ALGO.get(algo_code)
        if algo is None:
            raise ValueError(f"Unknown algorithm code: {algo_code}")

        is_folder = bool(flags & FLAG_FOLDER)

        fname_start = cls.fixed_header_size()
        fname_end = fname_start + fname_len
        if len(data) < fname_end:
            raise ValueError("Filename truncated in header")
        original_filename = data[fname_start:fname_end].decode("utf-8")

        hdr = cls(algo, orig_size, comp_size, original_filename, is_folder=is_folder)
        hdr._header_total_size = fname_end
        return hdr

    @classmethod
    def fixed_header_size(cls) -> int:
        return struct.calcsize("<4sBBIIBHB")

    def header_total_size(self) -> int:
        if hasattr(self, "_header_total_size"):
            return self._header_total_size
        return self.fixed_header_size() + len(self.original_filename.encode("utf-8"))


def pack_compressed_file(
    compressed_data: bytes,
    algorithm: AlgorithmType,
    original_size: int,
    original_filename: str = "",
    is_folder: bool = False,
) -> bytes:
    engine = get_core_engine()
    if engine is None or not hasattr(engine, "pack_wcx"):
        raise RuntimeError("WCX container write requires core_engine.pack_wcx")
    algo_map = {
        AlgorithmType.NONE: engine.AlgorithmID.NONE,
        AlgorithmType.DEFLATE: engine.AlgorithmID.DEFLATE,
        AlgorithmType.GZIP: engine.AlgorithmID.DEFLATE,
        AlgorithmType.LZSS: engine.AlgorithmID.LZSS,
        AlgorithmType.LZDP: engine.AlgorithmID.LZDP,
        AlgorithmType.DPFLATE: engine.AlgorithmID.DPFLATE,
        AlgorithmType.BROTLI: engine.AlgorithmID.BROTLI,
        AlgorithmType.ZSTD: engine.AlgorithmID.ZSTD,
        AlgorithmType.JPEG: engine.AlgorithmID.JPEG_COMPRESS,
        AlgorithmType.WEBP: engine.AlgorithmID.WEBP_COMPRESS,
    }
    algo_id = algo_map.get(algorithm)
    if algo_id is None:
        raise RuntimeError(f"WCX pack: algorithm not supported by core_engine: {algorithm}")
    packed = engine.pack_wcx(
        compressed_data,
        algo_id,
        int(original_size),
        original_filename,
        bool(is_folder),
    )
    return bytes(packed)


def unpack_compressed_file(data: bytes) -> tuple[CompressedFileHeader, bytes]:
    engine = get_core_engine()
    if engine is None or not hasattr(engine, "unpack_wcx"):
        raise RuntimeError("WCX container read requires core_engine.unpack_wcx")
    unpacked = engine.unpack_wcx(data)
    if not unpacked.success:
        msg = getattr(unpacked, "error_message", "") or "unpack_wcx failed"
        raise ValueError(msg)
    algo = CODE_TO_ALGO.get(int(unpacked.algo_code), AlgorithmType.NONE)
    header = CompressedFileHeader(
        algorithm=algo,
        original_size=int(unpacked.original_size),
        compressed_size=int(unpacked.compressed_size),
        original_filename=unpacked.original_filename,
        is_folder=bool(unpacked.is_folder),
    )
    return header, bytes(unpacked.payload)


def file_record_compression_blob(record: object) -> bytes | None:
    """Full WCX bytes for a ``FileRecord``-like object (memory or disk streaming output)."""
    data = getattr(record, "compressed_data", None)
    if isinstance(data, (bytes, bytearray)) and len(data) > 0:
        return bytes(data)
    path = getattr(record, "compressed_path", None)
    if path:
        try:
            p = Path(path)
            if p.is_file():
                return p.read_bytes()
        except OSError:
            return None
    return None


def strip_wcx_if_present(container: bytes) -> bytes:
    """If ``container`` is a WCMP v2 file, return inner algorithm payload; else ``container``."""
    if len(container) >= 4 and container[:4] == MAGIC:
        try:
            _, payload = unpack_compressed_file(container)
            return payload
        except Exception:
            return container
    return container





def make_export_filename(original_name: str, algorithm: AlgorithmType | None = None) -> str:
    base = original_name
    for ext in ('.wcl', '.wcm', '.wcs', '.wch', '.wcf', '.wcd', '.wcg', '.wcx', '.archive'):
        if base.lower().endswith(ext):
            base = base[:-len(ext)]
            break
    return base + UNIFIED_EXTENSION


def pack_folder_archive(
    folder_name: str,
    files: list[tuple[str, bytes, AlgorithmType, int]],
) -> bytes:
    buf = io.BytesIO()
    buf.write(struct.pack('<I', len(files)))
    for relative_path, comp_data, algo, orig_size in files:
        inner_packed = pack_compressed_file(
            comp_data, algo, orig_size,
            original_filename=relative_path,
            is_folder=False,
        )
        chunk_size = len(inner_packed)
        buf.write(struct.pack('<I', chunk_size))
        buf.write(inner_packed)
    payload = buf.getvalue()
    archive = pack_compressed_file(
        payload, AlgorithmType.NONE, len(payload),
        original_filename=folder_name,
        is_folder=True,
    )
    return archive


def unpack_folder_archive(data: bytes) -> list[tuple[CompressedFileHeader, bytes]]:
    outer_header, outer_payload = unpack_compressed_file(data)
    if not outer_header.is_folder:
        raise ValueError("Not a folder archive")
    files = []
    pos = 0
    if pos + 4 > len(outer_payload):
        return files
    file_count = struct.unpack('<I', outer_payload[pos:pos+4])[0]
    pos += 4
    for _ in range(file_count):
        if pos + 4 > len(outer_payload):
            break
        chunk_size = struct.unpack('<I', outer_payload[pos:pos+4])[0]
        pos += 4
        if pos + chunk_size > len(outer_payload):
            break
        chunk = outer_payload[pos:pos+chunk_size]
        pos += chunk_size
        try:
            inner_header, inner_payload = unpack_compressed_file(chunk)
            files.append((inner_header, inner_payload))
        except Exception as e:
            logger.warning("[unpack] skip invalid chunk at offset %d: %s", pos - chunk_size, e)
    return files
