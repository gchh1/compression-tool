from __future__ import annotations

import struct
import logging
import io
from pathlib import Path

from gui.models import AlgorithmType
from gui.engine.bridge import get_core_engine

logger = logging.getLogger(__name__)

MAGIC = b'WCMP'
HEADER_VERSION = 3
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
    AlgorithmType.PNG: 11,
    AlgorithmType.FLAC: 12,
    AlgorithmType.AAC_LC: 13,
    AlgorithmType.H264: 14,
}

CODE_TO_ALGO: dict[int, AlgorithmType] = {v: k for k, v in ALGO_CODE_MAP.items()}

FLAG_FOLDER = 1 << 0
FLAG_WEB_DICT_PREPROCESS = 1 << 1


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
        web_dict_preprocess: bool = False,
    ):
        self.algorithm = algorithm
        self.original_size = original_size
        self.compressed_size = compressed_size
        self.original_filename = original_filename
        self.is_folder = is_folder
        self.web_dict_preprocess = bool(web_dict_preprocess)

    @property
    def algo_code(self) -> int:
        return ALGO_CODE_MAP.get(self.algorithm, 0)

    @property
    def extension(self) -> str:
        return UNIFIED_EXTENSION

    def to_bytes(self) -> bytes:
        filename_bytes = self.original_filename.encode("utf-8")
        filename_len = len(filename_bytes)
        flags = 0
        if self.is_folder:
            flags |= FLAG_FOLDER
        if self.web_dict_preprocess:
            flags |= FLAG_WEB_DICT_PREPROCESS
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
        web_dict_preprocess = bool(flags & FLAG_WEB_DICT_PREPROCESS)

        fname_start = cls.fixed_header_size()
        fname_end = fname_start + fname_len
        if len(data) < fname_end:
            raise ValueError("Filename truncated in header")
        original_filename = data[fname_start:fname_end].decode("utf-8")

        hdr = cls(
            algo,
            orig_size,
            comp_size,
            original_filename,
            is_folder=is_folder,
            web_dict_preprocess=web_dict_preprocess,
        )
        hdr._header_total_size = fname_end
        return hdr

    @classmethod
    def fixed_header_size(cls) -> int:
        return struct.calcsize("<4sBBIIBHB")

    def header_total_size(self) -> int:
        if hasattr(self, "_header_total_size"):
            return self._header_total_size
        return self.fixed_header_size() + len(self.original_filename.encode("utf-8"))


def _pack_compressed_file_python_mirror(
    compressed_data: bytes,
    algorithm: AlgorithmType,
    original_size: int,
    original_filename: str = "",
    is_folder: bool = False,
    web_dict_preprocess: bool = False,
) -> bytes:
    """Last-resort mirror of C++ ``wcx::buildHeaderBytes`` when ``core_engine`` is absent.

    Not a separate protocol — byte layout must stay in sync with ``WCXProtocol.cpp``.
    """
    if algorithm not in ALGO_CODE_MAP:
        raise RuntimeError(f"WCX pack: algorithm not supported: {algorithm}")
    payload = bytes(compressed_data)
    header = CompressedFileHeader(
        algorithm=algorithm,
        original_size=int(original_size),
        compressed_size=len(payload),
        original_filename=original_filename or "",
        is_folder=bool(is_folder),
        web_dict_preprocess=bool(web_dict_preprocess),
    )
    return header.to_bytes() + payload


