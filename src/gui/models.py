from __future__ import annotations

import logging
import math
from collections import Counter
from enum import Enum
from pathlib import Path
from typing import TYPE_CHECKING

logger = logging.getLogger(__name__)

if TYPE_CHECKING:
    from gui.ade.types import DecisionResult


def formatted_size(size_bytes: int) -> str:
    if size_bytes < 1024:
        return f"{size_bytes} B"
    if size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.1f} KB"
    if size_bytes < 1024 * 1024 * 1024:
        return f"{size_bytes / (1024 * 1024):.2f} MB"
    return f"{size_bytes / (1024 * 1024 * 1024):.2f} GB"


def compressed_payload_size(record: object) -> int:
    """Bytes of the stored compressed artifact (memory or ``compressed_path`` on disk)."""
    data = getattr(record, "compressed_data", None)
    if data is not None:
        return len(data)
    path = getattr(record, "compressed_path", None)
    if path:
        try:
            p = Path(path)
            if p.is_file():
                return int(p.stat().st_size)
        except OSError:
            return 0
    return 0


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
    SKIPPED = "skipped"


class AlgorithmType(Enum):
    AUTO = "auto"
    DEFLATE = "deflate"
    HUFFMAN = "huffman"
    LZSS = "lzss"
    LZDP = "lzdp"
    DPFLATE = "dpflate"
    GZIP = "gzip"
    BROTLI = "brotli"
    ZSTD = "zstd"
    TRANSFORMER = "transformer (beta)"
    NONE = "none"


class AlgorithmParamDef:
    def __init__(self, key: str, label: str, default, min_val=0, max_val=0, step=1, suffix: str = "", choices: dict[int, str] | None = None):
        self.key = key
        self.label = label
        self.default = default
        self.min_val = min_val
        self.max_val = max_val
        self.step = step
        self.suffix = suffix
        self.choices = choices


ALGORITHM_PARAMS: dict[AlgorithmType, list[AlgorithmParamDef]] = {
    AlgorithmType.LZSS: [
        # search_size 上限受编码中距离表示约束（与实现 uint16 语义一致）
        AlgorithmParamDef("search_size", "搜索窗口大小", 4095, 255, 65535, 256, ""),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 18, 4, 255, 1, ""),
        AlgorithmParamDef("min_match", "最小匹配长度", 0, 0, 127, 1, ""),
        AlgorithmParamDef("use_flag_encoding", "编码方案", 1, choices={0: "Offset=0 兜底模式 (长纯文本占优)", 1: "1-Bit Flag 模式 (碎片化文件占优)"}),
    ],
    AlgorithmType.LZDP: [
        # 流式 OOC 链路与比特头：offset/length 位宽 ≤24，packed 距离 uint16 → 窗口上界 65535
        AlgorithmParamDef("search_size", "搜索窗口大小", 4096, 256, 65535, 256, " B"),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 256, 2, 65535, 1, " B"),
        AlgorithmParamDef("min_match", "最小匹配长度", 0, 0, 127, 1, ""),
        AlgorithmParamDef("dp_top", "DP 每步保留的匹配候选数", 3, 1, 64, 1, ""),
        AlgorithmParamDef("match_engine", "匹配引擎选择", 0, choices={0: "KMP 引擎 (支持重叠匹配, 慢)", 1: "HashChain 引擎 (支持重叠匹配, 快)"}),
        AlgorithmParamDef("use_flag_encoding", "编码方案", 0, choices={0: "Offset=0 兜底模式 (长纯文本占优)", 1: "1-Bit Flag 模式 (碎片化文件占优)"}),
    ],
    AlgorithmType.DPFLATE: [
        # FLATE 距离码最大距离 32768（与 Inflate 兼容）；不可再上调
        AlgorithmParamDef("search_size", "搜索窗口大小", 4096, 256, 32768, 256, " B"),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 256, 16, 8192, 16, " B"),
        AlgorithmParamDef("min_match", "最小匹配长度", 0, 0, 127, 1, ""),
        AlgorithmParamDef("dp_sub_match_max", "DP 每步保留的匹配候选数", 6, 1, 64, 1, ""),
        AlgorithmParamDef("max_chain_length", "最大搜索链长", 256, 4, 8192, 4, ""),
        AlgorithmParamDef("match_engine", "匹配引擎选择", 1, choices={0: "KMP 引擎 (支持重叠匹配, 慢)", 1: "HashChain 引擎 (支持重叠匹配, 快)"}),
        AlgorithmParamDef("use_flag_encoding", "编码方案", 1, choices={0: "Offset=0 兜底模式 (长纯文本占优)", 1: "1-Bit Flag 模式 (碎片化文件占优)"}),
        AlgorithmParamDef("use_3hfmtree", "Huffman 树策略", 0, choices={0: "标准 FLATE（两树，兼容 Inflate）", 1: "3HfMTree（三树 + 多级槽）"}),
        AlgorithmParamDef("huffman_offset_chunk_bits", "3HfM offset 槽宽（bit）", 8, 2, 20, 1, ""),
        AlgorithmParamDef("huffman_length_chunk_bits", "3HfM length 槽宽（bit）", 8, 2, 20, 1, ""),
    ],
    AlgorithmType.DEFLATE: [
        # 与 DPFlate 同款范围/步进；经典 Deflate 距离码上限 32768（与 Inflate 兼容）
        AlgorithmParamDef("search_size", "搜索窗口大小", 4096, 256, 32768, 256, " B"),
        AlgorithmParamDef("lookahead_size", "前瞻窗口大小", 256, 16, 8192, 16, " B"),
        AlgorithmParamDef("min_match", "最小匹配长度", 0, 0, 127, 1, " 0 表示使用实现默认（3）"),
        AlgorithmParamDef("max_chain_length", "哈希链搜索深度", 256, 4, 8192, 4, ""),
        AlgorithmParamDef(
            "use_flag_encoding",
            "编码方案",
            1,
            0,
            1,
            1,
            "",
            choices={0: "Offset=0 兜底模式 (长纯文本占优)", 1: "1-Bit Flag 模式 (碎片化文件占优)"},
        ),
        AlgorithmParamDef(
            "use_3hfmtree",
            "Huffman 树策略",
            0,
            0,
            1,
            1,
            "",
            choices={
                0: "标准 FLATE（两树，兼容 Inflate）",
                1: "3HfMTree（整块内存压缩走 DPFlate 内核；分块文件管线仍为经典 Deflate）",
            },
        ),
        AlgorithmParamDef(
            "huffman_offset_chunk_bits",
            "3HfM offset 槽宽（bit）",
            8,
            2,
            20,
            1,
            " 仅 use_3hfmtree=1",
        ),
        AlgorithmParamDef(
            "huffman_length_chunk_bits",
            "3HfM length 槽宽（bit）",
            8,
            2,
            20,
            1,
            " 仅 use_3hfmtree=1",
        ),
    ],
    AlgorithmType.GZIP: [
        AlgorithmParamDef("compression_level", "压缩级别", 6, 1, 9, 1, ""),
    ],
    AlgorithmType.BROTLI: [
        AlgorithmParamDef("window_size", "窗口大小", 65536, 4096, 65536, 4096, " B"),
        AlgorithmParamDef("min_match", "最小匹配长度", 0, 0, 127, 1, ""),
        AlgorithmParamDef("max_chain_length", "最大搜索链长", 256, 4, 8192, 4, ""),
    ],
    AlgorithmType.ZSTD: [
        AlgorithmParamDef("compression_level", "压缩级别", 3, 1, 22, 1, ""),
    ],
}

