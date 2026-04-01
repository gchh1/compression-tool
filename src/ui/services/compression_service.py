import os
import shutil
import struct
import sys
import time
from pathlib import Path
from typing import Dict, List, Tuple

from .models import LZ77Demo, LZ77DemoStep, OperationResult, ResourceInfo


APP_DIR = Path(__file__).resolve().parents[3]
RESOURCES_DIR = APP_DIR / "resources"
STAGING_DIR = RESOURCES_DIR / "staging"
COMPRESSED_DIR = APP_DIR / "compressed"
DECOMPRESSED_DIR = APP_DIR / "decompressed"
MAGIC = b"LZ77"
# Container type
TYPE_FILE = 0
TYPE_FOLDER = 1
# Header flags
FLAG_PAYLOAD_COMPRESSED = 1 << 0
FLAG_ARCHIVE_COMPRESSED = 1 << 1
# Keep visualization parsing aligned with current C++ compile-time config in lz77_core.h:
#   LZ77_SEARCH_WINDOW=16 -> offset field uses 2 bytes (little endian)
#   LZ77_LOOKAHEAD_WINDOW=8 -> length field uses 1 byte
OFFSET_FIELD_WIDTH = 2
LENGTH_FIELD_WIDTH = 1


def _load_lz77():
    bin_dir = APP_DIR / "bin"
    if str(bin_dir) not in sys.path:
        sys.path.insert(0, str(bin_dir))
    mingw_bin = Path("D:/AAA_C/AAA_MinGW/mingw64/bin")
    if hasattr(os, "add_dll_directory"):
        if mingw_bin.exists():
            os.add_dll_directory(str(mingw_bin))
        if bin_dir.exists():
            os.add_dll_directory(str(bin_dir))
    try:
        import lz77  # type: ignore
    except Exception as exc:  # pragma: no cover - runtime environment dependent
        raise RuntimeError(
            "Cannot import lz77 module. Build project first so bin/lz77* exists."
        ) from exc
    return lz77


def _ensure_dirs() -> None:
    STAGING_DIR.mkdir(parents=True, exist_ok=True)
    COMPRESSED_DIR.mkdir(parents=True, exist_ok=True)
    DECOMPRESSED_DIR.mkdir(parents=True, exist_ok=True)


def _timestamp_tag() -> str:
    return time.strftime("%Y%m%d_%H%M%S")


def _count_files_and_size(path: Path) -> Tuple[int, int]:
    if path.is_file():
        return 1, path.stat().st_size
    count = 0
    total = 0
    for p in path.rglob("*"):
        if p.is_file():
            count += 1
            total += p.stat().st_size
    return count, total


def _is_already_compressed(path: Path) -> bool:
    return path.suffix.lower() in {
        ".png",
        ".jpg",
        ".jpeg",
        ".gif",
        ".webp",
        ".zip",
        ".gz",
        ".7z",
        ".rar",
        ".woff",
        ".woff2",
        ".mp3",
        ".mp4",
        ".pdf",
    }


def _make_file_container(
    source_name: str,
    raw_data: bytes,
    payload: bytes,
    payload_compressed: bool,
    search_size: int,
    lookahead_size: int,
) -> bytes:
    name_b = source_name.encode("utf-8")
    flags = FLAG_PAYLOAD_COMPRESSED if payload_compressed else 0
    header = bytearray()
    header.extend(MAGIC)
    header.extend(struct.pack("<B", TYPE_FILE))
    header.extend(struct.pack("<B", flags))
    header.extend(struct.pack("<B", search_size & 0xFF))
    header.extend(struct.pack("<B", lookahead_size & 0xFF))
    header.extend(struct.pack("<H", len(name_b)))
    header.extend(name_b)
    header.extend(struct.pack("<Q", len(raw_data)))
    header.extend(struct.pack("<Q", len(payload)))
    return bytes(header) + payload


