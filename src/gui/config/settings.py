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
)

logger = logging.getLogger(__name__)

CONFIG_FILENAME = "webcompress_settings.json"

DEFAULTS = {
    "version": 1,
    "algorithms": {},
    "streaming": {
        "threshold_mb": STREAMING_THRESHOLD_MB,
        "chunk_size_kb": STREAMING_CHUNK_SIZE_KB,
    },
    "visualization": {
        "lzdp_dp_viz_max_size": LZDP_DP_VIZ_MAX_SIZE,
    },
    "theme": DEFAULT_THEME.to_dict(),
}

for algo, params in ALGORITHM_PARAMS.items():
    DEFAULTS["algorithms"][algo.value] = {p.key: p.default for p in params}


def _get_config_path() -> Path:
    if getattr(sys, 'frozen', False):
        base = Path(sys.executable).parent.parent / "config"
    else:
        base = Path(__file__).resolve().parent.parent.parent.parent / "config"
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
        logger.info("[config] loaded from %s", config_path)
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
        result["streaming"].update(user_data["streaming"])
    if "visualization" in user_data and isinstance(user_data["visualization"], dict):
        result["visualization"].update(user_data["visualization"])
    if "theme" in user_data and isinstance(user_data["theme"], dict):
        for key, val in user_data["theme"].items():
            if key in result["theme"]:
                result["theme"][key] = str(val)
    return result


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
            result[algo_type] = normalized
        except (ValueError, TypeError):
            continue
    return result


def get_streaming_threshold(config: dict | None = None) -> float:
    if config is None:
        config = load_config()
    return float(config.get("streaming", {}).get("threshold_mb", STREAMING_THRESHOLD_MB))


def get_streaming_chunk_size(config: dict | None = None) -> int:
    if config is None:
        config = load_config()
    return int(config.get("streaming", {}).get("chunk_size_kb", STREAMING_CHUNK_SIZE_KB))


