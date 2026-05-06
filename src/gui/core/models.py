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
    BINARY = "binary"
    COMPRESSED = "compressed"
    IMAGE = "image"
    SCRIPT = "script"
    TEXT = "text"
    UNKNOWN = "unknown"



class CompressionStatus(Enum):
    PENDING = "pending" # 等待压缩
    COMPRESSING = "compressing"
    DONE = "done"
    FAILED = "failed"


class AlgorithmType(Enum):
    AUTO = "auto"
    DEFLATE = "deflate"
    NONE = "none"





SCRIPT_EXTENSIONS = frozenset({
    ".html", ".htm", ".css", ".js", ".json",
    ".xml",".svg",
})

TEXT_EXTENSIONS = frozenset({
      ".txt", ".md", ".yaml", ".yml",
})

IMAGE_EXTENSIONS = frozenset({
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp",
})

BINARY_EXTENSIONS = frozenset({
    ".bin", ".dat", ".exe", ".dll", ".so", ".a", ".o",
})

COMPRESSED = frozenset({
    ".gz", ".zip", ".rar", ".7z",
})


def _filetype(ext: str) -> ResourceType:
    if ext in TEXT_EXTENSIONS:
        return ResourceType.TEXT
    if ext in IMAGE_EXTENSIONS:
        return ResourceType.IMAGE
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
        self.block_profile: dict | None = None
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


class ArchiveEntry:
    """Represents a file entry inside a .compressed archive for browsing."""

    def __init__(self, name: str, size: int):
        self.name = name
        self.size = size
        self.is_directory = name.endswith("/")
