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
"""
b"..."：表示这是一个字节字符串（bytes literal），在 Python 中每个字符占一个字节
"LZ77"：4个ASCII字符，对应十六进制 0x4C 0x5A 0x37 0x37
"""
# Container type
FILE_TYPE = 0
FOLDER_TYPE = 1
# Header flags
FLAG_PAYLOAD_COMPRESSED = 1 << 0
FLAG_ARCHIVE_COMPRESSED = 1 << 1
# Keep visualization parsing aligned with current C++ compile-time config in lz77_core.h:
#   LZ77_SEARCH_WINDOW=16 -> offset field uses 2 bytes (little endian)
#   LZ77_LOOKAHEAD_WINDOW=8 -> length field uses 1 byte
OFFSET_FIELD_WIDTH = 2
LENGTH_FIELD_WIDTH = 1

# 可修改宏定义
MINGW_BIN = Path("D:/AAA_C/AAA_MinGW/mingw64/bin")
# 保留clangd编译器的路径
CLANGANGD_BIN = None


def load_lz77():
    bin_dir = APP_DIR / "bin"
    if str(bin_dir) not in sys.path:
        sys.path.insert(0, str(bin_dir))
    if hasattr(os, "add_dll_directory"):
        if MINGW_BIN.exists():
            os.add_dll_directory(str(MINGW_BIN))  # 添加MinGW的bin目录到DLL搜索路径
        if bin_dir.exists():
            os.add_dll_directory(str(bin_dir))  # 添加项目bin目录到DLL搜索路径
    try:
        import lz77  # type: ignore
    except Exception as exc:  # pragma: no cover - runtime environment dependent
        raise RuntimeError(
            "Cannot import lz77 module. Build project first so bin/lz77* exists."
        ) from exc
    return lz77


def check_dirs() -> None:
    STAGING_DIR.mkdir(parents=True, exist_ok=True)
    COMPRESSED_DIR.mkdir(parents=True, exist_ok=True)
    DECOMPRESSED_DIR.mkdir(parents=True, exist_ok=True)


def timestamp() -> str: # 生成时间戳标签
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


def iscompressed(path: Path) -> bool:
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


def get_file_container(
    # source_name: str,
    raw_data: bytes,
    payload: bytes,
    payload_compressed: bool, # 有效载荷
    search_size: int,
    lookahead_size: int,
) -> bytes:
    # name_b = source_name.encode("utf-8")
    flags = FLAG_PAYLOAD_COMPRESSED if payload_compressed else 0
    header = bytearray()
    header.extend(MAGIC)
    header.extend(struct.pack("<B", FILE_TYPE)) # "<B" 表示小端字节序, 1字节
    header.extend(struct.pack("<B", flags))
    header.extend(struct.pack("<B", search_size & 0xFF))
    header.extend(struct.pack("<B", lookahead_size & 0xFF))
    # header.extend(struct.pack("<H", len(name_b))) # "<H" 表示小端字节序, 2字节
    # header.extend(name_b) # 变长文件名
    header.extend(struct.pack("<Q", len(raw_data))) # "<Q" 表示小端字节序, 8字节, 记录原数据大小
    header.extend(struct.pack("<Q", len(payload)))
    return bytes(header) + payload


def parse_file_container(blob: bytes) -> Tuple[Dict[str, object], bytes]:
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
    # name_len = struct.unpack("<H", blob[pos : pos + 2])[0]
    # pos += 2
    # name = blob[pos : pos + name_len].decode("utf-8")
    # pos += name_len
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


