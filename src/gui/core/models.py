from __future__ import annotations

import math
from collections import Counter
from enum import Enum
from pathlib import Path


def formatted_size(size_bytes: int) -> str:
    if size_bytes < 1024:
        return f"{size_bytes} B"
    if size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.1f} KB"
    if size_bytes < 1024 * 1024 * 1024:
        return f"{size_bytes / (1024 * 1024):.2f} MB"
    return f"{size_bytes / (1024 * 1024 * 1024):.2f} GB"


class ResourceType(Enum):
    AUDIO = "audio"
    BINARY = "binary"
    COMPRESSED = "compressed"
    IMAGE = "image"
    SCRIPT = "script"
    TEXT = "text"
    UNKNOWN = "unknown"
    VIDEO = "video"



class CompressionStatus(Enum):
    PENDING = "pending" # 等待压缩
    COMPRESSING = "compressing"
    DONE = "done"
    FAILED = "failed"


class AlgorithmType(Enum):
    AUTO = "auto"
    DEFLATE = "deflate"
    HUFFMAN = "huffman"
    LZSS = "lzss"
    LZMINE = "lzmine"
    LZCRAZY = "lzcrazy"
    CRAZYFLATE = "crazyflate"
    MYFLATE = "myflate"
    GZIP = "gzip"
    TRANSFORMER = "transformer (beta)"
    NONE = "none"


class AlgorithmParamDef:
    def __init__(self, key: str, label: str, default, min_val, max_val, step=1, suffix: str = ""):
        self.key = key
        self.label = label
        self.default = default
        self.min_val = min_val
        self.max_val = max_val
        self.step = step
        self.suffix = suffix


ALGORITHM_PARAMS: dict[AlgorithmType, list[AlgorithmParamDef]] = {
    AlgorithmType.LZSS: [
        AlgorithmParamDef("search_size", "搜索窗口大小", 4095, 255, 65535, 256, ""),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 18, 4, 255, 1, ""),
        AlgorithmParamDef("min_match", "最小匹配长度", 3, 2, 15, 1, ""),
    ],
    AlgorithmType.LZCRAZY: [
        AlgorithmParamDef("search_size", "搜索窗口大小", 4096, 256, 65536, 256, " B"),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 18, 4, 258, 1, ""),
        AlgorithmParamDef("min_match", "最小匹配长度", 3, 1, 6, 1, ""),
    ],
    AlgorithmType.LZMINE: [
        AlgorithmParamDef("search_size", "搜索窗口大小", 4096, 256, 65536, 256, " B"),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 256, 16, 65536, 16, " B"),
        AlgorithmParamDef("min_match", "最小匹配长度", 3, 2, 15, 1, ""),
        AlgorithmParamDef("dp_depth", "DP优化深度", 3, 1, 32, 1, ""),
    ],
    AlgorithmType.CRAZYFLATE: [
        AlgorithmParamDef("search_size", "搜索窗口大小", 4096, 256, 65536, 256, " B"),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 258, 16, 258, 1, ""),
        AlgorithmParamDef("min_match", "最小匹配长度", 3, 1, 6, 1, ""),
        AlgorithmParamDef("dp_depth", "DP优化深度", 6, 1, 32, 1, ""),
        AlgorithmParamDef("max_chain_length", "最大搜索链长", 128, 4, 4096, 4, ""),
    ],
    AlgorithmType.MYFLATE: [
        AlgorithmParamDef("search_size", "搜索窗口大小", 4096, 256, 32768, 256, " B"),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 256, 16, 4096, 16, " B"),
        AlgorithmParamDef("min_match", "最小匹配长度", 4, 2, 6, 1, ""),
        AlgorithmParamDef("dp_depth", "DP优化深度", 6, 1, 32, 1, ""),
        AlgorithmParamDef("max_chain_length", "最大搜索链长", 256, 4, 4096, 4, ""),
    ],
    AlgorithmType.DEFLATE: [
        AlgorithmParamDef("search_size", "搜索窗口大小", 32768, 1024, 65536, 1024, " B"),
        AlgorithmParamDef("min_match", "最小匹配长度", 3, 2, 6, 1, ""),
        AlgorithmParamDef("max_chain_length", "最大搜索链长", 256, 4, 4096, 4, ""),
    ],
    AlgorithmType.GZIP: [
        AlgorithmParamDef("compression_level", "压缩级别", 6, 1, 9, 1, ""),
    ],
}


def get_default_config() -> dict[AlgorithmType, dict[str, int]]:
    config = {}
    for algo, params in ALGORITHM_PARAMS.items():
        config[algo] = {p.key: p.default for p in params}
    return config





SCRIPT_EXTENSIONS = frozenset({
    ".html", ".htm", ".css", ".js", ".json",
    ".xml",".svg",
})

TEXT_EXTENSIONS = frozenset({
      ".txt", ".md", ".yaml", ".yml", ".csv", ".log", ".ini", ".cfg", ".conf", ".toml",
})

IMAGE_EXTENSIONS = frozenset({
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico", ".tiff", ".tif", ".svg",
})

AUDIO_EXTENSIONS = frozenset({
    ".wav", ".mp3", ".ogg", ".flac", ".aac", ".wma", ".m4a", ".opus", ".mid", ".midi",
})

VIDEO_EXTENSIONS = frozenset({
    ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm", ".m4v", ".mpg", ".mpeg",
})

