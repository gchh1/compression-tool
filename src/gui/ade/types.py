"""ADE decision datatypes shared by engine and parameter regression."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from enum import IntEnum

from gui.models import AlgorithmType


class DecisionMode(IntEnum):
    RULE_BASED = 0
    ML_HYBRID = 1
    ML_ONLY = 2


@dataclass
class TrainingSample:
    record_id: str = ""
    filepath: str = ""
    file_size: int = 0
    features: dict[str, float] = field(default_factory=dict)
    algorithm_used: str = ""
    params_used: dict[str, int] = field(default_factory=dict)
    compression_ratio: float = 1.0
    compression_time_ms: float = 0.0
    output_size_bytes: int = 0
    success: bool = True
    sample_weight: float = 1.0
    app_version: str = "3.0"
    timestamp: float = field(default_factory=time.time)


@dataclass
class DecisionResult:
    algorithm: AlgorithmType
    confidence: float
    reason: str
    params: dict[str, int] | None = None
    extraction_time_ms: float = 0.0
    file_type: str = ""
    shannon_entropy: float = 0.0
    mode_used: str = ""


@dataclass
class ParamRegressionSample:
    sample_id: str = ""
    features: dict[str, float] = field(default_factory=dict)
    algorithm_id: int = 0
    predicted_params: dict[str, int] = field(default_factory=dict)
    actual_params: dict[str, int] = field(default_factory=dict)
    compression_ratio: float = 1.0
    compression_time_ms: float = 0.0
    param_error: float = 0.0
    timestamp: float = field(default_factory=time.time)
