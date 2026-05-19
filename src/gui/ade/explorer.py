"""
Silent Explorer: AC-UCB based exploration policy for ADE

Implements Adaptive Clustered Upper Confidence Bound (AC-UCB) algorithm
for silent background exploration of compression algorithms.

Theory: Multi-Armed Bandit with Context (Contextual Bandit)
  - Context = file's 20-dim BaseFeatures vector
  - Arms    = algorithms × parameter combinations
  - Reward  = compression ratio (lower is better)
"""

from __future__ import annotations

import math
import os
import random
import threading
import time
import logging
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

from gui.models import AlgorithmType, FileRecord


@dataclass
class ArmStats:
    """Statistics for one arm in a cluster"""
    mean_ratio: float = 1.0
    count: int = 0
    sum_ratio: float = 0.0
    min_ratio: float = 1.0


class FeatureClusterSpace:
    """
    Maps 20-dim feature vectors to discrete clusters.
    
    Files in the same cluster are assumed to have similar optimal algorithms.
    Uses simple rule-based clustering (no K-Means dependency).
    """

    MAX_CLUSTERS = 64
    DISTANCE_THRESHOLD = 0.20

    def __init__(self):
        self.centers: dict[int, list[float]] = {}
        self._next_id = 0

    def assign(self, feature_vector: list[float]) -> int:
        vec = feature_vector

        if len(self.centers) == 0:
            return self._create_cluster(vec)

        best_id = -1
        best_dist = float('inf')

        for cid, center in self.centers.items():
            d = self._distance(vec, center)
            if d < best_dist:
                best_dist = d
                best_id = cid

        if best_dist > self.DISTANCE_THRESHOLD and len(self.centers) < self.MAX_CLUSTERS:
            return self._create_cluster(vec)

        return best_id

    def _create_cluster(self, vec: list[float]) -> int:
        cid = self._next_id
        self._next_id += 1
        self.centers[cid] = list(vec)
        return cid

    @staticmethod
    def _distance(a: list[float], b: list[float]) -> float:
        if HAS_NUMPY:
            return float(np.linalg.norm(np.array(a) - np.array(b)))
        return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))

    @property
    def n_clusters(self) -> int:
        return len(self.centers)


