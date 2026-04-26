from __future__ import annotations

import math
from collections import Counter
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path


class ResourceType(Enum):
    TEXT = "text"
    IMAGE = "image"
    BINARY = "binary"
    UNKNOWN = "unknown"


class CompressionStatus(Enum):
    PENDING = "pending"
    COMPRESSING = "compressing"
    DONE = "done"
    FAILED = "failed"


class AlgorithmType(Enum):
    DEFLATE = "deflate"
    LZSS = "lzss"
    HUFFMAN = "huffman"


TEXT_EXTENSIONS = frozenset({
    ".html", ".htm", ".css", ".js", ".json",
    ".xml", ".svg", ".txt", ".md", ".yaml", ".yml",
})

IMAGE_EXTENSIONS = frozenset({
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp",
})


def _identify_type(ext: str) -> ResourceType:
    if ext in TEXT_EXTENSIONS:
        return ResourceType.TEXT
    if ext in IMAGE_EXTENSIONS:
        return ResourceType.IMAGE
    if ext: # debug 请检查这里
        return ResourceType.BINARY
    return ResourceType.UNKNOWN


@dataclass
class FileRecord:
    # 文件基本信息
    path: str
    name: str
    extension: str
    size: int
    type: ResourceType

    raw_data: bytes = b""
    compressed_data: bytes | None = None

    compression_ratio: float = 0.0
    compression_time_ms: float = 0.0
    status: CompressionStatus = CompressionStatus.PENDING
    algorithm: AlgorithmType = AlgorithmType.DEFLATE
    error_message: str = ""
    # 文件特征，用于决策引擎的决策
    content_entropy: float = 0.0 # 内容熵，用于判断文件是否为文本文件
    repetition_ratio: float = 0.0 # 重复率，用于判断文件是否为文本文件

    @classmethod
    def from_path(cls, filepath: str | Path) -> FileRecord:
        p = Path(filepath)
        ext = p.suffix.lower()
        return cls(
            path=str(p),
            name=p.name,
            extension=ext,
            size=p.stat().st_size if p.exists() else 0,
            type=_identify_type(ext),
        )

    def extract_features(self) -> None:
        data = self.raw_data
        size = len(data)

        if size == 0:
            self.content_entropy = 0.0
            self.repetition_ratio = 0.0
            return

        byte_counts = Counter(data)
        entropy = 0.0
        for count in byte_counts.values():
            prob = count / size
            if prob > 0:
                entropy -= prob * math.log2(prob)

        most_common_count = byte_counts.most_common(1)[0][1]

        self.content_entropy = entropy
        self.repetition_ratio = most_common_count / size


@dataclass
class CompressionResult:
    original_size: int
    compressed_size: int
    compression_ratio: float
    time_ms: float
    data: bytes = b""


@dataclass
class BatchResult:# 汇总整个文件夹批量压缩的结果
    files: list[FileRecord] = field(default_factory=list) # 所有文件的压缩结果
    total_original: int = 0 # 所有文件的原始大小总和
    total_compressed: int = 0 # 所有文件的压缩大小总和
    total_time_ms: float = 0.0 # 所有文件的压缩时间总和（毫秒）
    errors: list[str] = field(default_factory=list) # 所有文件的压缩错误信息

    @property
    def overall_ratio(self) -> float:
        if self.total_original == 0:
            return 0.0
        return (1 - self.total_compressed / self.total_original) * 100


@dataclass
class NetworkProfile:
    name: str
    bandwidth_bps: int
    latency_ms: float

    def transfer_time(self, size_bytes: int) -> float:
        return (size_bytes * 8) / self.bandwidth_bps + self.latency_ms / 1000


NETWORK_PROFILES: dict[str, NetworkProfile] = {
    "4G": NetworkProfile("4G", 20_000_000, 50),
    "5G": NetworkProfile("5G", 100_000_000, 10),
    "WiFi": NetworkProfile("WiFi", 50_000_000, 20),
    "Ethernet": NetworkProfile("Ethernet", 1_000_000_000, 1),
}
