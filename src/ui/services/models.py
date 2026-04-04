from dataclasses import dataclass, field
from typing import Dict, List, Optional


@dataclass
class ResourceInfo:
    source_path: str
    staged_path: str # 压缩后的文件路径
    name: str
    resource_type: str
    size_bytes: int
    file_count: int = 1
    metadata: Dict[str, str] = field(default_factory=dict) # 其他元数据，如文件创建时间、修改时间等。


@dataclass
class OperationResult:
    success: bool
    message: str
    input_path: str
    output_path: str
    original_size: int = 0
    output_size: int = 0
    duration_sec: float = 0.0
    ratio: float = 0.0
    metadata: Dict[str, str] = field(default_factory=dict) # 其他元数据，如压缩率、压缩时间等。


@dataclass
class LZ77DemoStep:
    step_index: int
    cursor: int
    search_window: str
    lookahead_window: str
    token_type: str
    token_value: str
    match_length: int = 0
    match_distance: int = 0
    next_symbol: Optional[str] = None
    consumed_length: int = 1
    literal_length: int = 0
    token_bytes_hex: str = ""
    literal_bytes_hex: str = ""


@dataclass
class LZ77Demo:
    source_path: str
    source_text: str
    steps: List[LZ77DemoStep]