def _parse_file_container(blob: bytes) -> Tuple[Dict[str, object], bytes]:
    if len(blob) < 4 + 1 + 1 + 1 + 1 + 2 + 8 + 8:
        raise ValueError("Container too short.")
    if blob[:4] != MAGIC:
        raise ValueError("Invalid container magic.")
    pos = 4
    kind = blob[pos]
    pos += 1
    flags = blob[pos]
    pos += 1
    search_size = blob[pos]
    pos += 1
    lookahead_size = blob[pos]
    pos += 1
    name_len = struct.unpack("<H", blob[pos : pos + 2])[0]
    pos += 2
    name = blob[pos : pos + name_len].decode("utf-8")
    pos += name_len
    original_size = struct.unpack("<Q", blob[pos : pos + 8])[0]
    pos += 8
    payload_size = struct.unpack("<Q", blob[pos : pos + 8])[0]
    pos += 8
    payload = blob[pos : pos + payload_size]
    return (
        {
            "kind": kind,
            "flags": flags,
            "source_name": name,
            "search_size": search_size,
            "lookahead_size": lookahead_size,
            "original_size": original_size,
            "payload_size": payload_size,
        },
        payload,
    )


def _pack_folder_entries(directory_path: Path, search_size: int, lookahead_size: int) -> bytes:
    lz77 = _load_lz77()
    entries = []
    for file_path in sorted(directory_path.rglob("*")):
        if not file_path.is_file():
            continue
        rel_path = file_path.relative_to(directory_path).as_posix()
        raw = file_path.read_bytes()
        if _is_already_compressed(file_path):
            comp = raw
            is_comp = False
        else:
            c = lz77.compress(raw, search_size, lookahead_size)
            if len(c) < len(raw):
                comp = c
                is_comp = True
            else:
                comp = raw
                is_comp = False
        entries.append((rel_path.encode("utf-8"), raw, comp, is_comp))

    buf = bytearray()
    buf.extend(struct.pack("<I", len(entries)))
    for rel_b, raw, comp, is_comp in entries:
        flags = FLAG_PAYLOAD_COMPRESSED if is_comp else 0
        buf.extend(struct.pack("<H", len(rel_b)))
        buf.extend(rel_b)
        buf.extend(struct.pack("<B", flags))
        buf.extend(struct.pack("<B", search_size & 0xFF))
        buf.extend(struct.pack("<B", lookahead_size & 0xFF))
        buf.extend(struct.pack("<Q", len(raw)))
        buf.extend(struct.pack("<Q", len(comp)))
        buf.extend(comp)
    return bytes(buf)


def _unpack_folder_entries(packed_data: bytes, output_dir: Path) -> int:
    lz77 = _load_lz77()
    output_dir.mkdir(parents=True, exist_ok=True)
    pos = 0
    count = struct.unpack("<I", packed_data[pos : pos + 4])[0]
    pos += 4
    restored = 0
    for _ in range(count):
        path_len = struct.unpack("<H", packed_data[pos : pos + 2])[0]
        pos += 2
        rel = packed_data[pos : pos + path_len].decode("utf-8")
        pos += path_len
        flags = packed_data[pos]
        pos += 1
        pos += 1  # search_size
        pos += 1  # lookahead_size
        _ = struct.unpack("<Q", packed_data[pos : pos + 8])[0]
        pos += 8
        payload_len = struct.unpack("<Q", packed_data[pos : pos + 8])[0]
        pos += 8
        payload = packed_data[pos : pos + payload_len]
        pos += payload_len
        data = lz77.decompress(payload) if (flags & FLAG_PAYLOAD_COMPRESSED) else payload

        out_path = output_dir / rel
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(data)
        restored += 1
    return restored


def _make_folder_container(
    source_name: str,
    archive_stream: bytes,
    payload: bytes,
    archive_compressed: bool,
    search_size: int,
    lookahead_size: int,
) -> bytes:
    name_b = source_name.encode("utf-8")
    flags = FLAG_ARCHIVE_COMPRESSED if archive_compressed else 0
    header = bytearray()
    header.extend(MAGIC)
    header.extend(struct.pack("<B", TYPE_FOLDER))
    header.extend(struct.pack("<B", flags))
    header.extend(struct.pack("<B", search_size & 0xFF))
    header.extend(struct.pack("<B", lookahead_size & 0xFF))
    header.extend(struct.pack("<H", len(name_b)))
    header.extend(name_b)
    header.extend(struct.pack("<Q", len(archive_stream)))
    header.extend(struct.pack("<Q", len(payload)))
    return bytes(header) + payload


