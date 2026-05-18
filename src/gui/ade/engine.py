from __future__ import annotations

import json
import logging
import time
import uuid
from dataclasses import asdict
from pathlib import Path
from typing import Any

from gui.ade.params import ParameterRegressor
from gui.ade.types import DecisionMode, DecisionResult, ParamRegressionSample, TrainingSample
from gui.engine.compressor import CompressionEngine
from gui.models import (
    AlgorithmType,
    ALGORITHM_PARAMS,
    CompressionStatus,
    FileRecord,
)

logger = logging.getLogger(__name__)


class DecisionEngine:
    _instance = None

    def __init__(self, engine: CompressionEngine | None = None):
        self._engine = engine or CompressionEngine()
        self._mode = DecisionMode.RULE_BASED
        self._training_data: list[TrainingSample] = []
        self._data_path: Path | None = None
        self._ea_algorithm = 1
        self._ea_max_time_ms = 3000
        self._ade = None
        self._current_record: FileRecord | None = None
        self._init_ade()

    def _init_ade(self) -> None:
        if not self._engine.available:
            logger.warning("[decision] core_engine not available, ADE disabled")
            return
        try:
            eng = self._engine._engine
            if not hasattr(eng, 'ADE'):
                logger.warning("[decision] ADE not found in core_engine")
                return
            self._ade = eng.ADE()

            model_loaded = self._ade.try_load_default_model()

            if not model_loaded:
                model_loaded = self._try_load_model_from_project()

            if model_loaded:
                self._mode = DecisionMode.ML_HYBRID
                self._ade.set_mode(1)
                logger.info("[decision] ADE loaded default model, using ML_HYBRID mode")
            else:
                logger.info("[decision] ADE using RULE_BASED mode (no default model)")
            logger.info("[decision] ADE initialized successfully")
        except Exception as e:
            logger.warning("[decision] ADE init failed: %s", e)
            self._ade = None

    def _try_load_model_from_project(self) -> bool:
        import sys
        candidates = []

        if getattr(sys, 'frozen', False):
            base = Path(sys.executable).parent
            meipass = getattr(sys, "_MEIPASS", None)
            if meipass:
                meipass_path = Path(meipass)
                candidates.extend([
                    meipass_path / "ade" / "default_model.bin",
                    meipass_path / "default_model.bin",
                ])
        else:
            base = Path(__file__).resolve().parent.parent.parent.parent

        candidates.extend([
            base / "assets" / "ade" / "default_model.bin",
            base / "ade" / "default_model.bin",
            base / "ade" / "data" / "default_model.bin",
            base / "default_model.bin",
        ])

        for path in candidates:
            if path.exists():
                try:
                    if self._ade.load_model(str(path)):
                        logger.info("[decision] loaded model from %s", path)
                        return True
                except Exception as e:
                    logger.debug("[decision] failed to load from %s: %s", path, e)

        return False

    @classmethod
    def get(cls) -> "DecisionEngine":
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    def set_mode(self, mode: DecisionMode) -> None:
        self._mode = mode
        if self._ade is not None:
            try:
                self._ade.set_mode(int(mode))
                logger.info("[decision] ADE mode set to %s", mode.name)
            except Exception as e:
                logger.warning("[decision] failed to set ADE mode: %s", e)

    def get_mode(self) -> DecisionMode:
        return self._mode

    def set_ea_algorithm(self, algo_id: int) -> None:
        self._ea_algorithm = max(0, min(algo_id, 3))

    def set_ea_max_time(self, ms: int) -> None:
        self._ea_max_time_ms = max(100, min(ms, 60000))

    def decide(self, record: FileRecord) -> DecisionResult:
        if self._ade is None:
            return DecisionResult(
                algorithm=AlgorithmType.LZDP,
                confidence=0.0,
                reason="ADE not available",
                mode_used="fallback",
            )

        self._current_record = record
        t0 = time.perf_counter()

        try:
            if hasattr(record, 'path') and record.path and Path(record.path).exists():
                result = self._ade.analyze_file(record.path)
            elif hasattr(record, 'raw_data') and record.raw_data:
                result = self._ade.analyze(list(record.raw_data))
            else:
                return DecisionResult(
                    algorithm=AlgorithmType.LZDP,
                    confidence=0.0,
                    reason="No data available for analysis",
                    mode_used="fallback",
                )

            extraction_ms = (time.perf_counter() - t0) * 1000
            algo = self._map_algorithm(result.algorithm)

            params = None
            if algo != AlgorithmType.NONE:
                params = self._optimize_params(algo)

            return DecisionResult(
                algorithm=algo,
                confidence=result.confidence,
                reason=result.reason,
                params=params,
                extraction_time_ms=extraction_ms,
                mode_used=self._mode.name,
            )

        except Exception as e:
            logger.error("[decision] decide failed: %s", e)
            return DecisionResult(
                algorithm=AlgorithmType.LZDP,
                confidence=0.0,
                reason=f"Decision error: {e}",
                mode_used="error_fallback",
            )

    def _map_algorithm(self, core_algo_id) -> AlgorithmType:
        if not self._engine.available:
            return AlgorithmType.LZDP
        eng = self._engine._engine
        if core_algo_id == eng.AlgorithmID.DEFLATE:
            return AlgorithmType.DEFLATE
        elif core_algo_id == eng.AlgorithmID.LZSS:
            return AlgorithmType.LZSS
        elif core_algo_id == eng.AlgorithmID.LZDP:
            return AlgorithmType.LZDP
        elif core_algo_id == eng.AlgorithmID.DPFLATE:
            return AlgorithmType.DPFLATE
        elif core_algo_id == eng.AlgorithmID.BROTLI:
            return AlgorithmType.BROTLI
        elif core_algo_id == eng.AlgorithmID.ZSTD:
            return AlgorithmType.ZSTD
        elif core_algo_id == eng.AlgorithmID.NONE:
            return AlgorithmType.NONE
        else:
            return AlgorithmType.LZDP

    def decide_batch(self, records: list[FileRecord]) -> list[DecisionResult]:
        results = []
        for rec in records:
            results.append(self.decide(rec))
        return results

    def _optimize_params(self, algorithm: AlgorithmType) -> dict[str, int] | None:
        regressor = ParameterRegressor.get()

        if regressor.is_ready and hasattr(self, '_current_record') and self._current_record:
            try:
                nn_params = regressor.predict(self._current_record, algorithm)
                if nn_params:
                    logger.info("[decision] Stage2a: NN predicted params for %s: %s",
                                algorithm.value, nn_params)
                    return nn_params
            except Exception as e:
                logger.debug("[decision] Stage2a NN prediction failed: %s", e)

        if not self._engine.available:
            return None
        try:
            eng = self._engine._engine
            if not hasattr(eng, 'ParameterOptimizer'):
                return None

            opt = eng.ParameterOptimizer()
            opt.set_algorithm(self._ea_algorithm)

            bounds = eng.ParameterBounds()
            default_cfg = ALGORITHM_PARAMS.get(algorithm, {})

            target_ws = default_cfg.get('window_size', 32768)
            target_mm = default_cfg.get('min_match', 3)
            target_mc = default_cfg.get('max_chain_length', 128)
            target_la = default_cfg.get('lookahead_size', 64)
            target_dp = default_cfg.get('dp_range', 2)

            def fitness(params):
                ws_err = abs(params.window_size - target_ws) / max(target_ws, 1)
                mm_err = abs(params.min_match - target_mm) / max(target_mm, 1)
                mc_err = abs(params.max_chain_length - target_mc) / max(target_mc, 1)
                la_err = abs(params.lookahead_size - target_la) / max(target_la, 1)
                dp_err = abs(params.dp_range - target_dp) / max(target_dp, 1)
                return ws_err + mm_err + mc_err + la_err + dp_err

            ea_result = opt.optimize(fitness, bounds, algo_id=1, max_time_ms=self._ea_max_time_ms)
            best = ea_result.best_params
            logger.info("[decision] Stage2b: EA optimized params for %s", algorithm.value)
            return {
                'window_size': best.window_size,
                'min_match': best.min_match,
                'max_chain_length': best.max_chain_length,
                'lookahead_size': best.lookahead_size,
                'dp_range': best.dp_range,
            }
        except Exception as e:
            logger.debug("[decision] EA param optimization skipped: %s", e)
            return None

    def collect_training_sample(
        self,
        record: FileRecord,
        decision: DecisionResult | None = None,
    ) -> TrainingSample:
        sample = TrainingSample()
        sample.record_id = uuid.uuid4().hex[:12]
        sample.filepath = getattr(record, 'path', '')
        sample.file_size = getattr(record, 'size', len(getattr(record, 'raw_data', b'')))
        sample.algorithm_used = decision.algorithm.value if decision else getattr(record, 'algorithm', AlgorithmType.AUTO).value
        sample.compression_ratio = getattr(record, 'compression_ratio', 1.0)
        sample.compression_time_ms = getattr(record, 'compression_time_ms', 0.0)
        sample.output_size_bytes = len(getattr(record, 'compressed_data') or b'')
        sample.success = getattr(record, 'status', CompressionStatus.DONE) == CompressionStatus.DONE
        sample.sample_weight = 1.0 if sample.success else 0.5

        # Priority 1: Use 20-dim BaseFeatures from FileRecord
        base_features = getattr(record, 'base_features', None)
        if base_features is not None and hasattr(base_features, 'to_dict'):
            sample.features = base_features.to_dict()
            sample.features['data_version'] = '3.0'
            sample.features['feature_dim'] = 20
        # Priority 2: Fall back to C++ ADE analysis
        elif self._ade is not None and hasattr(record, 'raw_data') and record.raw_data:
            try:
                ade_result = self._ade.analyze(list(record.raw_data))
                sample.features = {
                    'shannon_entropy': ade_result.shannon_entropy,
                    'file_type': ade_result.file_type,
                    'confidence': ade_result.confidence,
                    'estimated_ratio': ade_result.estimated_ratio,
                    'extraction_time_ms': ade_result.extraction_time_ms,
                    'data_version': '2.0',
                    'feature_dim': 4,
                }
            except Exception:
                pass
        elif self._ade is not None and hasattr(record, 'path') and record.path:
            try:
                ade_result = self._ade.analyze_file(record.path)
                sample.features = {
                    'shannon_entropy': ade_result.shannon_entropy,
                    'file_type': ade_result.file_type,
                    'confidence': ade_result.confidence,
                    'estimated_ratio': ade_result.estimated_ratio,
                    'extraction_time_ms': ade_result.extraction_time_ms,
                    'data_version': '2.0',
                    'feature_dim': 4,
                }
            except Exception:
                pass

        self._training_data.append(sample)

        # Also collect to V3 store
        try:
            from gui.ade.training import get_training_store
            store = get_training_store()
            store.add_from_record(record, decision)
        except Exception as e:
            logger.debug("[decision] V3 store collection skipped: %s", e)

        return sample

    def collect_param_regression_sample(
        self,
        record: FileRecord,
        decision: DecisionResult | None = None,
    ) -> ParamRegressionSample | None:
        regressor = ParameterRegressor.get()
        algorithm = decision.algorithm if decision else getattr(record, 'algorithm', AlgorithmType.LZDP)

        predicted_params = decision.params if decision and decision.params else None
        actual_params = decision.params if decision and decision.params else ALGORITHM_PARAMS.get(algorithm, {})

        try:
            param_sample = regressor.collect_sample(
                record=record,
                algorithm=algorithm,
                predicted_params=predicted_params,
                actual_params=actual_params,
                decision_result=decision,
            )

            logger.info("[decision] collected param regression sample: algo=%s, error=%.4f",
                        algorithm.value, param_sample.param_error)
            return param_sample
        except Exception as e:
            logger.warning("[decision] failed to collect param regression sample: %s", e)
            return None

    def auto_retrain_param_regressor(self, min_samples: int = 50) -> dict[str, Any] | None:
        regressor = ParameterRegressor.get()
        if len(regressor._training_data) < min_samples:
            logger.info("[decision] not enough samples for retrain: %d/%d",
                       len(regressor._training_data), min_samples)
            return None

        try:
            metrics = regressor.train(epochs=30, lr=0.001, batch_size=16)
            regressor.save_model()

            logger.info("[decision] param regressor retrained: %s", metrics)
            return metrics
        except Exception as e:
            logger.error("[decision] param regressor retrain failed: %s", e)
            return None

    def train(self, records: list[FileRecord]) -> None:
        for rec in records:
            if rec.status == CompressionStatus.DONE:
                self.collect_training_sample(rec)
        logger.info("[decision] collected %d training samples", len(self._training_data))

    def train_with_feedback(
        self,
        records: list[FileRecord],
        decisions: list[DecisionResult] | None = None,
    ) -> dict[str, int]:
        stats = {'classification_samples': 0, 'param_regression_samples': 0}

        for i, rec in enumerate(records):
            if rec.status == CompressionStatus.DONE:
                decision = decisions[i] if decisions and i < len(decisions) else None
                self.collect_training_sample(rec, decision)
                stats['classification_samples'] += 1

                param_sample = self.collect_param_regression_sample(rec, decision)
                if param_sample:
                    stats['param_regression_samples'] += 1

        logger.info("[decision] training feedback collected: %s", stats)

        if stats['param_regression_samples'] >= 50:
            retrain_result = self.auto_retrain_param_regressor(min_samples=50)
            if retrain_result:
                logger.info("[decision] auto-retrain triggered and completed")

        return stats

    def save_training_data(self, path: str | Path | None = None) -> bool:
        target = Path(path) if path else self._get_default_data_path()
        if not target:
            return False
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            data = [asdict(s) for s in self._training_data]
            target.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')
            logger.info("[decision] saved %d samples to %s", len(self._training_data), target)

            # Also save V3 store
            try:
                from gui.ade.training import get_training_store
                store = get_training_store()
                store.save(incremental=True)
            except Exception as e:
                logger.debug("[decision] V3 store save skipped: %s", e)

            return True
        except Exception as e:
            logger.error("[decision] failed to save training data: %s", e)
            return False

    def load_training_data(self, path: str | Path | None = None) -> int:
        target = Path(path) if path else self._get_default_data_path()
        if not target or not target.exists():
            return 0
        try:
            raw = json.loads(target.read_text(encoding='utf-8'))
            self._training_data = [TrainingSample(**d) for d in raw]
            logger.info("[decision] loaded %d samples from %s", len(self._training_data), target)
            return len(self._training_data)
        except Exception as e:
            logger.error("[decision] failed to load training data: %s", e)
            return 0

    def _get_default_data_path(self) -> Path | None:
        import sys
        base = Path(sys.executable).parent.parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parent.parent.parent.parent
        candidates = [
            base / "ade" / "training_data.json",
            base / "assets" / "ade" / "training_data_v3.json",
        ]
        for c in candidates:
            if c.parent.exists() or c == candidates[-1]:
                return c
        return None

    @property
    def sample_count(self) -> int:
        return len(self._training_data)

    @property
    def is_ready(self) -> bool:
        return self._ade is not None

    def get_info(self) -> dict[str, Any]:
        info = {
            'mode': self._mode.name,
            'ready': self.is_ready,
            'sample_count': self.sample_count,
            'ea_algorithm': self._ea_algorithm,
            'ea_max_time_ms': self._ea_max_time_ms,
        }
        if self._ade is not None:
            try:
                info['ml_ready'] = self._ade.is_ml_ready()
                info['ade_json'] = self._ade.to_json()
            except Exception:
                pass
        return info