def pack_compressed_file(
    compressed_data: bytes,
    algorithm: AlgorithmType,
    original_size: int,
    original_filename: str = "",
    is_folder: bool = False,
    web_dict_preprocess: bool = False,
) -> bytes:
    """Wrap codec payload in WCMP v3 via C++ ``api::pack_wcx`` (canonical wire format).

  Python callers (GUI export, ADE after streaming completes, folder archive) invoke this
  at **completion** time only; streaming jobs hold raw/framed payload until then.
    """
    engine = get_core_engine()
    if engine is not None and hasattr(engine, "pack_wcx"):
        algo_map = {
            AlgorithmType.NONE: engine.AlgorithmID.NONE,
            AlgorithmType.DEFLATE: engine.AlgorithmID.DEFLATE,
            AlgorithmType.LZSS: engine.AlgorithmID.LZSS,
            AlgorithmType.LZDP: engine.AlgorithmID.LZDP,
            AlgorithmType.DPFLATE: engine.AlgorithmID.DPFLATE,
            AlgorithmType.BROTLI: engine.AlgorithmID.BROTLI,
            AlgorithmType.ZSTD: engine.AlgorithmID.ZSTD,
            AlgorithmType.JPEG: engine.AlgorithmID.IMAGE_JPEG,
            AlgorithmType.PNG: engine.AlgorithmID.IMAGE_PNG,
            AlgorithmType.FLAC: engine.AlgorithmID.AUDIO_FLAC,
            AlgorithmType.AAC_LC: engine.AlgorithmID.AUDIO_AAC_LC,
            AlgorithmType.H264: engine.AlgorithmID.VIDEO_H264,
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
            bool(web_dict_preprocess),
        )
        return bytes(packed)
    logger.warning(
        "[file_protocol] core_engine.pack_wcx unavailable; using Python mirror of WCX v3"
    )
    return _pack_compressed_file_python_mirror(
        compressed_data,
        algorithm,
        original_size,
        original_filename,
        is_folder,
        web_dict_preprocess,
    )


def finalize_codec_payload_to_wcx(
    payload: bytes,
    algorithm: AlgorithmType,
    original_size: int,
    original_filename: str = "",
    *,
    is_folder: bool = False,
    web_dict_preprocess: bool = False,
) -> bytes:
    """Wrap a finished streaming/memory codec payload into WCX (ADE / export completion hook).

    Streaming jobs should keep raw or u32-framed bytes until success/cancel is resolved, then call
    this once so ``pack_wcx`` runs in C++ with the canonical ``WCXProtocol`` layout.
    """
    return pack_compressed_file(
        payload,
        algorithm,
        int(original_size),
        original_filename,
        is_folder,
        web_dict_preprocess,
    )


def wcx_bytes_for_file_record(record: object) -> bytes | None:
    """Full ``.wcx`` bytes for export: pass through disk/memory WCX or wrap raw payload."""
    blob = file_record_compression_blob(record)
    if not blob:
        return None
    if len(blob) >= 4 and blob[:4] == MAGIC:
        return blob
    algo = getattr(record, "algorithm", AlgorithmType.NONE)
    if algo in (AlgorithmType.JPEG, AlgorithmType.PNG):
        return blob
    if getattr(record, "is_stored", False):
        algo = AlgorithmType.NONE
    name = getattr(record, "name", "") or ""
    orig = int(getattr(record, "size", 0) or 0)
    web_dict = bool(getattr(record, "web_dict_preprocess", False))
    return pack_compressed_file(
        blob, algo, orig, name, is_folder=False, web_dict_preprocess=web_dict
    )