def _safe_symbol(value: int) -> str:
    if 32 <= value <= 126:
        return chr(value)
    return f"\\x{value:02x}"


def import_resource(src_path: str) -> ResourceInfo:
    _ensure_dirs()
    source = Path(src_path).expanduser().resolve()
    if not source.exists():
        raise FileNotFoundError(f"Resource not found: {source}")

    stage_name = f"{_timestamp_tag()}_{source.name}"
    staged = STAGING_DIR / stage_name
    if source.is_file():
        shutil.copy2(source, staged)
        resource_type = "file"
    else:
        shutil.copytree(source, staged)
        resource_type = "folder"

    file_count, size_bytes = _count_files_and_size(staged)
    return ResourceInfo(
        source_path=str(source),
        staged_path=str(staged),
        name=source.name,
        resource_type=resource_type,
        size_bytes=size_bytes,
        file_count=file_count,
        metadata={"policy": "staged_copy"},
    )


def compress_resource(
    target_path: str, search_size: int = 255, lookahead_size: int = 255
) -> OperationResult:
    _ensure_dirs()
    lz77 = _load_lz77()
    target = Path(target_path).expanduser().resolve()
    if not target.exists():
        raise FileNotFoundError(f"Target not found: {target}")

    source_name = target.name
    start = time.time()
    if target.is_file():
        kind = "file"
        raw_data = target.read_bytes()
        compressed_data = lz77.compress(raw_data, search_size, lookahead_size)
        payload_compressed = len(compressed_data) < len(raw_data)
        payload = compressed_data if payload_compressed else raw_data
        archive = _make_file_container(
            source_name, raw_data, payload, payload_compressed, search_size, lookahead_size
        )
        detail_flag = payload_compressed
    else:
        kind = "folder"
        archive_stream = _pack_folder_entries(target, search_size, lookahead_size)
        compressed_stream = lz77.compress(archive_stream, search_size, lookahead_size)
        archive_compressed = len(compressed_stream) < len(archive_stream)
        payload = compressed_stream if archive_compressed else archive_stream
        archive = _make_folder_container(
            source_name, archive_stream, payload, archive_compressed, search_size, lookahead_size
        )
        raw_data = archive_stream
        detail_flag = archive_compressed
    duration = time.time() - start

    output_path = COMPRESSED_DIR / f"{source_name}.lz77"
    output_path.write_bytes(archive)

    return OperationResult(
        success=True,
        message="Compressed successfully." if detail_flag else "Stored original payload (no gain).",
        input_path=str(target),
        output_path=str(output_path),
        original_size=len(raw_data),
        output_size=len(archive),
        duration_sec=duration,
        # ratio is compression effectiveness of payload itself (exclude container header).
        ratio=(len(payload) / len(raw_data)) if raw_data else 0.0,
        metadata={
            "kind": kind,
            "is_compressed": str(detail_flag),
            "search_size": str(search_size),
            "lookahead_size": str(lookahead_size),
            "payload_size": str(len(payload)),
            "archive_size": str(len(archive)),
            "package_ratio": f"{(len(archive) / len(raw_data)) if raw_data else 0.0:.6f}",
        },
    )


def decompress_resource(archive_path: str) -> OperationResult:
    _ensure_dirs()
    lz77 = _load_lz77()
    archive = Path(archive_path).expanduser().resolve()
    if not archive.exists():
        raise FileNotFoundError(f"Archive not found: {archive}")

    blob = archive.read_bytes()
    header, payload = _parse_file_container(blob)
    kind = int(header["kind"])
    flags = int(header["flags"])
    source_name = str(header["source_name"])

    start = time.time()
    if kind == TYPE_FILE:
        raw_data = lz77.decompress(payload) if (flags & FLAG_PAYLOAD_COMPRESSED) else payload
        output_path = DECOMPRESSED_DIR / source_name
        output_path.write_bytes(raw_data)
        message = "File restored."
    elif kind == TYPE_FOLDER:
        stream = lz77.decompress(payload) if (flags & FLAG_ARCHIVE_COMPRESSED) else payload
        output_path = DECOMPRESSED_DIR / f"{source_name}_restored"
        file_count = _unpack_folder_entries(stream, output_path)
        raw_data = stream
        message = f"Folder restored with {file_count} files."
    else:
        raise ValueError("Unsupported container type.")
    duration = time.time() - start

    return OperationResult(
        success=True,
        message=message,
        input_path=str(archive),
        output_path=str(output_path),
        original_size=len(blob),
        output_size=len(raw_data),
        duration_sec=duration,
        ratio=(len(raw_data) / len(blob)) if blob else 0.0,
        metadata={
            "kind": "folder" if kind == TYPE_FOLDER else "file",
            "is_compressed": str(bool(flags & (FLAG_PAYLOAD_COMPRESSED | FLAG_ARCHIVE_COMPRESSED))),
        },
    )


