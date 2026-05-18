from __future__ import annotations

import json
import logging
import sys
from pathlib import Path

from gui.config.theme import ThemeManager, DEFAULT_THEME, THEME_FIELDS, CHART_FIELDS
from gui.models import (
    AlgorithmType,
    ALGORITHM_PARAMS,
    STREAMING_THRESHOLD_MB,
    STREAMING_CHUNK_SIZE_KB,
    LZDP_DP_VIZ_MAX_SIZE,
    get_default_config,
)

logger = logging.getLogger(__name__)

CONFIG_FILENAME = "webcompress_settings.json"


def _streaming_per_algorithm_defaults() -> dict[str, dict]:
    """Defaults for per-algorithm streaming overrides (see streaming-workspace-spec)."""
    chunk = STREAMING_CHUNK_SIZE_KB
    keys = ("deflate", "lzss", "lzdp", "dpflate", "gzip", "brotli", "zstd")
    out: dict[str, dict] = {}
    thr = float(STREAMING_THRESHOLD_MB)
    for k in keys:
        out[k] = {
            "follow_global_chunk": True,
            "chunk_size_kb": chunk,
            "follow_global_threshold": True,
            "threshold_mb": thr,
        }
    return out


DEFAULTS = {
    "version": 1,
    "algorithms": {},
    "ade": {
        # 静默探索：后台线程压缩；默认关；与「用户压缩/解压」通过 user-op 深度与引擎锁解耦
        "silent_explore_enabled": False,
        # JSONL 新增有效样本达到该数后触发 NN 参数回归补充训练（与 bandit warmup 无关）
        "retrain_min_new_samples": 50,
        # 静默探索专用 I/O（与用户主路径 streaming.* 独立；小文件走内存压测，不强制分块读盘）
        "explore_streaming_threshold_mb": float(STREAMING_THRESHOLD_MB),
        "explore_streaming_chunk_size_kb": int(STREAMING_CHUNK_SIZE_KB),
        # SilentExplorer AC-UCB tunables (editable in 决策引擎管理 → 配置)
        "explorer": {
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
        },
    },
    "streaming": {
        "threshold_mb": STREAMING_THRESHOLD_MB,
        "chunk_size_kb": STREAMING_CHUNK_SIZE_KB,
        "workspace_root": "",
        "per_algorithm": _streaming_per_algorithm_defaults(),
    },
    "visualization": {
        "lzdp_dp_viz_max_size": LZDP_DP_VIZ_MAX_SIZE,
    },
    "theme": DEFAULT_THEME.to_dict(),
}

for algo, params in ALGORITHM_PARAMS.items():
    DEFAULTS["algorithms"][algo.value] = {p.key: p.default for p in params}


def _get_config_path() -> Path:
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).parent.parent / "config"
    else:
        repo = Path(__file__).resolve().parent.parent.parent.parent
        # Dev runs often use ``Package/bin`` layout; share the same JSON as the shipped app.
        pkg_cfg = repo / "Package" / "config"
        if (pkg_cfg / CONFIG_FILENAME).is_file():
            base = pkg_cfg
        else:
            base = repo / "config"
    base.mkdir(parents=True, exist_ok=True)
    return base / CONFIG_FILENAME


def get_defaults() -> dict:
    return json.loads(json.dumps(DEFAULTS))


def load_config() -> dict:
    config_path = _get_config_path()
    if not config_path.exists():
        logger.info("[config] no config file at %s, using defaults", config_path)
        return get_defaults()
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        merged = _merge_with_defaults(data)
        if "theme" not in data or not isinstance(data.get("theme"), dict):
            logger.info("[config] theme missing in config file, saving defaults")
            save_config(merged)
        logger.debug("[config] loaded from %s", config_path)
        return merged
    except (json.JSONDecodeError, OSError) as e:
        logger.warning("[config] failed to load %s: %s, using defaults", config_path, e)
        return get_defaults()


def save_config(config: dict) -> None:
    config_path = _get_config_path()
    try:
        config_path.parent.mkdir(parents=True, exist_ok=True)
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        logger.info("[config] saved to %s", config_path)
    except OSError as e:
        logger.error("[config] failed to save %s: %s", config_path, e)


