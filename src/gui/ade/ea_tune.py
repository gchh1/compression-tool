"""Stage2b EA: JSONL-derived param priors + settings helpers."""

from __future__ import annotations

import json
import logging
import statistics
import time
from pathlib import Path
from typing import Any

from gui.ade.training import TrainingSampleV3, algorithm_type_from_v3, get_training_store
from gui.models import AlgorithmType

logger = logging.getLogger(__name__)

PRIORS_VERSION = "1.0"
MIN_PRIOR_SAMPLES_PER_ALGO = 5
TOP_QUANTILE = 0.25

_PARAM_KEYS = (
    "window_size",
    "search_size",
    "min_match",
    "max_chain_length",
    "lookahead_size",
    "dp_range",
    "dp_top",
    "dp_sub_match_max",
)

_priors_cache: dict[str, Any] | None = None


def resolve_ea_priors_path() -> Path:
    return get_training_store()._base_dir / "ea_param_priors.json"


def _normalize_params_for_algo(algorithm: AlgorithmType, raw: dict[str, int]) -> dict[str, int]:
    out: dict[str, int] = {}
    for k, v in raw.items():
        try:
            out[k] = int(v)
        except (TypeError, ValueError):
            continue
    if "window_size" in out and "search_size" not in out:
        if algorithm in (
            AlgorithmType.LZSS,
            AlgorithmType.LZDP,
            AlgorithmType.DPFLATE,
            AlgorithmType.DEFLATE,
        ):
            out["search_size"] = out["window_size"]
    if "dp_range" in out:
        dr = int(out["dp_range"])
        if algorithm == AlgorithmType.LZDP and "dp_top" not in out:
            out["dp_top"] = dr
        if algorithm == AlgorithmType.DPFLATE and "dp_sub_match_max" not in out:
            out["dp_sub_match_max"] = dr
    return out


def build_ea_priors_from_jsonl(
    *,
    top_quantile: float = TOP_QUANTILE,
    min_per_algo: int = MIN_PRIOR_SAMPLES_PER_ALGO,
) -> dict[str, Any]:
    """
    Build per-algorithm median params from best-compression JSONL rows.

    EA uses these as fallback targets when live compress evaluation is unavailable.
    """
    samples = get_training_store().load(validate=True)
    by_algo: dict[str, list[tuple[float, dict[str, int]]]] = {}

    for sample in samples:
        if not sample.success or not sample.params_used:
            continue
        algo = algorithm_type_from_v3(sample)
        if algo is None or algo == AlgorithmType.NONE:
            continue
        ratio = float(sample.compression_ratio)
        if ratio <= 0:
            continue
        params = _normalize_params_for_algo(algo, sample.params_used)
        by_algo.setdefault(algo.value, []).append((ratio, params))

    priors: dict[str, Any] = {}
    for algo_name, rows in by_algo.items():
        if len(rows) < min_per_algo:
            continue
        rows.sort(key=lambda x: x[0])
        cut = max(1, int(len(rows) * top_quantile))
        best = [p for _r, p in rows[:cut]]
        merged: dict[str, list[int]] = {}
        for p in best:
            for k, v in p.items():
                if k in _PARAM_KEYS or k in ("use_flag_encoding", "use_3hfmtree"):
                    merged.setdefault(k, []).append(int(v))
        if not merged:
            continue
        median_params = {k: int(statistics.median(vs)) for k, vs in merged.items() if vs}
        priors[algo_name] = {
            "params": median_params,
            "sample_count": len(rows),
            "best_quantile_count": len(best),
            "median_ratio": statistics.median([r for r, _ in rows[:cut]]),
        }

    payload = {
        "version": PRIORS_VERSION,
        "built_at": time.time(),
        "top_quantile": top_quantile,
        "by_algorithm": priors,
    }
    path = resolve_ea_priors_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    global _priors_cache
    _priors_cache = payload
    logger.info("[ea_tune] wrote priors for %d algorithms to %s", len(priors), path)
    return payload


def load_ea_priors(*, force_reload: bool = False) -> dict[str, Any]:
    global _priors_cache
    if _priors_cache is not None and not force_reload:
        return _priors_cache
    path = resolve_ea_priors_path()
    if not path.is_file():
        _priors_cache = {}
        return _priors_cache
    try:
        _priors_cache = json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:
        logger.warning("[ea_tune] failed to load priors: %s", e)
        _priors_cache = {}
    return _priors_cache


def get_prior_params(algorithm: AlgorithmType) -> dict[str, int] | None:
    data = load_ea_priors()
    entry = (data.get("by_algorithm") or {}).get(algorithm.value)
    if not entry:
        return None
    raw = entry.get("params") or {}
    return _normalize_params_for_algo(algorithm, raw)


def prior_fitness_penalty(algorithm: AlgorithmType, trial: dict[str, int]) -> float:
    """L1 distance to JSONL prior (0 = match); used when live compress is skipped."""
    prior = get_prior_params(algorithm)
    if not prior:
        return 0.5
    keys = set(prior) | set(trial)
    err = 0.0
    n = 0
    for k in keys:
        if k not in prior or k not in trial:
            continue
        pv, tv = int(prior[k]), int(trial[k])
        span = max(abs(pv), abs(tv), 1)
        err += abs(pv - tv) / span
        n += 1
    return err / max(n, 1)


def format_ea_priors_summary(payload: dict[str, Any]) -> str:
    by = payload.get("by_algorithm") or {}
    if not by:
        return "未生成任何算法的 EA 先验（样本不足或缺少 params_used）。"
    lines = [f"已校准 {len(by)} 个算法先验:"]
    for name, info in sorted(by.items()):
        med = info.get("median_ratio", 0)
        lines.append(
            f"  · {name}: {info.get('sample_count', 0)} 样本, "
            f"最优分位 {info.get('best_quantile_count', 0)}, 中位压缩率≈{med:.4f}"
        )
    lines.append(f"文件: {resolve_ea_priors_path()}")
    return "\n".join(lines)
