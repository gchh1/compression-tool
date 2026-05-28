"""
ADE Training Data Collection Framework
Version: 3.0 (Base Segment 20-dim features)

Provides:
- TrainingSampleV3: Upgraded sample with 20-dim BaseFeatures
- TrainingDataStore: JSON persistence with incremental save, versioning, quality checks
- Automatic collection hooks for compression pipeline integration

Storage format: JSON Lines (one sample per line) for efficient append
"""

from __future__ import annotations

import json
import logging
import time
import uuid
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

from gui.models import AlgorithmType, CompressionStatus, FileRecord, compressed_payload_size
from gui.ade.features import BaseFeatures, FileType, get_compression_decision

logger = logging.getLogger(__name__)

DATA_VERSION = "3.0"
FEATURE_DIM = 20

V3_ALGORITHM_ID_TO_TYPE: dict[int, AlgorithmType] = {
    0: AlgorithmType.NONE,
    1: AlgorithmType.DEFLATE,
    2: AlgorithmType.LZSS,
    3: AlgorithmType.LZDP,
    4: AlgorithmType.DPFLATE,
    5: AlgorithmType.BROTLI,
    6: AlgorithmType.ZSTD,
    7: AlgorithmType.GZIP,
    8: AlgorithmType.HUFFMAN,
    9: AlgorithmType.TRANSFORMER,
    10: AlgorithmType.JPEG,
    11: AlgorithmType.PNG,
    12: AlgorithmType.FLAC,
}


def v3_algorithm_id_to_param_slot(v3_id: int) -> int | None:
    """Map JSONL ``algorithm_id`` to param-regressor one-hot slot (0..5)."""
    if 1 <= v3_id <= 6:
        return v3_id - 1
    return None


def algorithm_type_from_v3(sample: "TrainingSampleV3") -> AlgorithmType | None:
    """Resolve algorithm arm label (required for per-algorithm param learning)."""
    if sample.algorithm_id in V3_ALGORITHM_ID_TO_TYPE:
        algo = V3_ALGORITHM_ID_TO_TYPE[sample.algorithm_id]
        if algo not in (AlgorithmType.NONE, AlgorithmType.AUTO, AlgorithmType.TRANSFORMER):
            return algo
    if sample.algorithm_used:
        try:
            algo = AlgorithmType(sample.algorithm_used)
            if algo not in (AlgorithmType.NONE, AlgorithmType.AUTO, AlgorithmType.TRANSFORMER):
                return algo
        except ValueError:
            pass
    return None
FEATURE_NAMES = [
    'file_size_log2', 'magic_confidence', 'printable_ratio',
    'shannon_entropy', 'min_entropy', 'unique_byte_ratio',
    'mean_byte_normalized', 'std_byte_normalized', 'longest_run_log2',
    'zero_byte_ratio', 'high_bit_ratio', 'header_entropy',
    'local_entropy_variance', 'block_boundary_density', 'skewness',
    'kurtosis', 'unique_bigram_ratio', 'bigram_topk_concentration',
    'rle_potential', 'dict_potential'
]


