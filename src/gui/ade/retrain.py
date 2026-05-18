"""ADE supplemental training: ingest JSONL samples and retrain on threshold."""

from __future__ import annotations

import logging
from typing import Any

from gui.ade.params import ParameterRegressor
from gui.ade.training import TrainingSampleV3, get_training_store
from gui.config.settings import get_ade_retrain_min_new_samples

logger = logging.getLogger(__name__)


def ingest_v3_into_param_regressor(
    regressor: ParameterRegressor | None = None,
    *,
    samples: list[TrainingSampleV3] | None = None,
    clear_existing: bool = True,
) -> dict[str, int]:
    """
    Load V3 JSONL rows into the param regressor.

    Each row must carry ``algorithm_used`` / ``algorithm_id`` (arm label) and
    ``params_used`` (algorithm-specific knobs) plus ``compression_ratio`` (reward).
    """
    reg = regressor or ParameterRegressor.get()
    if clear_existing:
        reg.clear_ingested_samples()

    if samples is None:
        samples = get_training_store().load(validate=True)

    counts: dict[str, int] = {"ingested": 0, "skipped": 0, "by_algorithm": {}}
    for sample in samples:
        if reg.ingest_v3_sample(sample):
            counts["ingested"] += 1
            algo = sample.algorithm_used or "?"
            counts["by_algorithm"][algo] = counts["by_algorithm"].get(algo, 0) + 1
        else:
            counts["skipped"] += 1
    return counts


def maybe_supplemental_retrain(
    min_new_samples: int | None = None,
) -> dict[str, Any] | None:
    """
    When persisted JSONL grew by >= ``min_new_samples`` since last retrain,
    reload V3 into the param regressor and run a supplemental training pass.
    """
    threshold = (
        int(min_new_samples)
        if min_new_samples is not None
        else get_ade_retrain_min_new_samples()
    )
    store = get_training_store()
    stats = store.get_stats()
    total = int(stats.get("total_samples", 0) or 0)
    last = int(stats.get("last_retrain_total_samples", 0) or 0)
    new_count = total - last

    if new_count < threshold:
        logger.debug(
            "[retrain] skip supplemental: new=%d threshold=%d total=%d",
            new_count,
            threshold,
            total,
        )
        return None

    ingest_stats = ingest_v3_into_param_regressor(clear_existing=True)
    regressor = ParameterRegressor.get()
    per_algo = regressor.count_by_algorithm()

    try:
        metrics = regressor.train(epochs=30, lr=0.001, batch_size=16)
        regressor.save_model()
    except Exception as e:
        logger.warning("[retrain] supplemental train failed: %s", e)
        return None

    stats["last_retrain_total_samples"] = total
    stats["last_retrain_time"] = __import__("time").time()
    store._stats.update(stats)
    store._save_stats()

    result = {
        "new_samples_since_last": new_count,
        "ingest": ingest_stats,
        "per_algorithm_ingested": per_algo,
        "metrics": metrics,
    }
    logger.info("[retrain] supplemental retrain complete: %s", result)
    return result