def pack_folder_entries(directory_path: Path) -> bytes:
    lz77 = load_lz77()
    entries = []
    for file_path in sorted(directory_path.rglob("*")): # 递归遍历目录下的所有文件
        if not file_path.is_file():
            continue
        rel_path = file_path.relative_to(directory_path).as_posix() # POSIX风格，使用正斜杠'/'作为路径分隔符
        raw = file_path.read_bytes()
        if iscompressed(file_path):
            comp = raw
            is_comp = False
        else:
            if file_path.suffix.lower() == ".lz77":
                c = lz77.compress(raw)
                if len(c) < len(raw):
                    comp = c
                    is_comp = True
            else:
                comp = raw
                is_comp = False
        entries.append((rel_path.encode("utf-8"), raw, comp, is_comp))

    buf = bytearray()
    buf.extend(struct.pack("<I", len(entries))) # "<I" 表示小端字节序, 4字节, 记录文件数量
    for path, raw, comp, is_comp in entries:
        # flags = FLAG_PAYLOAD_COMPRESSED if is_comp else 0
        flags = is_comp
        buf.extend(struct.pack("<H", len(path))) # "<H" 表示小端字节序, 2字节(utf-8编码字符串会占用更多字节), 记录路径长度
        buf.extend(path) # 变长路径
        buf.extend(struct.pack("<B", flags)) # "<B" 表示小端字节序, 1字节, 记录有效载荷是否压缩
        buf.extend(struct.pack("<Q", len(raw))) # "<Q" 表示小端字节序, 8字节, 记录原数据大小
        buf.extend(struct.pack("<Q", len(comp))) # "<Q" 表示小端字节序, 8字节, 记录压缩后大小
        buf.extend(comp)
    return bytes(buf)


def unpack_folder_entries(packed_data: bytes, output_dir: Path) -> int:
    lz77 = load_lz77()
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


def get_folder_container(
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
    header.extend(struct.pack("<B", FOLDER_TYPE))
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
    check_dirs()
    source_path = Path(src_path).expanduser().resolve()
    if not source_path.exists():
        raise FileNotFoundError(f"Resource not found: {source_path}")

    stage_name = f"{timestamp()}_{source_path.name}"
    staged_path = STAGING_DIR / stage_name
    if source_path.is_file():
        """
        保留设计,shutil可以做成自己设计的数据结构
        """
        shutil.copy2(source_path, staged_path)
        resource_type = "file"
    else:
        shutil.copytree(source_path, staged_path)
        resource_type = "folder"

    file_count, size_bytes = _count_files_and_size(staged_path)
    return ResourceInfo(
        source_path=str(source_path),
        staged_path=str(staged_path),
        name=source_path.name,
        resource_type=resource_type,
        size_bytes=size_bytes,
        file_count=file_count,
        metadata={"policy": "staged_copy"},
    )


def compress_resource(
    target_path: str, search_size: int = 255, lookahead_size: int = 255
) -> OperationResult:
    check_dirs()
    lz77 = load_lz77()
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
        archive = get_file_container(
            source_name, raw_data, payload, payload_compressed, search_size, lookahead_size
        )
        detail_flag = payload_compressed
    else:
        kind = "folder"
        archive_stream = pack_folder_entries(target)
        compressed_stream = lz77.compress(archive_stream)
        archive_compressed = len(compressed_stream) < len(archive_stream)
        payload = compressed_stream if archive_compressed else archive_stream
        archive = get_folder_container(
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
    check_dirs()
    lz77 = load_lz77()
    archive = Path(archive_path).expanduser().resolve()
    if not archive.exists():
        raise FileNotFoundError(f"Archive not found: {archive}")

    blob = archive.read_bytes()
    header, payload = parse_file_container(blob)
    kind = int(header["kind"])
    flags = int(header["flags"])
    source_name = str(header["source_name"])

    start = time.time()
    if kind == FILE_TYPE:
        raw_data = lz77.decompress(payload) if (flags & FLAG_PAYLOAD_COMPRESSED) else payload
        output_path = DECOMPRESSED_DIR / source_name
        output_path.write_bytes(raw_data)
        message = "File restored."
    elif kind == FOLDER_TYPE:
        stream = lz77.decompress(payload) if (flags & FLAG_ARCHIVE_COMPRESSED) else payload
        output_path = DECOMPRESSED_DIR / f"{source_name}_restored"
        file_count = unpack_folder_entries(stream, output_path)
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
            "kind": "folder" if kind == FOLDER_TYPE else "file",
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
    lz77 = load_lz77()
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