BINARY_EXTENSIONS = frozenset({
    ".bin", ".dat", ".exe", ".dll", ".so", ".a", ".o", ".lib", ".pdb", ".obj",
})

COMPRESSED = frozenset({
    ".gz", ".zip", ".rar", ".7z", ".tar", ".bz2", ".xz", ".zst",
    ".wcx",
})


def _filetype(ext: str) -> ResourceType:
    if ext in TEXT_EXTENSIONS:
        return ResourceType.TEXT
    if ext in IMAGE_EXTENSIONS:
        return ResourceType.IMAGE
    if ext in AUDIO_EXTENSIONS:
        return ResourceType.AUDIO
    if ext in VIDEO_EXTENSIONS:
        return ResourceType.VIDEO
    if ext in SCRIPT_EXTENSIONS:
        return ResourceType.SCRIPT
    if ext in COMPRESSED:
        return ResourceType.COMPRESSED
    if ext in BINARY_EXTENSIONS:
        return ResourceType.BINARY
    return ResourceType.UNKNOWN

class Record:
    def __init__(self, path: str):
        self.path: str = path
        self.name: str = Path(path).name
    def load_raw_data(self) -> None:
        pass
    def extract_features(self) -> None:
        pass

class FileRecord(Record):
    def __init__(self, path: str):
        super().__init__(path)
        self.extension: str = Path(path).suffix.lower() # 保留？
        self.size: int = Path(path).stat().st_size if Path(path).exists() else 0
        self.type: ResourceType = _filetype(self.extension)


        self.raw_data: bytes = b""
        self.compressed_data: bytes | None = None

        self.compression_ratio: float = 1.00
        self.compression_time_ms: float = 0.0
        self.status: CompressionStatus = CompressionStatus.PENDING
        self.algorithm: AlgorithmType = AlgorithmType.DEFLATE if not self.type == ResourceType.COMPRESSED else AlgorithmType.NONE
        self.error_message: str = ""
        self.is_stored: bool = False
        #=====features============
        self.content_entropy: float = 0.0
        self.repetition_ratio: float = 0.0
    def load_raw_data(self) -> None:
        self.raw_data = (Path(self.path).read_bytes()) if Path(self.path).exists() else b""

    def extract_features(self) -> None:
        """
        pass,待修改
        """
        pass
        # data = self.raw_data
        # size = len(data)

        # if size == 0:
        #     self.content_entropy = 0.0
        #     self.repetition_ratio = 0.0
        #     return

        # byte_counts = Counter(data)
        # entropy = 0.0
        # for count in byte_counts.values():
        #     prob = count / size
        #     if prob > 0:
        #         entropy -= prob * math.log2(prob)

        # most_common_count = byte_counts.most_common(1)[0][1]

        # self.content_entropy = entropy
        # self.repetition_ratio = most_common_count / size

def get_folder_size(path: str) -> int:
    p = Path(path)
    if not p.exists():
        return -1
    # 递归累加所有文件大小
    return sum(f.stat().st_size for f in p.rglob('*') if f.is_file())

class FolderRecord(Record):
    """文件夹批量压缩结果汇总"""
    def __init__(self, path:str):
        super().__init__(path)

        self.files: list[FileRecord] = [FileRecord(str(f)) for f in Path(path).rglob('*') if f.is_file()]        
        self.size: int = get_folder_size(path)

        self.status: CompressionStatus = CompressionStatus.PENDING

        self.error_messages: list[str] = []
        self.total_original: int = 0
        self.total_compressed: int = 0
        self.compression_ratio: float = 1.00
        self.total_time_ms: float = 0.0

    def add_file(self, record: FileRecord) -> None:
        self.files.append(record)
        self.total_original += record.size
        if record.compressed_data:
            self.total_compressed += len(record.compressed_data)
        self.total_time_ms += record.compression_time_ms
        if record.error_message:
            self.error_messages.append(f"{record.name}: {record.error_message}")
    def load_raw_data(self) -> None:
        for record in self.files:
            record.load_raw_data()
    def extract_features(self) -> None:
        for record in self.files:
            record.extract_features()
    @property
    def filenum(self) -> int:
        return len(self.files)

    @property
    def success_count(self) -> int:
        return sum(1 for f in self.files if f.status == CompressionStatus.DONE)

    @property
    def error_count(self) -> int:
        return len(self.error_messages)

    def summary(self) -> str:
        return (
            f"文件数: {self.filenum} | "
            f"成功: {self.success_count} | "
            f"失败: {self.error_count} | "
            f"原始大小: {formatted_size(self.total_original)} | "
            f"压缩后: {formatted_size(self.total_compressed)} | "
            f"压缩率: {self.compression_ratio:.1f}%"
        )


class NetworkProfile:
    def __init__(self, name: str, bandwidth_bps: int, latency_ms: float):
        self.name = name
        self.bandwidth_bps = bandwidth_bps
        self.latency_ms = latency_ms

    def transfer_time(self, size_bytes: int) -> float:
        return (size_bytes * 8) / self.bandwidth_bps + self.latency_ms / 1000


NETWORK_PROFILES: dict[str, NetworkProfile] = {
    "4G": NetworkProfile("4G", 20_000_000, 50),
    "5G": NetworkProfile("5G", 100_000_000, 10),
    "WiFi": NetworkProfile("WiFi", 50_000_000, 20),
    "Ethernet": NetworkProfile("Ethernet", 1_000_000_000, 1),
}