STREAMING_THRESHOLD_MB = 10
STREAMING_CHUNK_SIZE_KB = 1024
LZDP_DP_VIZ_MAX_SIZE = 131072


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
        self.compressed_path: str | None = None

        self.compression_ratio: float = 1.00
        self.compression_time_ms: float = 0.0
        self.status: CompressionStatus = CompressionStatus.PENDING
        self.algorithm: AlgorithmType = AlgorithmType.DEFLATE if not self.type == ResourceType.COMPRESSED else AlgorithmType.NONE
        self.error_message: str = ""
        self.is_stored: bool = False
        #=====features============
        self.content_entropy: float = 0.0
        self.repetition_ratio: float = 0.0
        #=====ADE Decision (持久化保存)============
        self.decision_result: DecisionResult | None = None
        # 本次成功压缩时使用的算法参数字典（与全局配置解耦，供演示/解析复现）
        self.compression_config_snapshot: dict[str, int] | None = None
        #=====ADE Feature Vector (v3.0 - 20-dim Base Segment)============
        self.base_features = None
        self.detected_file_type = None
    def load_raw_data(self) -> None:
        self.raw_data = (Path(self.path).read_bytes()) if Path(self.path).exists() else b""

    def extract_features(self) -> None:
        """
        Extract ADE Base Segment features (20 dimensions)
        
        Uses single-pass O(n) algorithm with ~130KB working memory.
        Populates:
          - self.base_features: BaseFeatures object (20-dim vector)
          - self.detected_file_type: FileType enum from magic bytes
          - self.content_entropy: Shannon entropy (backward compat)
          - self.repetition_ratio: Unique byte ratio (backward compat)
        """
        if not self.raw_data:
            self.load_raw_data()
            
        if not self.raw_data or len(self.raw_data) == 0:
            self.content_entropy = 0.0
            self.repetition_ratio = 0.0
            return
            
        try:
            from gui.ade.features import extract_base_features_fast
            
            self.base_features, self.detected_file_type = extract_base_features_fast(self.raw_data)
            
            # Backward compatibility: update old fields
            self.content_entropy = self.base_features.shannon_entropy
            self.repetition_ratio = 1.0 - self.base_features.unique_byte_ratio
            
        except Exception as e:
            logger.warning("[FileRecord] feature extraction failed for %s: %s", self.name, e)
            self.base_features = None
            self.detected_file_type = None

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
    def __init__(self, name: str, size: int):
        self.name = name
        self.size = size
        self.is_directory = name.endswith("/")