def _merge_with_defaults(user_data: dict) -> dict:
    result = get_defaults()
    if "algorithms" in user_data and isinstance(user_data["algorithms"], dict):
        for algo_name, algo_params in user_data["algorithms"].items():
            if algo_name in result["algorithms"] and isinstance(algo_params, dict):
                result["algorithms"][algo_name].update(algo_params)
    if "streaming" in user_data and isinstance(user_data["streaming"], dict):
        u_s = user_data["streaming"]
        r_s = result["streaming"]
        for k, v in u_s.items():
            if k == "per_algorithm" and isinstance(v, dict):
                base_pa = r_s.setdefault(
                    "per_algorithm", _streaming_per_algorithm_defaults()
                )
                for ak, sub in v.items():
                    if isinstance(sub, dict):
                        slot = base_pa.setdefault(ak, {})
                        slot.update(sub)
            else:
                r_s[k] = v
    if "visualization" in user_data and isinstance(user_data["visualization"], dict):
        result["visualization"].update(user_data["visualization"])
    if "ade" in user_data and isinstance(user_data["ade"], dict):
        result.setdefault("ade", {})
        for k, v in user_data["ade"].items():
            if k == "silent_explore_enabled":
                result["ade"][k] = bool(v)
            elif k == "explore_streaming_threshold_mb":
                try:
                    result["ade"][k] = max(0.0, float(v))
                except (TypeError, ValueError):
                    pass
            elif k == "explore_streaming_chunk_size_kb":
                try:
                    result["ade"][k] = max(64, int(v))
                except (TypeError, ValueError):
                    pass
            elif k == "explorer" and isinstance(v, dict):
                slot = result["ade"].setdefault(
                    "explorer", json.loads(json.dumps(DEFAULTS["ade"]["explorer"]))
                )
                slot.update(v)
            else:
                result["ade"][k] = v
    if "theme" in user_data and isinstance(user_data["theme"], dict):
        for key, val in user_data["theme"].items():
            if key in result["theme"]:
                result["theme"][key] = str(val)
    return result


def get_silent_explore_enabled(config: dict | None = None) -> bool:
    """Persisted switch for ADE silent exploration (background compress samples)."""
    if config is None:
        config = load_config()
    ade = config.get("ade")
    if not isinstance(ade, dict):
        return False
    return bool(ade.get("silent_explore_enabled", False))


def get_ade_explore_streaming_threshold_mb(config: dict | None = None) -> float:
    """File size above this (MB) uses file-to-file chunked compress during silent explore."""
    if config is None:
        config = load_config()
    ade = config.get("ade")
    if not isinstance(ade, dict):
        return float(DEFAULTS["ade"]["explore_streaming_threshold_mb"])
    try:
        return max(0.0, float(ade.get(
            "explore_streaming_threshold_mb",
            DEFAULTS["ade"]["explore_streaming_threshold_mb"],
        )))
    except (TypeError, ValueError):
        return float(STREAMING_THRESHOLD_MB)


def get_ade_explore_streaming_chunk_kb(config: dict | None = None) -> int:
    """Read/chunk size (KB) for silent explore streaming path only."""
    if config is None:
        config = load_config()
    ade = config.get("ade")
    if not isinstance(ade, dict):
        return int(DEFAULTS["ade"]["explore_streaming_chunk_size_kb"])
    try:
        return max(64, int(ade.get(
            "explore_streaming_chunk_size_kb",
            DEFAULTS["ade"]["explore_streaming_chunk_size_kb"],
        )))
    except (TypeError, ValueError):
        return int(STREAMING_CHUNK_SIZE_KB)


def get_ade_explorer_tunables(config: dict | None = None) -> dict:
    """Merged SilentExplorer tunables from defaults + ``ade.explorer`` in config."""
    from gui.ade.explorer import SilentExplorer

    out: dict = dict(SilentExplorer.DEFAULT_TUNABLES)
    if config is None:
        config = load_config()
    raw = config.get("ade", {}).get("explorer") if isinstance(config.get("ade"), dict) else None
    if isinstance(raw, dict):
        for key in out:
            if key in raw:
                out[key] = raw[key]
    try:
        out["epsilon_base"] = max(0.02, min(0.60, float(out["epsilon_base"])))
        out["alpha_ucb"] = max(0.1, min(5.0, float(out["alpha_ucb"])))
        out["max_concurrent"] = max(1, min(8, int(out["max_concurrent"])))
        out["timeout_seconds"] = max(1.0, min(600.0, float(out["timeout_seconds"])))
        out["budget_ratio"] = max(0.05, min(1.0, float(out["budget_ratio"])))
        out["min_file_size_bytes"] = max(0, int(out["min_file_size_bytes"]))
        out["max_file_size_for_l2"] = max(1024, int(out["max_file_size_for_l2"]))
        out["warmup_samples"] = max(1, int(out["warmup_samples"]))
        out["ucb_gap_threshold"] = max(0.0, min(1.0, float(out["ucb_gap_threshold"])))
        out["l1_min_samples"] = max(1, int(out["l1_min_samples"]))
        out["l2_min_samples"] = max(1, int(out["l2_min_samples"]))
    except (TypeError, ValueError):
        pass
    return out


def get_ade_retrain_min_new_samples(config: dict | None = None) -> int:
    """Min new JSONL samples before supplemental param-regressor retrain."""
    if config is None:
        config = load_config()
    ade = config.get("ade")
    if not isinstance(ade, dict):
        return int(DEFAULTS["ade"]["retrain_min_new_samples"])
    try:
        return max(1, int(ade.get("retrain_min_new_samples", 50)))
    except (TypeError, ValueError):
        return 50


def get_theme_config(config: dict | None = None) -> dict[str, str]:
    if config is None:
        config = load_config()
    return config.get("theme", DEFAULT_THEME.to_dict())


