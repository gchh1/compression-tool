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
import random
import threading
import time
import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

from gui.core.models import AlgorithmType, FileRecord


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

    DEFAULT_CONFIG = {
        "enabled": True,
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
        cfg = {**self.DEFAULT_CONFIG, **(config or {})}
        self.enabled: bool = cfg["enabled"]
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
        if not self.enabled:
            return False

        if not isinstance(record, FileRecord):
            return False

        if record.base_features is None:
            return False

        if record.size < self.min_file_size:
            return False

        if greedy_algo == AlgorithmType.NONE or greedy_algo == AlgorithmType.SKIP:
            return False

        cluster_id = self.cluster_space.assign(record.base_features.vector)

        should, target_algo, explore_type = self._should_explore(
            cluster_id, greedy_algo, record.size
        )

        if not should:
            self._record_arm_result(cluster_id, greedy_algo, record.compression_ratio)
            return False

        if self._active_threads >= self.max_concurrent:
            logger.debug("[explorer] max concurrent reached (%d), skip", self.max_concurrent)
            return False

        budget_ok = self._check_budget(compress_time_ms)
        if not budget_ok:
            return False

        target_params = self._pick_explore_params(explore_type, target_algo, record)
        parent_decision = greedy_algo.value

        ucb_gap = self._compute_ucb_gap(cluster_id, greedy_algo)

        thread = threading.Thread(
            target=self._execute_explore_async,
            args=(record, target_algo, target_params, explore_type,
                  parent_decision, cluster_id, ucb_gap),
            daemon=True,
        )
        thread.start()

        logger.info("[explorer] launched %s -> %s (cluster=%d, type=%s)",
                    parent_decision, target_algo.value, cluster_id, explore_type)
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
            if algo == AlgorithmType.AUTO or algo == AlgorithmType.SKIP:
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
            if algo != exclude and algo not in (AlgorithmType.AUTO, AlgorithmType.SKIP, AlgorithmType.NONE, AlgorithmType.TRANSFORMER)
        ]
        if not candidates:
            all_algos = [a for a in AlgorithmType
                        if a not in (AlgorithmType.AUTO, AlgorithmType.SKIP, AlgorithmType.NONE, AlgorithmType.TRANSFORMER)]
            random.shuffle(all_algos)
            return all_algos[0] if all_algos else None
        candidates.sort(key=lambda x: x[1])
        return candidates[0][0]

    def _pick_explore_params(
        self, explore_type: str, algo: AlgorithmType, record: FileRecord
    ) -> dict[str, int]:
        from gui.core.engine import CompressionEngine
        from gui.core.models import ALGORITHM_PARAMS

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
                    logger.debug("[explorer] L2 perturb: %s.%s %d->%d",
                                algo.value, pick.key, old_val, new_val)

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
        self._active_threads += 1
        t_start = time.perf_counter()
        success = False

        try:
            from gui.core.engine import CompressionEngine
            from gui.core.training_store import get_training_store, TrainingSampleV3

            if not record.raw_data:
                record.load_raw_data()

            if not record.raw_data:
                return

            engine = CompressionEngine()
            if not engine.available:
                return

            original_config = CompressionEngine.get_config()
            algo_config = dict(original_config.get(target_algo, {}))
            algo_config.update(target_params)
            full_config = dict(original_config)
            full_config[target_algo] = algo_config

            try:
                CompressionEngine.set_config(full_config, save=False)
                result = engine.compress(record.raw_data, target_algo)
            finally:
                CompressionEngine.set_config(original_config, save=False)

            elapsed_ms = (time.perf_counter() - t_start) * 1000

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
            sample.compression_ratio = result.compression_ratio
            sample.compression_time_ms = elapsed_ms
            sample.output_size_bytes = result.compressed_size
            sample.success = result.success

            if sample.compression_ratio < record.compression_ratio:
                self.discovery_count += 1
                logger.info("[explorer] DISCOVERY! %s %.4f vs greedy %.4f (+%.1f%%)",
                            target_algo.value, sample.compression_ratio,
                            record.compression_ratio,
                            (record.compression_ratio - sample.compression_ratio) / record.compression_ratio * 100)

            store.add_sample(sample)

            self._record_arm_result(cluster_id, target_algo, result.compression_ratio)
            success = True
            self.explore_count += 1

        except Exception as e:
            logger.debug("[explorer] async failed: %s", e)
        finally:
            self._active_threads -= 1
            elapsed_ms = (time.perf_counter() - t_start) * 1000
            self._total_explore_time += elapsed_ms / 1000.0

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