class RandomForestStrategy(DecisionEngine):

    def __init__(self, engine: CompressionEngine | None = None):
        super().__init__(engine)
        self.set_mode(DecisionMode.ML_ONLY)

    def decide(self, record: FileRecord) -> DecisionResult:
        if self._ade is None:
            return DecisionResult(
                algorithm=AlgorithmType.LZDP,
                confidence=0.0,
                reason="ADE/RF not available",
                mode_used="rf_fallback",
            )
        self.set_mode(DecisionMode.ML_ONLY)
        return super().decide(record)

    def train(self, records: list[FileRecord]) -> None:
        super().train(records)
        if self._ade is None:
            return
        try:
            loaded = self._ade.try_load_default_model()
            if loaded:
                logger.info("[rf_strategy] default model loaded")
        except Exception as e:
            logger.warning("[rf_strategy] model loading failed: %s", e)


class NeuralNetworkStrategy(DecisionEngine):

    def __init__(self, engine: CompressionEngine | None = None):
        super().__init__(engine)
        self._model = None
        self._has_torch = False
        try:
            import torch
            import torch.nn as nn
            self._torch = torch
            self._nn = nn
            self._has_torch = True
        except ImportError:
            logger.warning("[nn_strategy] PyTorch not available")

    def decide(self, record: FileRecord) -> DecisionResult:
        if not self._has_torch or self._model is None:
            return DecisionResult(
                algorithm=AlgorithmType.LZDP,
                confidence=0.0,
                reason="NN model not ready",
                mode_used="nn_fallback",
            )
        try:
            features = self._extract_features(record)
            input_tensor = self._torch.tensor([features], dtype=self._torch.float32)
            with self._torch.no_grad():
                output = self._model(input_tensor)
                probs = self._torch.softmax(output, dim=1)
                pred_idx = self._torch.argmax(probs, dim=1).item()
                confidence = probs[0][pred_idx].item()

            algo_list = [
                AlgorithmType.DEFLATE, AlgorithmType.LZSS, AlgorithmType.LZDP,
                AlgorithmType.DPFLATE, AlgorithmType.BROTLI, AlgorithmType.ZSTD,
            ]
            algo = algo_list[pred_idx] if pred_idx < len(algo_list) else AlgorithmType.LZDP

            return DecisionResult(
                algorithm=algo,
                confidence=confidence,
                reason=f"NN prediction (idx={pred_idx})",
                mode_used="neural_network",
            )
        except Exception as e:
            logger.error("[nn_strategy] inference failed: %s", e)
            return DecisionResult(
                algorithm=AlgorithmType.LZDP,
                confidence=0.0,
                reason=f"NN error: {e}",
                mode_used="nn_error_fallback",
            )

    def _extract_features(self, record: FileRecord) -> list[float]:
        if self._ade is not None and hasattr(record, 'raw_data') and record.raw_data:
            try:
                r = self._ade.analyze(list(record.raw_data))
                return [
                    r.shannon_entropy / 8.0,
                    float(r.confidence),
                    r.estimated_ratio,
                    1.0 if r.algorithm != self._engine._engine.AlgorithmID.NONE else 0.0,
                ]
            except Exception:
                pass
        return [0.5] * 33

    def build_model(self, input_dim: int = 33, num_classes: int = 6, hidden_dim: int = 64) -> None:
        if not self._has_torch:
            raise RuntimeError("PyTorch not installed")
        nn = self._nn
        self._model = nn.Sequential(
            nn.Linear(input_dim, hidden_dim),
            nn.ReLU(),
            nn.Dropout(0.3),
            nn.Linear(hidden_dim, hidden_dim // 2),
            nn.ReLU(),
            nn.Dropout(0.2),
            nn.Linear(hidden_dim // 2, num_classes),
        )
        logger.info("[nn_strategy] model built: input=%d, hidden=%d, classes=%d", input_dim, hidden_dim, num_classes)

    def train(self, records: list[FileRecord], epochs: int = 10, lr: float = 0.001) -> None:
        if not self._has_torch:
            return
        super().train(records)
        if len(self._training_data) < 10:
            logger.warning("[nn_strategy] need >=10 samples, got %d", len(self._training_data))
            return
        try:
            X, y = [], []
            algo_map = {
                'DEFLATE': 0, 'LZSS': 1, 'LZDP': 2,
                'DPFLATE': 3, 'BROTLI': 4, 'ZSTD': 5,
            }
            for s in self._training_data:
                feats = s.features
                feat_vec = [
                    feats.get('shannon_entropy', 0.5) / 8.0,
                    feats.get('confidence', 0.5),
                    feats.get('estimated_ratio', 0.5),
                ] + [0.0] * 30
                X.append(feat_vec)
                y.append(algo_map.get(s.algorithm_used.upper(), 2))

            if self._model is None:
                self.build_model()

            dataset = list(zip(X, y))
            optimizer = self._torch.optim.Adam(self._model.parameters(), lr=lr)
            criterion = self._torch.nn.CrossEntropyLoss()

            self._model.train()
            for epoch in range(epochs):
                total_loss = 0.0
                correct = 0
                for xi, yi in dataset:
                    x = self._torch.tensor([xi], dtype=self._torch.float32)
                    label = self._torch.tensor([yi], dtype=self._torch.long)
                    optimizer.zero_grad()
                    out = self._model(x)
                    loss = criterion(out, label)
                    loss.backward()
                    optimizer.step()
                    total_loss += loss.item()
                    if self._torch.argmax(out, dim=1).item() == yi:
                        correct += 1
                acc = correct / len(dataset)
                logger.info("[nn_strategy] epoch %d/%d loss=%.4f acc=%.2f%%",
                            epoch + 1, epochs, total_loss / len(dataset), acc * 100)

            self._model.eval()
            logger.info("[nn_strategy] training complete on %d samples", len(dataset))
        except Exception as e:
            logger.error("[nn_strategy] training failed: %s", e)


class StrategyDispatcher:

    def __init__(
        self,
        engine: CompressionEngine | None = None,
        strategy: DecisionEngine | None = None,
    ):
        self._engine = engine or CompressionEngine()
        self._strategy = strategy or DecisionEngine(self._engine)

    def dispatch(self, record: FileRecord) -> tuple[FileRecord, DecisionResult]:
        decision = self._strategy.decide(record)
        record.algorithm = decision.algorithm
        return record, decision

    def dispatch_and_compress(self, record: FileRecord) -> tuple[FileRecord, DecisionResult]:
        record, decision = self.dispatch(record)
        if decision.algorithm == AlgorithmType.NONE:
            record.status = CompressionStatus.SKIPPED
            record.error_message = decision.reason
            return record, decision

        record.status = CompressionStatus.COMPRESSING
        try:
            snap = CompressionEngine.snapshot_for_algorithm(decision.algorithm)
            from gui.ade.explorer import SilentExplorer

            with SilentExplorer.user_compression_priority():
                result = self._engine.compress(record.raw_data, decision.algorithm)
            record.compressed_data = bytes(result.data)
            record.compression_ratio = result.compression_ratio
            record.compression_time_ms = result.time_ms
            record.status = CompressionStatus.DONE if result.success else CompressionStatus.FAILED
            if not result.success:
                record.error_message = result.error_message
                record.compression_config_snapshot = None
            else:
                record.compression_config_snapshot = snap
        except Exception as e:
            record.status = CompressionStatus.FAILED
            record.error_message = str(e)
            record.compression_config_snapshot = None
            logger.error("[dispatcher] compress failed %s: %s", record.path, e)

        self._strategy.collect_training_sample(record, decision)
        return record, decision

    def dispatch_batch(self, records: list[FileRecord]) -> list[tuple[FileRecord, DecisionResult]]:
        return [self.dispatch(rec) for rec in records]

    @property
    def strategy(self) -> DecisionEngine:
        return self._strategy

    @strategy.setter
    def strategy(self, value: DecisionEngine) -> None:
        self._strategy = value