def apply_theme(config: dict | None = None) -> None:
    theme_dict = get_theme_config(config)
    theme = ThemeManager.from_dict(theme_dict)
    ThemeManager.apply(theme)


def save_theme(theme_dict: dict[str, str]) -> None:
    full = load_config()
    full["theme"] = theme_dict
    save_config(full)


def get_algo_config(config: dict | None = None) -> dict[AlgorithmType, dict[str, int]]:
    if config is None:
        config = load_config()
    result: dict[AlgorithmType, dict[str, int]] = {}
    for algo_str, params in config.get("algorithms", {}).items():
        try:
            algo_type = AlgorithmType(algo_str)
            normalized = {k: int(v) for k, v in params.items()}
            if algo_type == AlgorithmType.LZDP and "dp_depth" in normalized:
                # Historical key migration: dp_depth -> dp_top
                normalized["dp_top"] = normalized.pop("dp_depth")
            if algo_type == AlgorithmType.DPFLATE and "dp_depth" in normalized:
                # Historical key migration: dp_depth -> dp_sub_match_max
                normalized["dp_sub_match_max"] = normalized.pop("dp_depth")
            if algo_type == AlgorithmType.DEFLATE:
                _def_ok = frozenset(
                    {
                        "search_size",
                        "lookahead_size",
                        "min_match",
                        "max_chain_length",
                        "use_flag_encoding",
                        "use_3hfmtree",
                        "huffman_offset_chunk_bits",
                        "huffman_length_chunk_bits",
                    }
                )
                filtered = {k: v for k, v in normalized.items() if k in _def_ok}
                base = get_default_config().get(algo_type, {})
                normalized = {**base, **filtered}
            result[algo_type] = normalized
        except (ValueError, TypeError):
            continue
    return result


def get_streaming_threshold(config: dict | None = None) -> float:
    if config is None:
        config = load_config()
    raw = float(config.get("streaming", {}).get("threshold_mb", STREAMING_THRESHOLD_MB))
    return max(0.0, raw)


def get_streaming_chunk_size(config: dict | None = None) -> int:
    if config is None:
        config = load_config()
    return int(config.get("streaming", {}).get("chunk_size_kb", STREAMING_CHUNK_SIZE_KB))


def get_streaming_workspace_root_override(config: dict | None = None) -> str | None:
    """Non-empty ``streaming.workspace_root`` in config; ``None`` means use default layout."""
    if config is None:
        config = load_config()
    raw = config.get("streaming", {}).get("workspace_root", "")
    if raw is None:
        return None
    s = str(raw).strip()
    return s if s else None


def get_streaming_per_algorithm(config: dict | None = None) -> dict[str, dict]:
    if config is None:
        config = load_config()
    pa = config.get("streaming", {}).get("per_algorithm")
    if not isinstance(pa, dict):
        return _streaming_per_algorithm_defaults()
    merged = _streaming_per_algorithm_defaults()
    for k, sub in pa.items():
        if isinstance(sub, dict) and k in merged:
            merged[k].update(sub)
        elif isinstance(sub, dict):
            merged[k] = dict(sub)
    # DPFlate: only global spill path is supported; drop legacy config key if present.
    dp = merged.get("dpflate")
    if isinstance(dp, dict):
        dp.pop("streaming_mode", None)
    lz = merged.get("lzdp")
    if isinstance(lz, dict):
        lz.pop("streaming_mode", None)
    return merged


def get_effective_streaming_chunk_kb(
    algorithm: AlgorithmType | None, config: dict | None = None
) -> int:
    """Effective read/chunk KB for pipeline: per-algorithm override or global ``chunk_size_kb``."""
    if config is None:
        config = load_config()
    key = algorithm.value if algorithm is not None else "deflate"
    sub = get_streaming_per_algorithm(config).get(key, {})
    if bool(sub.get("follow_global_chunk", True)):
        return int(config.get("streaming", {}).get("chunk_size_kb", STREAMING_CHUNK_SIZE_KB))
    return int(sub.get("chunk_size_kb", STREAMING_CHUNK_SIZE_KB))


def get_effective_streaming_threshold_mb(
    algorithm: AlgorithmType | None, config: dict | None = None
) -> float:
    """When to switch to streaming path: per-algorithm override or global ``threshold_mb``."""
    if config is None:
        config = load_config()
    if algorithm is None:
        raw = float(config.get("streaming", {}).get("threshold_mb", STREAMING_THRESHOLD_MB))
        return max(0.0, raw)
    key = algorithm.value
    sub = get_streaming_per_algorithm(config).get(key, {})
    if bool(sub.get("follow_global_threshold", True)):
        raw = float(config.get("streaming", {}).get("threshold_mb", STREAMING_THRESHOLD_MB))
        return max(0.0, raw)
    raw = float(sub.get("threshold_mb", STREAMING_THRESHOLD_MB))
    return max(0.0, raw)
