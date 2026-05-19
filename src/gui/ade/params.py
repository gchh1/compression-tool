from __future__ import annotations

import logging
import uuid
from pathlib import Path
from typing import Any

from gui.ade.types import DecisionResult, ParamRegressionSample
from gui.models import ALGORITHM_PARAMS, AlgorithmType, FileRecord

logger = logging.getLogger(__name__)


class ParameterRegressor:
    """Stage2a parameter regression — C++ MLP in ``core_engine`` (no PyTorch)."""

    _instance = None

    PARAM_NAMES = ['window_size', 'min_match', 'max_chain_length', 'lookahead_size', 'dp_range']
    PARAM_RANGES = {
        'window_size': (1024, 524288),
        'min_match': (2, 96),
        'max_chain_length': (4, 1024),
        'lookahead_size': (8, 512),
        'dp_range': (1, 24),
    }
    ALGORITHM_MAP = {
        AlgorithmType.DEFLATE: 0,
        AlgorithmType.LZSS: 1,
        AlgorithmType.LZDP: 2,
        AlgorithmType.DPFLATE: 3,
        AlgorithmType.BROTLI: 4,
        AlgorithmType.ZSTD: 5,
    }

    def __init__(self):
        self._training_data: list[ParamRegressionSample] = []
        self._input_dim = 39
        self._output_dim = 5
        self._hidden_dim = 128
        self._cpp_net = None
        self._has_cpp = False
        self._init_cpp_backend()

    def _init_cpp_backend(self) -> None:
        try:
            from gui.engine.compressor import CompressionEngine

            eng = CompressionEngine()
            if eng.available and hasattr(eng._engine, 'ParamRegressorNet'):
                self._cpp_net = eng._engine.ParamRegressorNet()
                self._has_cpp = True
                logger.info("[param_regressor] C++ ParamRegressorNet available")
            else:
                logger.warning(
                    "[param_regressor] core_engine.ParamRegressorNet missing; "
                    "rebuild core_engine for Stage2a NN"
                )
        except Exception as e:
            logger.warning("[param_regressor] C++ backend init failed: %s", e)

    @classmethod
    def get(cls, *, force_reload: bool = False) -> "ParameterRegressor":
        if force_reload:
            cls._instance = None
        if cls._instance is None:
            cls._instance = cls()
            try:
                cls._instance.load_model()
            except Exception:
                pass
        elif not cls._instance._has_cpp:
            cls._instance._init_cpp_backend()
            if cls._instance._has_cpp:
                try:
                    cls._instance.load_model()
                except Exception:
                    pass
        return cls._instance

    @property
    def is_ready(self) -> bool:
        return bool(self._has_cpp and self._cpp_net is not None and self._cpp_net.is_trained())

    def _param_keys_for_algorithm(self, algorithm: AlgorithmType) -> list[str]:
        defs = ALGORITHM_PARAMS.get(algorithm, [])
        if defs:
            return [p.key for p in defs]
        return [k for k in self.PARAM_NAMES if k in self.PARAM_RANGES]

    def _normalize_params(self, params: dict[str, int], algorithm: AlgorithmType | None = None) -> list[float]:
        normalized = [0.0] * len(self.PARAM_NAMES)
        active_keys = (
            self._param_keys_for_algorithm(algorithm)
            if algorithm is not None
            else list(params.keys())
        )
        for name in self.PARAM_NAMES:
            if name not in active_keys:
                continue
            value = params.get(name, 0)
            if name not in self.PARAM_RANGES:
                continue
            min_val, max_val = self.PARAM_RANGES[name]
            norm_val = (value - min_val) / max(max_val - min_val, 1)
            idx = self.PARAM_NAMES.index(name)
            normalized[idx] = max(0.0, min(1.0, norm_val))
        return normalized

    def _denormalize_params(self, normalized: list[float]) -> dict[str, int]:
        params = {}
        for i, name in enumerate(self.PARAM_NAMES):
            if i < len(normalized):
                min_val, max_val = self.PARAM_RANGES[name]
                value = normalized[i] * (max_val - min_val) + min_val
                params[name] = int(round(max(min_val, min(max_val, value))))
        return params

    def _encode_algorithm(self, algorithm: AlgorithmType) -> list[float]:
        one_hot = [0.0] * 6
        algo_id = self.ALGORITHM_MAP.get(algorithm, 2)
        one_hot[algo_id] = 1.0
        return one_hot

    def _extract_features(self, record: FileRecord, ade_result=None) -> list[float]:
        base_features = getattr(record, 'base_features', None)
        if base_features is not None and hasattr(base_features, 'vector'):
            return self._features_from_v3_vector(list(base_features.vector))

        if ade_result is not None:
            base_features = [
                ade_result.shannon_entropy / 8.0,
                float(ade_result.confidence),
                ade_result.estimated_ratio,
                1.0 if hasattr(ade_result, 'algorithm') and ade_result.algorithm != 0 else 0.0,
            ]
        elif hasattr(record, 'raw_data') and record.raw_data:
            from gui.ade.engine import DecisionEngine
            de = DecisionEngine.get()
            if de._ade is not None:
                try:
                    r = de._ade.analyze(list(record.raw_data))
                    base_features = [
                        r.shannon_entropy / 8.0,
                        float(r.confidence),
                        r.estimated_ratio,
                        1.0 if r.algorithm != 0 else 0.0,
                    ]
                except Exception:
                    base_features = [0.5] * 4
            else:
                base_features = [0.5] * 4
        else:
            base_features = [0.5] * 4

        file_size_norm = getattr(record, 'size', 0)
        if file_size_norm > 0:
            import math
            file_size_norm = math.log2(file_size_norm + 1) / 32.0
        base_features.append(file_size_norm)

        while len(base_features) < 33:
            base_features.append(0.0)

        return base_features[:33]

    def _features_from_v3_vector(self, features_vector: list[float]) -> list[float]:
        base = [float(x) for x in (features_vector or [])[:20]]
        while len(base) < 33:
            base.append(0.0)
        return base[:33]

    def _build_input_vector(self, record: FileRecord, algorithm: AlgorithmType) -> list[float]:
        return self._extract_features(record)[:33] + self._encode_algorithm(algorithm)

    def _build_cpp_training_rows(self) -> list[tuple[list[float], list[float], float]]:
        rows: list[tuple[list[float], list[float], float]] = []
        for s in self._training_data:
            feat_vec = [0.0] * 33
            if any(k.startswith("f") for k in s.features):
                for i in range(20):
                    feat_vec[i] = float(s.features.get(f"f{i}", 0.0))
            else:
                feat_vec = [
                    s.features.get('shannon_entropy', 0.5) / 8.0,
                    s.features.get('confidence', 0.5),
                    s.features.get('estimated_ratio', 0.5),
                    1.0,
                ] + [0.0] * 29

            algo_one_hot = [0.0] * 6
            algo_one_hot[s.algorithm_id % 6] = 1.0
            input_vec = feat_vec[:33] + algo_one_hot

            algo_type = None
            for at, aid in self.ALGORITHM_MAP.items():
                if aid == s.algorithm_id:
                    algo_type = at
                    break
            targets = self._normalize_params(s.actual_params, algo_type)
            rows.append((input_vec, targets, 1.0))
        return rows

    def clear_ingested_samples(self) -> None:
        self._training_data.clear()

    def count_by_algorithm(self) -> dict[str, int]:
        out: dict[str, int] = {}
        for s in self._training_data:
            key = str(s.algorithm_id)
            out[key] = out.get(key, 0) + 1
        return out

    def ingest_v3_sample(self, sample: object) -> bool:
        from gui.ade.training import algorithm_type_from_v3

        if not getattr(sample, "is_valid", True):
            sample.validate()
        if not sample.is_valid:
            return False
        if not sample.params_used:
            return False

        algorithm = algorithm_type_from_v3(sample)
        if algorithm is None:
            return False

        if algorithm not in self.ALGORITHM_MAP:
            return False

        from gui.ade.training import v3_algorithm_id_to_param_slot

        slot = v3_algorithm_id_to_param_slot(int(sample.algorithm_id))
        if slot is None:
            slot = self.ALGORITHM_MAP.get(algorithm)
        if slot is None:
            return False

        pr = ParamRegressionSample()
        pr.sample_id = sample.sample_id or uuid.uuid4().hex[:12]
        pr.algorithm_id = int(slot)
        pr.actual_params = {k: int(v) for k, v in sample.params_used.items()}
        pr.compression_ratio = float(sample.compression_ratio)
        pr.compression_time_ms = float(sample.compression_time_ms)
        if sample.features_vector:
            pr.features = {f"f{i}": float(v) for i, v in enumerate(sample.features_vector[:20])}
        else:
            pr.features = dict(sample.features_dict or {})

        self._training_data.append(pr)
        return True

    def predict(self, record: FileRecord, algorithm: AlgorithmType) -> dict[str, int] | None:
        if not self.is_ready:
            logger.debug("[param_regressor] model not ready")
            return None

        try:
            input_vec = self._build_input_vector(record, algorithm)
            normalized = self._cpp_net.predict(input_vec)
            params = self._denormalize_params(normalized)
            from gui.models import sanitize_stage2_params

            file_size = int(getattr(record, "size", 0) or 0)
            params = sanitize_stage2_params(algorithm, params, file_size=file_size)
            logger.info("[param_regressor] C++ predicted params for %s: %s",
                        algorithm.value, params)
            return params
        except Exception as e:
            logger.error("[param_regressor] prediction failed: %s", e)
            return None

    def collect_sample(
        self,
        record: FileRecord,
        algorithm: AlgorithmType,
        predicted_params: dict[str, int] | None,
        actual_params: dict[str, int],
        decision_result: DecisionResult | None = None,
    ) -> ParamRegressionSample:
        sample = ParamRegressionSample()
        sample.sample_id = uuid.uuid4().hex[:12]

        from gui.ade.engine import DecisionEngine
        de = DecisionEngine.get()
        if de._ade is not None and hasattr(record, 'raw_data') and record.raw_data:
            try:
                r = de._ade.analyze(list(record.raw_data))
                sample.features = {
                    'shannon_entropy': r.shannon_entropy,
                    'confidence': float(r.confidence),
                    'estimated_ratio': r.estimated_ratio,
                }
            except Exception:
                pass

        sample.algorithm_id = self.ALGORITHM_MAP.get(algorithm, 2)
        sample.predicted_params = predicted_params or {}
        sample.actual_params = actual_params
        sample.compression_ratio = getattr(record, 'compression_ratio', 1.0)
        sample.compression_time_ms = getattr(record, 'compression_time_ms', 0.0)

        if predicted_params and actual_params:
            total_error = 0.0
            for name in self.PARAM_NAMES:
                pred = predicted_params.get(name, 0)
                actual = actual_params.get(name, 0)
                min_val, max_val = self.PARAM_RANGES[name]
                range_val = max(max_val - min_val, 1)
                error = abs(pred - actual) / range_val
                total_error += error
            sample.param_error = total_error / len(self.PARAM_NAMES)

        self._training_data.append(sample)
        return sample

    def train(
        self,
        epochs: int = 50,
        lr: float = 0.001,
        batch_size: int = 32,
        validation_split: float = 0.2,
    ) -> dict[str, float]:
        if not self._has_cpp or self._cpp_net is None:
            raise RuntimeError(
                "C++ ParamRegressorNet 不可用，请重新编译 core_engine（含 ade ParamRegressorNet）"
            )

        if len(self._training_data) < 4:
            raise ValueError(f"Need >=4 training samples, got {len(self._training_data)}")

        rows = self._build_cpp_training_rows()
        logger.info("[param_regressor] C++ training on %d samples", len(rows))

        from gui.engine.compressor import CompressionEngine
        TrainCfg = CompressionEngine()._engine.ParamRegressorTrainConfig
        train_cfg = TrainCfg()
        train_cfg.epochs = int(epochs)
        train_cfg.learning_rate = float(lr)
        train_cfg.batch_size = int(min(batch_size, max(1, len(rows))))
        train_cfg.validation_split = float(validation_split)
        if len(rows) < 8:
            train_cfg.validation_split = 0.0

        logger.info("[param_regressor] invoking C++ train (%d rows)", len(rows))
        metrics = self._cpp_net.train(rows, train_cfg)
        final_metrics = {
            'final_train_loss': float(metrics.final_train_loss),
            'final_val_loss': float(metrics.final_val_loss),
            'epochs_completed': int(metrics.epochs_completed),
            'num_samples': int(metrics.num_samples),
        }
        logger.info("[param_regressor] C++ training complete: %s", final_metrics)
        return final_metrics

    def save_model(self, path: str | Path | None = None) -> bool:
        if not self.is_ready:
            logger.warning("[param_regressor] no trained model to save")
            return False

        target = Path(path) if path else self._get_default_model_path()
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            ok = bool(self._cpp_net.save(str(target)))
            if ok:
                logger.info("[param_regressor] C++ model saved to %s (%.1f KB)",
                            target, target.stat().st_size / 1024)
            return ok
        except Exception as e:
            logger.error("[param_regressor] failed to save model: %s", e)
            return False

    def load_model(self, path: str | Path | None = None) -> bool:
        if not self._has_cpp or self._cpp_net is None:
            return False

        target = Path(path) if path else self._get_default_model_path()
        if not target.exists():
            if self._cpp_net.try_load_default():
                logger.info("[param_regressor] loaded default C++ model")
                return True
            logger.info("[param_regressor] no saved model at %s", target)
            return False

        try:
            ok = bool(self._cpp_net.load(str(target)))
            if ok:
                logger.info("[param_regressor] C++ model loaded from %s", target)
            return ok
        except Exception as e:
            logger.error("[param_regressor] failed to load model: %s", e)
            return False

    def _get_default_model_path(self) -> Path:
        try:
            from gui.ade.training import get_training_store
            return get_training_store()._base_dir / "param_regressor.bin"
        except Exception:
            return Path("ade") / "param_regressor.bin"

    def get_info(self) -> dict[str, Any]:
        return {
            'available': self._has_cpp,
            'backend': 'cpp' if self._has_cpp else 'none',
            'ready': self.is_ready,
            'trained': self.is_ready,
            'num_samples': len(self._training_data),
            'input_dim': self._input_dim,
            'output_dim': self._output_dim,
            'hidden_dim': self._hidden_dim,
        }
