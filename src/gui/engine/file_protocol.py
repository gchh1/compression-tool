from __future__ import annotations

import struct
import logging
import io
from pathlib import Path

from gui.models import AlgorithmType

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
    header = CompressedFileHeader(
        algorithm=algorithm,
        original_size=original_size,
        compressed_size=len(compressed_data),
        original_filename=original_filename,
        is_folder=is_folder,
    )
    return header.to_bytes() + compressed_data


def unpack_compressed_file(data: bytes) -> tuple[CompressedFileHeader, bytes]:
    header = CompressedFileHeader.from_bytes(data)
    payload_start = header.header_total_size()
    if len(data) < payload_start:
        raise ValueError(f"Data too short: {len(data)} < header size {payload_start}")
    payload = data[payload_start:]
    if len(payload) != header.compressed_size:
        logger.warning(
            "Size mismatch: header says %d, actual %d",
            header.compressed_size,
            len(payload),
        )
    return header, payload


def detect_algorithm_from_file(path: str | Path) -> AlgorithmType | None:
    p = Path(path)
    try:
        data = p.read_bytes()
        header = CompressedFileHeader.from_bytes(data)
        return header.algorithm
    except Exception as e:
        logger.warning("[detect] failed to read header from %s: %s", path, e)
        return None


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