@dataclass
class TrainingSampleV3:
    """
    Training sample with 20-dim BaseFeatures vector.

  Label semantics (RF + NN + EA stack):
    - **Context**: ``features_vector`` (20-dim BaseFeatures), file metadata.
    - **Algorithm arm** (required for param opt): ``algorithm_used`` / ``algorithm_id``.
    - **Algorithm-specific knobs**: ``params_used`` (schema depends on ``algorithm_used``).
    - **Reward / quality**: ``compression_ratio`` (primary), ``compression_time_ms``,
      ``output_size_bytes`` (codec payload, not full WCX).

  ADE does **not** store streaming I/O flags or chunk sizes; those live in engine settings only.
    """
    sample_id: str = ""
    timestamp: float = 0.0
    data_version: str = DATA_VERSION
    
    file_path: str = ""
    file_name: str = ""
    file_size: int = 0
    file_extension: str = ""
    detected_type: str = ""
    resource_type: str = ""
    
    features_vector: list[float] = field(default_factory=lambda: [0.0] * FEATURE_DIM)
    features_dict: dict[str, float] = field(default_factory=dict)
    
    decision_hint: str = ""
    algorithm_used: str = ""
    algorithm_id: int = 0
    params_used: dict[str, int] = field(default_factory=dict)
    
    compression_ratio: float = 1.0
    compression_time_ms: float = 0.0
    output_size_bytes: int = 0
    success: bool = True
    
    sample_weight: float = 1.0
    is_valid: bool = True
    validation_message: str = ""
    
    is_exploration: bool = False
    exploration_type: str = ""
    exploration_target: str = ""
    parent_decision: str = ""
    ucb_gap_at_time: float = 0.0
    cluster_id: int = -1

    def __post_init__(self):
        if not self.sample_id:
            self.sample_id = uuid.uuid4().hex[:12]
        if self.timestamp == 0.0:
            self.timestamp = time.time()

    def validate(self) -> bool:
        """Validate sample quality and consistency"""
        errors = []
        
        if len(self.features_vector) != FEATURE_DIM:
            errors.append(f"features_vector has {len(self.features_vector)} dims, expected {FEATURE_DIM}")
        
        if self.file_size <= 0:
            errors.append("file_size must be positive")
        
        if self.compression_ratio <= 0:
            errors.append("compression_ratio must be positive")
        
        if not self.algorithm_used:
            errors.append("algorithm_used is empty")
        
        if self.compression_ratio < 0.01:
            errors.append("compression_ratio suspiciously low")
        
        for i, v in enumerate(self.features_vector):
            if not isinstance(v, (int, float)):
                errors.append(f"features_vector[{i}] is not numeric: {type(v)}")
                break
        
        self.is_valid = len(errors) == 0
        self.validation_message = "; ".join(errors) if errors else "OK"
        return self.is_valid

    def to_json_line(self) -> str:
        """Serialize to single JSON line for JSONL format"""
        return json.dumps(asdict(self), ensure_ascii=False)

    @classmethod
    def from_json_line(cls, line: str) -> Optional["TrainingSampleV3"]:
        """Deserialize from JSON line"""
        try:
            data = json.loads(line.strip())
            return cls(**data)
        except Exception as e:
            logger.warning("[TrainingSampleV3] failed to parse line: %s", e)
            return None

    @classmethod
    def from_file_record(
        cls,
        record: FileRecord,
        decision_result=None,
    ) -> "TrainingSampleV3":
        """
        Create a training sample from a compressed FileRecord
        
        Args:
            record: FileRecord with compression results
            decision_result: Optional DecisionResult from ADE
            
        Returns:
            TrainingSampleV3 populated with all available data
        """
        sample = cls()
        sample.file_path = getattr(record, 'path', '')
        sample.file_name = getattr(record, 'name', '')
        sample.file_size = getattr(record, 'size', 0)
        sample.file_extension = getattr(record, 'extension', '')
        sample.resource_type = getattr(record, 'type', '').name if hasattr(getattr(record, 'type', ''), 'name') else str(getattr(record, 'type', ''))
        
        # Extract 20-dim features from base_features
        base_features = getattr(record, 'base_features', None)
        if base_features is not None and isinstance(base_features, BaseFeatures):
            sample.features_vector = base_features.vector
            sample.features_dict = base_features.to_dict()
            sample.decision_hint = get_compression_decision(base_features)
        else:
            sample.features_vector = [0.0] * FEATURE_DIM
            sample.features_dict = {}
            sample.decision_hint = "UNKNOWN"
        
        # Detected file type
        detected_type = getattr(record, 'detected_file_type', None)
        if detected_type is not None and isinstance(detected_type, FileType):
            sample.detected_type = detected_type.name
        else:
            sample.detected_type = "UNKNOWN"
        
        # Algorithm info
        algo = getattr(record, 'algorithm', AlgorithmType.AUTO)
        sample.algorithm_used = algo.value if isinstance(algo, AlgorithmType) else str(algo)
        
        algo_id_map = {
            AlgorithmType.NONE: 0,
            AlgorithmType.DEFLATE: 1,
            AlgorithmType.LZSS: 2,
            AlgorithmType.LZDP: 3,
            AlgorithmType.DPFLATE: 4,
            AlgorithmType.BROTLI: 5,
            AlgorithmType.ZSTD: 6,
            AlgorithmType.GZIP: 7,
            AlgorithmType.HUFFMAN: 8,
            AlgorithmType.TRANSFORMER: 9,
            AlgorithmType.JPEG: 10,
            AlgorithmType.PNG: 11,
            AlgorithmType.FLAC: 12,
            AlgorithmType.AUTO: -1,
        }
        sample.algorithm_id = algo_id_map.get(algo, -1)
        
        # Decision result info
        if decision_result is not None:
            from gui.ade.types import DecisionResult
            if isinstance(decision_result, DecisionResult):
                sample.params_used = decision_result.params or {}
                if not sample.algorithm_used or sample.algorithm_used == 'auto':
                    sample.algorithm_used = decision_result.algorithm.value
                    sample.algorithm_id = algo_id_map.get(decision_result.algorithm, -1)
        
        # Compression metrics
        sample.compression_ratio = getattr(record, 'compression_ratio', 1.0)
        sample.compression_time_ms = getattr(record, 'compression_time_ms', 0.0)
        sample.output_size_bytes = compressed_payload_size(record)
        sample.success = getattr(record, 'status', CompressionStatus.FAILED) == CompressionStatus.DONE
        
        # Weight: penalize failed samples
        sample.sample_weight = 1.0 if sample.success else 0.3
        
        # Validate
        sample.validate()
        
        return sample