def build_lz77_demo_steps(
    test_file_path: str, search_size: int = 255, lookahead_size: int = 255
) -> LZ77Demo:
    path = Path(test_file_path).expanduser().resolve()
    if not path.exists():
        raise FileNotFoundError(f"Demo source not found: {path}")

    data = path.read_bytes()
    source_text = data.decode("utf-8", errors="replace")
    lz77 = _load_lz77()
    compressed = lz77.compress(data, search_size, lookahead_size)

    def _read_u(encoded: bytes, pos: int, width: int) -> Tuple[int, int]:
        if width == 2:
            if pos + 1 >= len(encoded):
                return 0, len(encoded)
            return encoded[pos] | (encoded[pos + 1] << 8), pos + 2
        if pos >= len(encoded):
            return 0, len(encoded)
        return encoded[pos], pos + 1

    steps: List[LZ77DemoStep] = []
    in_pos = 0
    cursor = 0
    step_index = 0
    while in_pos < len(compressed):
        token_start = in_pos
        offset, in_pos = _read_u(compressed, in_pos, OFFSET_FIELD_WIDTH)
        length, in_pos = _read_u(compressed, in_pos, LENGTH_FIELD_WIDTH)

        search_start = max(0, cursor - search_size)
        search_buf = data[search_start:cursor]
        lookahead = data[cursor : cursor + lookahead_size]

        if offset == 0:
            # Literal run format in core: [offset=0][length][length raw bytes]
            literal_bytes = compressed[in_pos : in_pos + length]
            in_pos += length
            literal_preview = literal_bytes.decode("utf-8", errors="replace")
            token_value = f"literal_run(len={length})"
            token_bytes = compressed[token_start:in_pos]
            steps.append(
                LZ77DemoStep(
                    step_index=step_index,
                    cursor=cursor,
                    search_window=search_buf.decode("utf-8", errors="replace"),
                    lookahead_window=lookahead.decode("utf-8", errors="replace"),
                    token_type="literal_run",
                    token_value=token_value,
                    match_length=0,
                    match_distance=0,
                    next_symbol=literal_preview,
                    consumed_length=length,
                    literal_length=length,
                    token_bytes_hex=token_bytes.hex(),
                    literal_bytes_hex=literal_bytes.hex(),
                )
            )
            cursor += length
        else:
            # Match format in core: [offset][length][next_byte]
            next_byte = compressed[in_pos] if in_pos < len(compressed) else 0
            in_pos += 1 if in_pos < len(compressed) else 0
            next_symbol = _safe_symbol(next_byte)
            token_value = f"({offset}, {length}, {next_symbol})"
            token_bytes = compressed[token_start:in_pos]
            steps.append(
                LZ77DemoStep(
                    step_index=step_index,
                    cursor=cursor,
                    search_window=search_buf.decode("utf-8", errors="replace"),
                    lookahead_window=lookahead.decode("utf-8", errors="replace"),
                    token_type="match",
                    token_value=token_value,
                    match_length=length,
                    match_distance=offset,
                    next_symbol=next_symbol,
                    consumed_length=length + 1,
                    literal_length=0,
                    token_bytes_hex=token_bytes.hex(),
                    literal_bytes_hex="",
                )
            )
            cursor += length + 1
        step_index += 1

    return LZ77Demo(source_path=str(path), source_text=source_text, steps=steps)
