from __future__ import annotations

import enum
from dataclasses import dataclass, field


class DecisionMode(enum.Enum):
    RULE_BASED = "rule_based"
    ML_HYBRID = "ml_hybrid"
    ML_ONLY = "ml_only"


@dataclass
class DecisionResult:
    algorithm: object | None = None
    confidence: float = 0.0
    reason: str = ""
    params: dict[str, int] | None = None
    extraction_time_ms: float = 0.0
    mode_used: str = ""
    latency_ms: float = 0.0
    strategy_id: str = ""
    is_model_decision: bool = False


@dataclass
class ParamRegressionSample:
    sample_id: str = ""
    algorithm_id: int = 0
    actual_params: dict[str, int] = field(default_factory=dict)
    predicted_params: dict[str, int] | None = None
    compression_ratio: float = 1.0
    compression_time_ms: float = 0.0
    features: dict[str, float] = field(default_factory=dict)
    file_size: int = 0


@dataclass
class TrainingSample:
    record_id: str = ""
    filepath: str = ""
    file_size: int = 0
    algorithm_used: str = ""
    compression_ratio: float = 1.0
    compression_time_ms: float = 0.0
    output_size_bytes: int = 0
    success: bool = False
    sample_weight: float = 1.0
    features: dict = field(default_factory=dict)
    features_vector: list[float] | None = None