class SilentExplorer:
    """
    AC-UCB based silent exploration engine.
    
    Decides whether to explore after each compression,
    picks the target algorithm/parameters,
    and executes exploration asynchronously.
    
    Usage:
        explorer = SilentExplorer.get()
        
        # After main compression completes:
        explorer.maybe_explore(record, greedy_algorithm)
    """

    _instance: SilentExplorer | None = None
    _lock = threading.Lock()
    _user_ops_depth = 0
    _user_ops_lock = threading.Lock()

    # Tunables merged with optional ``config`` in ``__init__``; ``enabled`` comes from settings + env.
    DEFAULT_TUNABLES = {
        "epsilon_base": 0.25,
        "alpha_ucb": 1.41,
        "max_concurrent": 2,
        "timeout_seconds": 30.0,
        "budget_ratio": 0.30,
        "min_file_size_bytes": 1024,
        "max_file_size_for_l2": 100 * 1024 * 1024,
        "warmup_samples": 50,
        "ucb_gap_threshold": 0.05,
        "l1_min_samples": 3,
        "l2_min_samples": 5,
    }

    def __init__(self, config: dict | None = None):
        from gui.config.settings import get_ade_explorer_tunables, get_silent_explore_enabled

        cfg = {**get_ade_explorer_tunables(), **(config or {})}
        persisted = bool(get_silent_explore_enabled())
        if os.environ.get("WEBCOMPRESS_SILENT_EXPLORE", "").strip().lower() in ("1", "true", "yes"):
            persisted = True
        self.enabled = persisted
        if config is not None and "enabled" in config:
            self.enabled = bool(config["enabled"])
        self.epsilon_base: float = cfg["epsilon_base"]
        self.alpha_ucb: float = cfg["alpha_ucb"]
        self.max_concurrent: int = cfg["max_concurrent"]
        self.timeout_seconds: float = cfg["timeout_seconds"]
        self.budget_ratio: float = cfg["budget_ratio"]
        self.min_file_size: int = cfg["min_file_size_bytes"]
        self.max_file_size_l2: int = cfg["max_file_size_for_l2"]
        self.warmup_samples: int = cfg["warmup_samples"]
        self.ucb_gap_threshold: float = cfg["ucb_gap_threshold"]
        self.l1_min_samples: int = cfg["l1_min_samples"]
        self.l2_min_samples: int = cfg["l2_min_samples"]

        self.cluster_space = FeatureClusterSpace()
        self.cluster_stats: dict[int, dict[AlgorithmType, ArmStats]] = {}
        self.total_samples: int = 0
        self.explore_count: int = 0
        self.discovery_count: int = 0

        self._active_threads: int = 0
        self._total_compress_time: float = 0.0
        self._total_explore_time: float = 0.0

    @classmethod
    def begin_user_operation(cls) -> None:
        """用户主路径压缩/解压进入关键区：禁止新起静默探索线程（类「中断」优先级）。"""
        with cls._user_ops_lock:
            cls._user_ops_depth += 1

    @classmethod
    def end_user_operation(cls) -> None:
        with cls._user_ops_lock:
            cls._user_ops_depth = max(0, cls._user_ops_depth - 1)

    @classmethod
    def user_operations_active(cls) -> bool:
        with cls._user_ops_lock:
            return cls._user_ops_depth > 0

    @classmethod
    @contextmanager
    def user_compression_priority(cls):
        """``with SilentExplorer.user_compression_priority():`` 包裹 native 压/解压调用。"""
        cls.begin_user_operation()
        try:
            yield
        finally:
            cls.end_user_operation()

    @classmethod
    def apply_enabled_from_settings(cls) -> None:
        """从磁盘配置刷新单例开关与探索器可调参数。"""
        cls.apply_tunables_from_settings()

    @classmethod
    def apply_tunables_from_settings(cls) -> None:
        from gui.config.settings import get_ade_explorer_tunables, get_silent_explore_enabled

        inst = cls.get()
        t = get_ade_explorer_tunables()
        inst.epsilon_base = float(t["epsilon_base"])
        inst.alpha_ucb = float(t["alpha_ucb"])
        inst.max_concurrent = int(t["max_concurrent"])
        inst.timeout_seconds = float(t["timeout_seconds"])
        inst.budget_ratio = float(t["budget_ratio"])
        inst.min_file_size = int(t["min_file_size_bytes"])
        inst.max_file_size_l2 = int(t["max_file_size_for_l2"])
        inst.warmup_samples = int(t["warmup_samples"])
        inst.ucb_gap_threshold = float(t["ucb_gap_threshold"])
        inst.l1_min_samples = int(t["l1_min_samples"])
        inst.l2_min_samples = int(t["l2_min_samples"])
        persisted = bool(get_silent_explore_enabled())
        if os.environ.get("WEBCOMPRESS_SILENT_EXPLORE", "").strip().lower() in ("1", "true", "yes"):
            persisted = True
        inst.enabled = persisted

    def set_enabled(self, enabled: bool) -> None:
        """运行时开关（由 UI 写入；``WEBCOMPRESS_SILENT_EXPLORE`` 仅在 ``apply_enabled_from_settings`` 时覆盖读盘值）。"""
        self.enabled = bool(enabled)

    @classmethod
    def get(cls) -> SilentExplorer:
        with cls._lock:
            if cls._instance is None:
                cls._instance = cls()
            return cls._instance

    @classmethod
    def reset(cls) -> None:
        with cls._lock:
            cls._instance = None

    def maybe_explore(
        self,
        record: FileRecord,
        greedy_algo: AlgorithmType,
        compress_time_ms: float = 0.0,
    ) -> bool:
        """
        Decide whether to trigger silent exploration.
        
        Args:
            record: The compressed FileRecord (with base_features populated)
            greedy_algo: The algorithm chosen by DecisionEngine (exploit path)
            compress_time_ms: Time taken for the main compression
            
        Returns:
            True if exploration was triggered
        """
        from gui.ade.explore_log import log_explore

        file_name = getattr(record, "name", "") or ""

        if not self.enabled:
            log_explore("skip", reason="disabled", file=file_name, level=logging.DEBUG)
            return False

        if self.user_operations_active():
            log_explore("skip", reason="user_ops_active", file=file_name)
            return False

        if record.base_features is None:
            try:
                if not getattr(record, "raw_data", None):
                    record.load_raw_data()
                record.extract_features()
            except Exception as e:
                log_explore(
                    "skip",
                    reason="feature_extract_failed",
                    file=file_name,
                    error=str(e),
                )
                return False
        if record.base_features is None:
            log_explore("skip", reason="no_base_features", file=file_name)
            return False

        if record.size < self.min_file_size:
            log_explore(
                "skip",
                reason="file_too_small",
                file=file_name,
                size=record.size,
                min_size=self.min_file_size,
                level=logging.DEBUG,
            )
            return False

        if greedy_algo == AlgorithmType.NONE:
            log_explore("skip", reason="greedy_none", file=file_name, level=logging.DEBUG)
            return False

        cluster_id = self.cluster_space.assign(record.base_features.vector)

        should, target_algo, explore_type = self._should_explore(
            cluster_id, greedy_algo, record.size
        )

        if not should:
            self._record_arm_result(cluster_id, greedy_algo, record.compression_ratio)
            log_explore(
                "skip",
                reason="policy",
                file=file_name,
                cluster_id=cluster_id,
                greedy=greedy_algo.value,
            )
            return False

        if self._active_threads >= self.max_concurrent:
            log_explore(
                "skip",
                reason="max_concurrent",
                file=file_name,
                active=self._active_threads,
                max=self.max_concurrent,
            )
            return False

        budget_ok = self._check_budget(compress_time_ms)
        if not budget_ok:
            log_explore(
                "skip",
                reason="budget",
                file=file_name,
                explore_time_s=round(self._total_explore_time, 3),
                compress_time_s=round(self._total_compress_time, 3),
                budget_ratio=self.budget_ratio,
            )
            return False

        target_params = self._pick_explore_params(explore_type, target_algo, record)
        parent_decision = greedy_algo.value

        ucb_gap = self._compute_ucb_gap(cluster_id, greedy_algo)

        safe_name = (file_name or "file")[:48].replace("/", "_")
        thread = threading.Thread(
            target=self._execute_explore_async,
            args=(record, target_algo, target_params, explore_type,
                  parent_decision, cluster_id, ucb_gap),
            daemon=True,
            name=f"silent-explore-{safe_name}",
        )
        thread.start()

        log_explore(
            "launched",
            file=file_name,
            parent=parent_decision,
            target=target_algo.value,
            explore_type=explore_type,
            cluster_id=cluster_id,
            ucb_gap=round(ucb_gap, 6),
            params=target_params,
            size=getattr(record, "size", 0),
        )
        return True

    def _should_explore(
        self,
        cluster_id: int,
        greedy_algo: AlgorithmType,
        file_size: int,
    ) -> tuple[bool, AlgorithmType | None, str]:
        stats = self.cluster_stats.get(cluster_id, {})
        N_total = sum(s.count for s in stats.values())

        epsilon_eff = self._effective_epsilon(N_total, cluster_id)

        if random.random() > epsilon_eff:
            return False, None, ""

        if N_total < self.l1_min_samples * len(AlgorithmType):
            target = self._pick_least_sampled(cluster_id, greedy_algo)
            if target:
                return True, target, "L1_coldstart"

        ucb_scores = self._compute_ucb_scores(cluster_id, N_total)
        best_algo = min(ucb_scores, key=ucb_scores.get)
        gap = abs(ucb_scores.get(best_algo, 1.0) - ucb_scores.get(greedy_algo, 1.0))

        if best_algo != greedy_algo and gap > self.ucb_gap_threshold:
            return True, best_algo, "L1_algo"

        if N_total >= self.l2_min_samples and file_size <= self.max_file_size_l2:
            algo_stats = stats.get(greedy_algo)
            if algo_stats and algo_stats.count >= self.l2_min_samples:
                return True, greedy_algo, "L2_param"

        least_sampled = self._pick_least_sampled(cluster_id, greedy_algo)
        if least_sampled:
            return True, least_sampled, "L1_undersampled"

        return False, None, ""

    def _compute_ucb_scores(self, cluster_id: int, N_total: int) -> dict[AlgorithmType, float]:
        scores: dict[AlgorithmType, float] = {}
        stats = self.cluster_stats.get(cluster_id, {})

        for algo in AlgorithmType:
            if algo == AlgorithmType.AUTO:
                continue
            s = stats.get(algo)
            if s is None or s.count == 0:
                scores[algo] = float('inf')
            else:
                bonus = self.alpha_ucb * math.sqrt(math.log(N_total + 1) / s.count)
                scores[algo] = s.mean_ratio - bonus

        return scores

    def _compute_ucb_gap(self, cluster_id: int, greedy_algo: AlgorithmType) -> float:
        stats = self.cluster_stats.get(cluster_id, {})
        N_total = max(1, sum(s.count for s in stats.values()))
        scores = self._compute_ucb_scores(cluster_id, N_total)
        best_val = min(scores.values()) if scores else 1.0
        greedy_val = scores.get(greedy_algo, 1.0)
        return abs(best_val - greedy_val)

    def _pick_least_sampled(
        self, cluster_id: int, exclude: AlgorithmType | None = None
    ) -> AlgorithmType | None:
        stats = self.cluster_stats.get(cluster_id, {})
        candidates = [
            (algo, s.count) for algo, s in stats.items()
            if algo != exclude and algo not in (AlgorithmType.AUTO, AlgorithmType.NONE, AlgorithmType.TRANSFORMER)
        ]
        if not candidates:
            all_algos = [a for a in AlgorithmType
                        if a not in (AlgorithmType.AUTO, AlgorithmType.NONE, AlgorithmType.TRANSFORMER)]
            random.shuffle(all_algos)
            return all_algos[0] if all_algos else None
        candidates.sort(key=lambda x: x[1])
        return candidates[0][0]

    def _pick_explore_params(
        self, explore_type: str, algo: AlgorithmType, record: FileRecord
    ) -> dict[str, int]:
        from gui.engine.compressor import CompressionEngine
        from gui.models import ALGORITHM_PARAMS

        default_config = CompressionEngine.get_config()
        current_params = default_config.get(algo, {})
        params = dict(current_params)

        if explore_type.startswith("L2"):
            param_defs = ALGORITHM_PARAMS.get(algo, [])
            if param_defs:
                mutable = [p for p in param_defs if p.key not in ("min_match",)]
                if mutable:
                    pick = random.choice(mutable)
                    old_val = params.get(pick.key, pick.default)
                    delta = int(old_val * random.uniform(-0.5, 0.5))
                    new_val = max(pick.min_val, min(pick.max_val, old_val + delta))
                    new_val = max(pick.step, new_val)
                    params[pick.key] = new_val
                    from gui.ade.explore_log import log_explore as _log_explore

                    _log_explore(
                        "l2_perturb",
                        algorithm=algo.value,
                        param=pick.key,
                        old=old_val,
                        new=new_val,
                        level=logging.DEBUG,
                    )

        return params

    def _effective_epsilon(self, n_total: int, cluster_id: int) -> float:
        f_confidence = max(0.08, 1.0 - math.sqrt(n_total / max(1, self.warmup_samples * 4)))

        n_clusters = self.cluster_space.n_clusters
        f_novelty = 1.0
        if n_clusters > 0 and n_total > 0:
            avg_per_cluster = n_total / n_clusters
            cluster_stats = self.cluster_stats.get(cluster_id, {})
            cluster_count = sum(s.count for s in cluster_stats.values())
            if cluster_count < avg_per_cluster * 0.3:
                f_novelty = 1.5

        total_time = self._total_compress_time + self._total_explore_time
        if total_time > 0:
            remaining = max(0, self.budget_ratio * self._total_compress_time - self._total_explore_time)
            f_budget = min(1.0, remaining / max(1, self._total_compress_time * 0.1))
        else:
            f_budget = 1.0

        result = self.epsilon_base * f_confidence * f_novelty * f_budget
        return min(0.60, max(0.02, result))

    def _check_budget(self, compress_time_ms: float) -> bool:
        if compress_time_ms <= 0:
            return True
        self._total_compress_time += compress_time_ms
        allowed = self._total_compress_time * self.budget_ratio
        return self._total_explore_time < allowed

    def _execute_explore_async(
        self,
        record: FileRecord,
        target_algo: AlgorithmType,
        target_params: dict[str, int],
        explore_type: str,
        parent_decision: str,
        cluster_id: int,
        ucb_gap: float,
    ) -> None:
        from gui.ade.explore_log import log_explore, summarize_stream_result

        self._active_threads += 1
        t_start = time.perf_counter()
        success = False
        job_id = ""
        file_name = getattr(record, "name", "") or ""

        try:
            log_explore(
                "async_start",
                file=file_name,
                target=target_algo.value,
                explore_type=explore_type,
                cluster_id=cluster_id,
                parent=parent_decision,
            )
            if SilentExplorer.user_operations_active():
                log_explore("async_skip", reason="user_ops_active", file=file_name)
                return
            from gui.engine.compressor import CompressionEngine
            from gui.ade.training import get_training_store, TrainingSampleV3

            if not record.raw_data:
                record.load_raw_data()

            if not record.raw_data:
                log_explore("async_skip", reason="no_raw_data", file=file_name)
                return

            engine = CompressionEngine()
            if not engine.available:
                log_explore("async_skip", reason="engine_unavailable", file=file_name)
                return

            original_config = CompressionEngine.get_config()
            algo_config = dict(original_config.get(target_algo, {}))
            algo_config.update(target_params)
            full_config = dict(original_config)
            full_config[target_algo] = algo_config

            from gui.ade.checkpoint import new_job_id
            from gui.ade.streaming_explore import run_explore_stream_with_optional_cancel

            job_id = new_job_id("silent")
            log_explore(
                "stream_dispatch",
                job_id=job_id,
                file=file_name,
                algorithm=target_algo.value,
                params=target_params,
            )

            try:
                CompressionEngine.set_config(full_config, save=False)
                stream_res = run_explore_stream_with_optional_cancel(
                    record,
                    target_algo,
                    job_id=job_id,
                    cancel_after_bytes=0,
                    cancel_reason="user_irq",
                    dispatch_meta={
                        "cluster_id": cluster_id,
                        "explore_type": explore_type,
                        "parent_decision": parent_decision,
                        "ucb_gap": ucb_gap,
                    },
                    explore_config_snapshot={target_algo.value: target_params},
                )
            finally:
                CompressionEngine.set_config(original_config, save=False)

            elapsed_ms = (time.perf_counter() - t_start) * 1000

            if stream_res.cancelled:
                log_explore(
                    "stream_cancelled",
                    job_id=job_id,
                    file=file_name,
                    **summarize_stream_result(stream_res),
                )
                return

            if not stream_res.success:
                log_explore(
                    "stream_failed",
                    job_id=job_id,
                    file=file_name,
                    **summarize_stream_result(stream_res),
                )
                return

            store = get_training_store()
            sample = TrainingSampleV3.from_file_record(record, None)
            sample.is_exploration = True
            sample.exploration_type = explore_type
            sample.exploration_target = target_algo.value
            sample.parent_decision = parent_decision
            sample.ucb_gap_at_time = round(ucb_gap, 6)
            sample.cluster_id = cluster_id
            sample.algorithm_used = target_algo.value
            sample.algorithm_id = list(AlgorithmType).index(target_algo) if target_algo in list(AlgorithmType) else 0
            sample.params_used = target_params
            sample.compression_ratio = stream_res.compression_ratio
            sample.compression_time_ms = elapsed_ms
            sample.output_size_bytes = stream_res.payload_bytes
            sample.success = True

            greedy_ratio = float(getattr(record, "compression_ratio", 1.0) or 1.0)
            is_discovery = sample.compression_ratio < greedy_ratio
            if is_discovery:
                self.discovery_count += 1
                improve_pct = (
                    (greedy_ratio - sample.compression_ratio) / greedy_ratio * 100
                    if greedy_ratio > 0
                    else 0.0
                )
                log_explore(
                    "discovery",
                    job_id=job_id,
                    file=file_name,
                    target=target_algo.value,
                    explore_ratio=round(sample.compression_ratio, 6),
                    greedy_ratio=round(greedy_ratio, 6),
                    improve_pct=round(improve_pct, 2),
                )

            added = store.add_sample(sample)
            log_explore(
                "sample_saved",
                job_id=job_id,
                file=file_name,
                sample_id=sample.sample_id,
                added=added,
                algorithm=target_algo.value,
                explore_type=explore_type,
                ratio=round(sample.compression_ratio, 6),
                payload_bytes=sample.output_size_bytes,
                elapsed_ms=round(elapsed_ms, 2),
            )

            self._record_arm_result(cluster_id, target_algo, stream_res.compression_ratio)
            success = True
            self.explore_count += 1
            end_fields = summarize_stream_result(stream_res)
            end_fields.pop("job_id", None)
            end_fields.pop("success", None)
            log_explore(
                "async_done",
                job_id=job_id,
                file=file_name,
                success=True,
                **end_fields,
            )

        except Exception as e:
            log_explore(
                "async_error",
                job_id=job_id or None,
                file=file_name,
                error=str(e),
                level=logging.WARNING,
            )
            logger.debug("[explorer] async failed: %s", e)
        finally:
            self._active_threads -= 1
            elapsed_ms = (time.perf_counter() - t_start) * 1000
            self._total_explore_time += elapsed_ms / 1000.0
            if not success:
                log_explore(
                    "async_done",
                    job_id=job_id or None,
                    file=file_name,
                    success=False,
                    elapsed_ms=round(elapsed_ms, 2),
                    level=logging.DEBUG,
                )
            try:
                from gui.utils.logging import flush_logging

                flush_logging()
            except Exception:
                pass

    def _record_arm_result(
        self, cluster_id: int, algo: AlgorithmType, ratio: float
    ) -> None:
        if cluster_id not in self.cluster_stats:
            self.cluster_stats[cluster_id] = {}

        if algo not in self.cluster_stats[cluster_id]:
            self.cluster_stats[cluster_id][algo] = ArmStats()

        s = self.cluster_stats[cluster_id][algo]
        s.count += 1
        s.sum_ratio += ratio
        s.mean_ratio = s.sum_ratio / s.count
        s.min_ratio = min(s.min_ratio, ratio)
        self.total_samples += 1

    def get_stats(self) -> dict:
        return {
            "enabled": self.enabled,
            "total_samples": self.total_samples,
            "explore_count": self.explore_count,
            "discovery_count": self.discovery_count,
            "n_clusters": self.cluster_space.n_clusters,
            "active_threads": self._active_threads,
            "total_compress_time_s": round(self._total_compress_time, 1),
            "total_explore_time_s": round(self._total_explore_time, 1),
            "explore_rate": round(self.explore_count / max(1, self.total_samples), 3),
            "discovery_rate": round(self.discovery_count / max(1, self.explore_count), 3),
            "clusters_summary": {
                cid: {a.value: {"count": s.count, "mean": round(s.mean_ratio, 4)}
                 for a, s in stats.items()}
                for cid, stats in self.cluster_stats.items()
            },
        }