def unpack_compressed_file(data: bytes) -> tuple[CompressedFileHeader, bytes]:
    engine = get_core_engine()
    if engine is None or not hasattr(engine, "unpack_wcx"):
        raise RuntimeError("WCX container read requires core_engine.unpack_wcx")
    unpacked = engine.unpack_wcx(data)
    if not unpacked.success:
        msg = getattr(unpacked, "error_message", "") or "unpack_wcx failed"
        raise ValueError(msg)
    algo = CODE_TO_ALGO.get(int(unpacked.algo_code), AlgorithmType.NONE)
    web_dict = bool(getattr(unpacked, "web_dict_preprocess", False))
    header = CompressedFileHeader(
        algorithm=algo,
        original_size=int(unpacked.original_size),
        compressed_size=int(unpacked.compressed_size),
        original_filename=unpacked.original_filename,
        is_folder=bool(unpacked.is_folder),
        web_dict_preprocess=web_dict,
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


def deframe_u32_be_chunk_stream(payload: bytes) -> bytes:
    """Concatenate ``(be_u32 len || chunk)* || 0`` wire format from ``compressFile`` / pipeline."""
    if not is_u32_be_chunk_framed_stream_payload(payload):
        return payload
    out = bytearray()
    pos = 0
    n = len(payload)
    while pos + 4 <= n:
        sz = int.from_bytes(payload[pos : pos + 4], "big")
        pos += 4
        if sz == 0:
            break
        out.extend(payload[pos : pos + sz])
        pos += sz
    return bytes(out)


def dpflate_format_byte(payload: bytes) -> int | None:
    """Leading DPFlate stream tag: ``0x46`` = FLATE/Inflate, ``0x33`` = 3HfMT."""
    if not payload:
        return None
    b = payload[0]
    if b in (0x46, 0x33):
        return b
    return None


def prepare_token_parse_payload(
    payload: bytes, algorithm: AlgorithmType | None = None
) -> bytes:
    """Normalize WCX inner bytes for GUI token parsers (deframe + codec prefix strip)."""
    payload = deframe_u32_be_chunk_stream(payload)
    if algorithm in (AlgorithmType.DPFLATE, AlgorithmType.DEFLATE):
        fb = dpflate_format_byte(payload)
        if fb in (0x46, 0x33):
            return payload[1:]
    return payload


def compression_blob_for_visualization(record: object) -> tuple[bytes, bytes]:
    """Return ``(inner_after_wcx, parse_ready)`` for heatmap / demo parsers."""
    blob = file_record_compression_blob(record) or b""
    inner = strip_wcx_if_present(blob)
    algo = getattr(record, "algorithm", None)
    return inner, prepare_token_parse_payload(inner, algo)


def is_u32_be_chunk_framed_stream_payload(payload: bytes) -> bool:
    """Return True if ``payload`` matches ``StreamingCompressAdapter`` / ``WholeFileFramedCompressAdapter`` wire format.

    Format: ``(be_u32 chunk_len || chunk_bytes)*`` then ``be_u32 0`` terminator, consuming the entire buffer.

    ``compressFile`` / ``pipeline_compress`` emit this; one-shot ``compress()`` emits raw codec bytes without
    these length prefixes. ``smart_decompress`` must use ``pipeline_decompress`` for framed payloads even when
    the payload is below the size threshold, otherwise native one-shot decode may crash.
    """
    pos = 0
    n = len(payload)
    if n < 8:
        return False
    saw_chunk = False
    while pos + 4 <= n:
        sz = int.from_bytes(payload[pos : pos + 4], "big")
        pos += 4
        if sz == 0:
            return saw_chunk and pos == n
        if sz > n - pos:
            return False
        saw_chunk = True
        pos += sz
    return False


def detect_algorithm_from_file(path: str | Path) -> AlgorithmType | None:
    p = Path(path)
    try:
        data = p.read_bytes()
        header, _ = unpack_compressed_file(data)
        return header.algorithm
    except Exception as e:
        logger.warning("[detect] failed to read header from %s: %s", path, e)
        return None


def make_export_filename(original_name: str, algorithm: AlgorithmType | None = None) -> str:
    if algorithm == AlgorithmType.JPEG:
        return Path(original_name).stem + ".jpg"
    if algorithm == AlgorithmType.PNG:
        return Path(original_name).stem + ".png"
    base = original_name
    for ext in ('.wcl', '.wcm', '.wcs', '.wch', '.wcf', '.wcd', '.wcg', '.wcx', '.archive'):
        if base.lower().endswith(ext):
            base = base[:-len(ext)]
            break
    return base + UNIFIED_EXTENSION


def pack_folder_archive(
    folder_name: str,
    files: list[tuple[str, bytes, AlgorithmType, int, bool]],
) -> bytes:
    buf = io.BytesIO()
    buf.write(struct.pack('<I', len(files)))
    for relative_path, comp_data, algo, orig_size, web_dict in files:
        inner_packed = pack_compressed_file(
            comp_data, algo, orig_size,
            original_filename=relative_path,
            is_folder=False,
            web_dict_preprocess=web_dict,
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