class TrainingDataStore:
    """
    JSONL-based persistent storage for ADE training data
    
    Features:
    - Incremental append (no full rewrite)
    - Version management (data_version field)
    - Quality filtering on load
    - Statistics and export utilities
    - Thread-safe file operations
    
    Storage format: JSON Lines (one JSON object per line)
    File location: <base_dir>/ade_training_v3.jsonl
    """
    
    def __init__(self, base_dir: str | Path | None = None):
        if base_dir is None:
            import sys
            if getattr(sys, 'frozen', False):
                base_dir = Path(sys.executable).parent.parent / "ade"
            else:
                base_dir = Path(__file__).resolve().parent.parent.parent.parent / "ade"
        
        self._base_dir = Path(base_dir)
        self._data_file = self._base_dir / "ade_training_v3.jsonl"
        self._stats_file = self._base_dir / "ade_stats_v3.json"
        self._samples: list[TrainingSampleV3] = []
        self._dirty: bool = False
        self._stats: dict[str, Any] = {
            'total_samples': 0,
            'valid_samples': 0,
            'invalid_samples': 0,
            'last_save_time': 0.0,
            'last_retrain_total_samples': 0,
            'last_retrain_time': 0.0,
            'data_version': DATA_VERSION,
            'algorithm_distribution': {},
            'type_distribution': {},
        }
        
        self._ensure_dir()
        self._load_stats()
    
    def _ensure_dir(self) -> None:
        """Ensure base directory exists"""
        self._base_dir.mkdir(parents=True, exist_ok=True)
    
    def _load_stats(self) -> None:
        """Load statistics from stats file"""
        if self._stats_file.exists():
            try:
                raw = json.loads(self._stats_file.read_text(encoding='utf-8'))
                self._stats.update(raw)
                logger.debug("[TrainingDataStore] loaded stats: %d total samples",
                           self._stats.get('total_samples', 0))
            except Exception as e:
                logger.warning("[TrainingDataStore] failed to load stats: %s", e)
    
    def _save_stats(self) -> None:
        """Save statistics to stats file"""
        try:
            self._stats['last_save_time'] = time.time()
            self._stats_file.write_text(
                json.dumps(self._stats, indent=2, ensure_ascii=False),
                encoding='utf-8'
            )
        except Exception as e:
            logger.warning("[TrainingDataStore] failed to save stats: %s", e)
    
    @property
    def data_path(self) -> Path:
        return self._data_file
    
    @property
    def sample_count(self) -> int:
        return len(self._samples)
    
    @property
    def total_persisted(self) -> int:
        return self._stats.get('total_samples', 0)
    
    def add_sample(self, sample: TrainingSampleV3) -> bool:
        """
        Add a training sample to the store
        
        Args:
            sample: TrainingSampleV3 to add
            
        Returns:
            True if sample was added (valid), False if rejected
        """
        if not sample.is_valid:
            sample.validate()
        
        if not sample.is_valid:
            logger.warning("[TrainingDataStore] rejected invalid sample %s: %s",
                         sample.sample_id, sample.validation_message)
            self._stats['invalid_samples'] = self._stats.get('invalid_samples', 0) + 1
            self._save_stats()
            return False
        
        self._samples.append(sample)
        self._dirty = True
        
        # Update stats
        self._stats['total_samples'] = self._stats.get('total_samples', 0) + 1
        self._stats['valid_samples'] = self._stats.get('valid_samples', 0) + 1
        
        algo = sample.algorithm_used
        algo_dist = self._stats.get('algorithm_distribution', {})
        algo_dist[algo] = algo_dist.get(algo, 0) + 1
        self._stats['algorithm_distribution'] = algo_dist
        
        ftype = sample.detected_type
        type_dist = self._stats.get('type_distribution', {})
        type_dist[ftype] = type_dist.get(ftype, 0) + 1
        self._stats['type_distribution'] = type_dist
        
        return True
    
    def add_from_record(
        self,
        record: FileRecord,
        decision_result=None,
    ) -> Optional[TrainingSampleV3]:
        """
        Create and add a training sample from a FileRecord

        Args:
            record: Compressed FileRecord
            decision_result: Optional ADE DecisionResult

        Returns:
            TrainingSampleV3 if added, None if rejected
        """
        from gui.models import is_media_algorithm

        algorithm = getattr(record, 'algorithm', None)
        if algorithm is not None and is_media_algorithm(algorithm):
            logger.debug("[TrainingDataStore] skipping media algo: %s", algorithm.value)
            return None

        sample = TrainingSampleV3.from_file_record(record, decision_result)
        if self.add_sample(sample):
            return sample
        return None
    
    def save(self, incremental: bool = True) -> bool:
        """
        Save training data to disk
        
        Args:
            incremental: If True, append new samples only. If False, rewrite entire file.
            
        Returns:
            True if save succeeded
        """
        if not self._dirty and incremental:
            return True
        
        try:
            if incremental and self._data_file.exists():
                with open(self._data_file, 'a', encoding='utf-8') as f:
                    for sample in self._samples:
                        f.write(sample.to_json_line() + '\n')
            else:
                with open(self._data_file, 'w', encoding='utf-8') as f:
                    for sample in self._samples:
                        f.write(sample.to_json_line() + '\n')
            
            self._dirty = False
            self._save_stats()
            
            logger.info("[TrainingDataStore] saved %d samples to %s",
                       len(self._samples), self._data_file)
            self._samples.clear()
            
            return True
            
        except Exception as e:
            logger.error("[TrainingDataStore] failed to save: %s", e)
            return False
    
    def load(self, validate: bool = True) -> list[TrainingSampleV3]:
        """
        Load all training data from disk
        
        Args:
            validate: If True, validate each sample and filter invalid ones
            
        Returns:
            List of valid TrainingSampleV3 samples
        """
        if not self._data_file.exists():
            logger.info("[TrainingDataStore] no data file at %s", self._data_file)
            return []
        
        samples = []
        invalid_count = 0
        line_count = 0
        
        try:
            with open(self._data_file, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    line_count += 1
                    
                    sample = TrainingSampleV3.from_json_line(line)
                    if sample is None:
                        invalid_count += 1
                        continue
                    
                    if validate and not sample.validate():
                        invalid_count += 1
                        continue
                    
                    samples.append(sample)
            
            logger.info("[TrainingDataStore] loaded %d/%d samples from %s (%d invalid)",
                       len(samples), line_count, self._data_file, invalid_count)
            
            return samples
            
        except Exception as e:
            logger.error("[TrainingDataStore] failed to load: %s", e)
            return []
    
    def get_feature_matrix(self, samples: list[TrainingSampleV3] | None = None) -> list[list[float]]:
        """
        Extract feature matrix for ML training
        
        Args:
            samples: Optional subset of samples. If None, loads from disk.
            
        Returns:
            List of feature vectors (each is 20 floats)
        """
        if samples is None:
            samples = self.load(validate=True)
        
        return [s.features_vector for s in samples if s.is_valid and len(s.features_vector) == FEATURE_DIM]
    
    def get_label_vector(self, samples: list[TrainingSampleV3] | None = None) -> list[int]:
        """
        Extract algorithm arm IDs for RF / classifier training.
        """
        if samples is None:
            samples = self.load(validate=True)

        return [s.algorithm_id for s in samples if s.is_valid and s.algorithm_id >= 0]

    def get_ratio_vector(self, samples: list[TrainingSampleV3] | None = None) -> list[float]:
        """Compression ratio rewards (lower is better) aligned with ``get_label_vector`` order."""
        if samples is None:
            samples = self.load(validate=True)
        return [
            float(s.compression_ratio)
            for s in samples
            if s.is_valid and s.algorithm_id >= 0
        ]

    def get_param_regression_rows(
        self, samples: list[TrainingSampleV3] | None = None
    ) -> list[TrainingSampleV3]:
        """Rows suitable for NN param regression: arm label + params + ratio."""
        if samples is None:
            samples = self.load(validate=True)
        return [
            s for s in samples
            if s.is_valid and s.algorithm_id >= 0 and bool(s.params_used)
        ]
    
    def get_weight_vector(self, samples: list[TrainingSampleV3] | None = None) -> list[float]:
        """
        Extract sample weights for ML training
        
        Args:
            samples: Optional subset of samples
            
        Returns:
            List of sample weights
        """
        if samples is None:
            samples = self.load(validate=True)
        
        return [s.sample_weight for s in samples if s.is_valid]
    
    def count_persisted_exploration_samples(self) -> int:
        """Count JSONL rows with ``is_exploration=true`` (full file scan)."""
        if not self._data_file.is_file():
            return 0
        n = 0
        try:
            with open(self._data_file, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        row = json.loads(line)
                        if row.get("is_exploration"):
                            n += 1
                    except json.JSONDecodeError:
                        continue
        except OSError as e:
            logger.warning("[TrainingDataStore] count exploration failed: %s", e)
        return n

    def get_stats(self) -> dict[str, Any]:
        """Get current statistics"""
        out = dict(self._stats)
        out["exploration_samples"] = self.count_persisted_exploration_samples()
        return out
    
    def export_csv(self, output_path: str | Path) -> int:
        """
        Export training data as CSV for analysis
        
        Args:
            output_path: Path to output CSV file
            
        Returns:
            Number of rows exported
        """
        import csv
        
        samples = self.load(validate=True)
        if not samples:
            return 0
        
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        headers = [
            'sample_id', 'file_name', 'file_size', 'file_extension',
            'detected_type', 'resource_type', 'decision_hint',
        ] + FEATURE_NAMES + [
            'algorithm_used', 'algorithm_id',
            'compression_ratio', 'compression_time_ms',
            'success', 'sample_weight',
        ]
        
        with open(output_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=headers)
            writer.writeheader()
            
            for sample in samples:
                row = {
                    'sample_id': sample.sample_id,
                    'file_name': sample.file_name,
                    'file_size': sample.file_size,
                    'file_extension': sample.file_extension,
                    'detected_type': sample.detected_type,
                    'resource_type': sample.resource_type,
                    'decision_hint': sample.decision_hint,
                    'algorithm_used': sample.algorithm_used,
                    'algorithm_id': sample.algorithm_id,
                    'compression_ratio': sample.compression_ratio,
                    'compression_time_ms': sample.compression_time_ms,
                    'success': sample.success,
                    'sample_weight': sample.sample_weight,
                }
                row.update(sample.features_dict)
                writer.writerow(row)
        
        logger.info("[TrainingDataStore] exported %d samples to %s", len(samples), output_path)
        return len(samples)
    
    def clear(self) -> None:
        """Clear all in-memory samples (does not delete file)"""
        self._samples.clear()
        self._dirty = False
    
    def reset(self) -> bool:
        """Delete all persisted training data"""
        try:
            if self._data_file.exists():
                self._data_file.unlink()
            if self._stats_file.exists():
                self._stats_file.unlink()
            self._samples.clear()
            self._dirty = False
            self._stats = {
                'total_samples': 0,
                'valid_samples': 0,
                'invalid_samples': 0,
                'last_save_time': 0.0,
                'data_version': DATA_VERSION,
                'algorithm_distribution': {},
                'type_distribution': {},
            }
            logger.info("[TrainingDataStore] all data reset")
            return True
        except Exception as e:
            logger.error("[TrainingDataStore] failed to reset: %s", e)
            return False


_store_instance: TrainingDataStore | None = None


def get_training_store() -> TrainingDataStore:
    """Get singleton TrainingDataStore instance"""
    global _store_instance
    if _store_instance is None:
        _store_instance = TrainingDataStore()
    return _store_instance
