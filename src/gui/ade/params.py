from __future__ import annotations

import json
import logging
import math
import time
import uuid
from pathlib import Path
from typing import Any

from gui.ade.types import DecisionResult, ParamRegressionSample
from gui.models import ALGORITHM_PARAMS, AlgorithmType, FileRecord

logger = logging.getLogger(__name__)

class ParameterRegressor:
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
        self._model = None
        self._has_torch = False
        self._training_data: list[ParamRegressionSample] = []
        self._input_dim = 39
        self._output_dim = 5
        self._hidden_dim = 128
        self._is_trained = False

        try:
            import torch
            import torch.nn as nn
            self._torch = torch
            self._nn = nn
            self._has_torch = True
            logger.info("[param_regressor] PyTorch available")
        except ImportError:
            logger.warning("[param_regressor] PyTorch not available, Stage2 disabled")

    @classmethod
    def get(cls) -> "ParameterRegressor":
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    @property
    def is_ready(self) -> bool:
        return self._has_torch and self._model is not None and self._is_trained

    def build_model(self, input_dim: int | None = None, hidden_dim: int = 128) -> None:
        if not self._has_torch:
            raise RuntimeError("PyTorch not installed for parameter regression")

        nn = self._nn
        in_dim = input_dim or self._input_dim
        self._input_dim = in_dim
        self._hidden_dim = hidden_dim

        self._model = nn.Sequential(
            nn.Linear(in_dim, hidden_dim),
            nn.ReLU(),
            nn.BatchNorm1d(hidden_dim),
            nn.Dropout(0.3),
            nn.Linear(hidden_dim, hidden_dim // 2),
            nn.ReLU(),
            nn.BatchNorm1d(hidden_dim // 2),
            nn.Dropout(0.2),
            nn.Linear(hidden_dim // 2, 32),
            nn.ReLU(),
            nn.Linear(32, self._output_dim),
        )

        logger.info("[param_regressor] model built: input=%d, hidden=%d, output=%d",
                    in_dim, hidden_dim, self._output_dim)

    def _param_keys_for_algorithm(self, algorithm: AlgorithmType) -> list[str]:
        defs = ALGORITHM_PARAMS.get(algorithm, [])
        if defs:
            return [p.key for p in defs]
        return [k for k in self.PARAM_NAMES if k in self.PARAM_RANGES]

    def _normalize_params(self, params: dict[str, int], algorithm: AlgorithmType | None = None) -> list[float]:
        """Normalize to fixed output dim; keys not used by ``algorithm`` are zero."""
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

    def clear_ingested_samples(self) -> None:
        self._training_data.clear()

    def count_by_algorithm(self) -> dict[str, int]:
        out: dict[str, int] = {}
        for s in self._training_data:
            key = str(s.algorithm_id)
            out[key] = out.get(key, 0) + 1
        return out

    def ingest_v3_sample(self, sample: object) -> bool:
        """
        Ingest one JSONL row: arm = ``algorithm_used`` / ``algorithm_id``,
        knobs = ``params_used`` (algorithm-specific), reward = ``compression_ratio``.
        """
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
        pr.actual_params = {
            k: int(v) for k, v in sample.params_used.items()
        }
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
            logger.debug("[param_regressor] model not ready, returning default params")
            return None

        try:
            from gui.ade.engine import DecisionEngine
            de = DecisionEngine.get()

            features = self._extract_features(record)
            algo_one_hot = self._encode_algorithm(algorithm)
            input_vec = features[:33] + algo_one_hot

            input_tensor = self._torch.tensor([input_vec], dtype=self._torch.float32)

            with self._torch.no_grad():
                output = self._model(input_tensor)
                normalized_params = output[0].tolist()

            params = self._denormalize_params(normalized_params)

            logger.info("[param_regressor] predicted params for %s: %s",
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
        if not self._has_torch:
            raise RuntimeError("PyTorch not available")

        if len(self._training_data) < 20:
            raise ValueError(f"Need >=20 training samples, got {len(self._training_data)}")

        logger.info("[param_regressor] starting training on %d samples", len(self._training_data))

        X, y_algo, y_params = [], [], []
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
            X.append(input_vec)
            y_algo.append(s.algorithm_id)
            algo_type = None
            for at, aid in self.ALGORITHM_MAP.items():
                if aid == s.algorithm_id:
                    algo_type = at
                    break
            y_params.append(self._normalize_params(s.actual_params, algo_type))

        import random
        combined = list(zip(X, y_algo, y_params))
        random.shuffle(combined)
        X, y_algo, y_params = zip(*combined) if combined else ([], [], [])

        split_idx = int(len(X) * (1 - validation_split))
        X_train, X_val = list(X[:split_idx]), list(X[split_idx:])
        y_train, y_val = list(y_params[:split_idx]), list(y_params[split_idx:])

        if self._model is None:
            self.build_model(len(X_train[0]) if X_train else 39)

        dataset = list(zip(X_train, y_train))

        optimizer = self._torch.optim.Adam(
            self._model.parameters(),
            lr=lr,
            weight_decay=1e-5
        )
        criterion = self._torch.nn.MSELoss()

        self._model.train()
        best_val_loss = float('inf')
        patience_counter = 0
        patience_limit = 10
        history = {'train_loss': [], 'val_loss': []}

        for epoch in range(epochs):
            epoch_loss = 0.0
            random.shuffle(dataset)

            for i in range(0, len(dataset), batch_size):
                batch = dataset[i:i + batch_size]
                if not batch:
                    continue

                x_batch = self._torch.tensor([d[0] for d in batch], dtype=self._torch.float32)
                y_batch = self._torch.tensor([d[1] for d in batch], dtype=self._torch.float32)

                optimizer.zero_grad()
                output = self._model(x_batch)
                loss = criterion(output, y_batch)
                loss.backward()
                self._torch.nn.utils.clip_grad_norm_(self._model.parameters(), 1.0)
                optimizer.step()
                epoch_loss += loss.item()

            avg_train_loss = epoch_loss / max(len(dataset) // batch_size, 1)
            history['train_loss'].append(avg_train_loss)

            if X_val and y_val:
                self._model.eval()
                with self._torch.no_grad():
                    val_x = self._torch.tensor(X_val, dtype=self._torch.float32)
                    val_y = self._torch.tensor(y_val, dtype=self._torch.float32)
                    val_output = self._model(val_x)
                    val_loss = criterion(val_output, val_y).item()
                history['val_loss'].append(val_loss)
                self._model.train()

                if val_loss < best_val_loss - 1e-6:
                    best_val_loss = val_loss
                    patience_counter = 0
                else:
                    patience_counter += 1

                if patience_counter >= patience_limit:
                    logger.info("[param_regressor] early stopping at epoch %d", epoch + 1)
                    break

            if (epoch + 1) % 10 == 0 or epoch == 0:
                log_msg = f"[param_regressor] epoch {epoch+1}/{epochs} loss={avg_train_loss:.6f}"
                if history['val_loss']:
                    log_msg += f" val_loss={history['val_loss'][-1]:.6f}"
                logger.info(log_msg)

        self._model.eval()
        self._is_trained = True

        final_metrics = {
            'final_train_loss': history['train_loss'][-1] if history['train_loss'] else 0,
            'final_val_loss': history['val_loss'][-1] if history['val_loss'] else 0,
            'epochs_completed': epoch + 1,
            'num_samples': len(self._training_data),
        }

        logger.info("[param_regressor] training complete: %s", final_metrics)
        return final_metrics

    def save_model(self, path: str | Path | None = None) -> bool:
        if not self.is_ready:
            logger.warning("[param_regressor] no trained model to save")
            return False

        target = Path(path) if path else self._get_default_model_path()
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            state = {
                'model_state_dict': self._model.state_dict(),
                'input_dim': self._input_dim,
                'hidden_dim': self._hidden_dim,
                'is_trained': self._is_trained,
                'num_training_samples': len(self._training_data),
                'version': '3.0',
            }
            self._torch.save(state, target)
            logger.info("[param_regressor] model saved to %s (%.1f KB)",
                        target, target.stat().st_size / 1024)
            return True
        except Exception as e:
            logger.error("[param_regressor] failed to save model: %s", e)
            return False

    def load_model(self, path: str | Path | None = None) -> bool:
        if not self._has_torch:
            return False

        target = Path(path) if path else self._get_default_model_path()
        if not target.exists():
            logger.info("[param_regressor] no saved model at %s", target)
            return False

        try:
            state = self._torch.load(target, map_location='cpu')
            self.build_model(state.get('input_dim'), state.get('hidden_dim'))
            self._model.load_state_dict(state['model_state_dict'])
            self._is_trained = state.get('is_trained', True)
            self._model.eval()
            logger.info("[param_regressor] model loaded from %s (samples=%d)",
                        target, state.get('num_training_samples', 0))
            return True
        except Exception as e:
            logger.error("[param_regressor] failed to load model: %s", e)
            return False

    def _get_default_model_path(self) -> Path:
        import sys
        if getattr(sys, 'frozen', False):
            base = Path(sys.executable).parent.parent
            meipass = getattr(sys, "_MEIPASS", None)
        else:
            base = Path(__file__).resolve().parent.parent.parent.parent
            meipass = None
        candidates = [
            base / "ade" / "param_regressor.pt",
            base / "assets" / "ade" / "param_regressor_v3.pt",
            base / "src" / "ade" / "data" / "param_regressor_v3.pt",
        ]
        if meipass:
            candidates.insert(0, Path(meipass) / "ade" / "param_regressor.pt")
        for c in candidates:
            if c.parent.exists() or c == candidates[-1]:
                return c
        return candidates[-1]

    def get_info(self) -> dict[str, Any]:
        return {
            'available': self._has_torch,
            'ready': self.is_ready,
            'trained': self._is_trained,
            'model_built': self._model is not None,
            'num_samples': len(self._training_data),
            'input_dim': self._input_dim,
            'output_dim': self._output_dim,
            'hidden_dim': self._hidden_dim,
        }

