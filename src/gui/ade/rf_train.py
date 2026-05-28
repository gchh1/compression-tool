"""
Train C++ ADE Random Forest from ``ade_training_v3.jsonl``.

Uses in-process ``core_engine.ADE().train()`` when available; falls back to
``train_ade_model`` CLI for older builds.
"""

from __future__ import annotations

import json
import logging
import random
import shutil
import subprocess
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from PyQt6.QtCore import QThread, pyqtSignal

from gui.ade.training import (
    FEATURE_DIM,
    TrainingSampleV3,
    algorithm_type_from_v3,
    get_training_store,
)
from gui.models import AlgorithmType

logger = logging.getLogger(__name__)

PADDED_FEATURE_DIM = 33
MIN_RF_SAMPLES = 30
MIN_RF_CLASSES = 2
DEFAULT_HOLDOUT_RATIO = 0.2

ADE_LABEL_NAMES: dict[int, str] = {
    0: "NONE",
    1: "DEFLATE",
    2: "LZSS",
    3: "LZDP",
    4: "DPFLATE",
    5: "GZIP",
    7: "BROTLI",
    8: "ZSTD",
    9: "SKIP",
    10: "FFMPEG_H264",
    11: "FFMPEG_H265",
    12: "OPENH264",
}

ALGORITHM_TYPE_TO_ADE_LABEL: dict[AlgorithmType, int] = {
    AlgorithmType.NONE: 0,
    AlgorithmType.DEFLATE: 1,
    AlgorithmType.LZSS: 2,
    AlgorithmType.LZDP: 3,
    AlgorithmType.DPFLATE: 4,
    AlgorithmType.GZIP: 5,
    AlgorithmType.BROTLI: 7,
    AlgorithmType.ZSTD: 8,
    AlgorithmType.FFMPEG_H264: 10,
    AlgorithmType.FFMPEG_H265: 11,
    AlgorithmType.OPENH264: 12,
}


def pad_features_for_rf(base_vector: list[float], target_dim: int = PADDED_FEATURE_DIM) -> list[float]:
    """Match C++ ``FeatureVectorV3::to_padded_array`` (base 20 + extension zeros)."""
    out = [0.0] * target_dim
    n = min(len(base_vector), target_dim)
    for i in range(n):
        out[i] = float(base_vector[i])
    return out


def v3_sample_to_ade_label(sample: TrainingSampleV3) -> int | None:
    algo = algorithm_type_from_v3(sample)
    if algo is None:
        return None
    label = ALGORITHM_TYPE_TO_ADE_LABEL.get(algo)
    if label is None or label == 0:
        return None
    return label


def build_rf_rows(
    samples: list[TrainingSampleV3],
    *,
    include_exploration: bool = True,
) -> list[tuple[list[float], int, float]]:
    rows: list[tuple[list[float], int, float]] = []
    for sample in samples:
        if not sample.success:
            continue
        if sample.is_exploration and not include_exploration:
            continue
        if len(sample.features_vector) != FEATURE_DIM:
            continue
        label = v3_sample_to_ade_label(sample)
        if label is None:
            continue
        feats = pad_features_for_rf(sample.features_vector)
        weight = float(sample.sample_weight)
        if sample.is_exploration:
            weight *= 0.85
        if weight <= 0:
            continue
        rows.append((feats, label, weight))
    return rows


def count_rf_ready_samples() -> int:
    store = get_training_store()
    samples = store.load(validate=True)
    return len(build_rf_rows(samples))


def resolve_rf_model_output_path() -> Path:
    return get_training_store()._base_dir / "default_model.bin"


def _find_train_cli() -> Path | None:
    root = Path(__file__).resolve().parent.parent.parent.parent
    candidates = [
        root / "build_py" / "bin" / "train_ade_model.exe",
        root / "build_debug" / "bin" / "train_ade_model.exe",
        root / "build" / "bin" / "train_ade_model.exe",
    ]
    for path in candidates:
        if path.is_file():
            return path
    return None


def _export_training_json(rows: list[tuple[list[float], int, float]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": "3.0",
        "feature_count": PADDED_FEATURE_DIM,
        "sample_count": len(rows),
        "samples": [
            {"features": feats, "label": label, "weight": weight}
            for feats, label, weight in rows
        ],
    }
    path.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")


def _make_rf_config(core_module: Any) -> Any:
    cfg = core_module.RandomForestConfig()
    cfg.num_trees = 100
    cfg.max_depth = 12
    cfg.min_samples_split = 5
    cfg.min_samples_leaf = 2
    cfg.max_features = 0
    cfg.random_seed = 42
    cfg.bootstrap = True
    cfg.bootstrap_ratio = 0.8
    return cfg


def _split_train_val(
    rows: list[tuple[list[float], int, float]],
    holdout_ratio: float,
    seed: int,
) -> tuple[list[tuple[list[float], int, float]], list[tuple[list[float], int, float]]]:
    if holdout_ratio <= 0 or len(rows) < MIN_RF_SAMPLES:
        return rows, []
    rng = random.Random(seed)
    shuffled = list(rows)
    rng.shuffle(shuffled)
    n_val = max(1, int(len(shuffled) * holdout_ratio))
    if n_val >= len(shuffled):
        n_val = max(1, len(shuffled) // 5)
    val = shuffled[:n_val]
    train = shuffled[n_val:]
    return train, val


def _accuracy(ade: Any, rows: list[tuple[list[float], int, float]]) -> float | None:
    if not rows or not hasattr(ade, "predict_padded"):
        return None
    correct = 0
    for feats, label, _w in rows:
        try:
            pred = int(ade.predict_padded(feats))
        except Exception:
            return None
        if pred == label:
            correct += 1
    return correct / len(rows)


def _train_via_ade(
    ade: Any,
    core_module: Any,
    rows: list[tuple[list[float], int, float]],
    output_path: Path,
) -> dict[str, Any]:
    train_rows, val_rows = _split_train_val(rows, DEFAULT_HOLDOUT_RATIO, seed=42)
    labels = {label for _f, label, _w in train_rows}
    if len(train_rows) < MIN_RF_SAMPLES:
        raise ValueError(f"有效训练样本不足（{len(train_rows)} < {MIN_RF_SAMPLES}）")
    if len(labels) < MIN_RF_CLASSES:
        raise ValueError(f"算法类别不足（{len(labels)} < {MIN_RF_CLASSES}）")

    config = _make_rf_config(core_module)
    t0 = time.perf_counter()
    ade.train(train_rows, config)
    train_ms = (time.perf_counter() - t0) * 1000.0

    output_path.parent.mkdir(parents=True, exist_ok=True)
    if not ade.save_model(str(output_path)):
        raise RuntimeError(f"保存模型失败: {output_path}")

    train_acc = _accuracy(ade, train_rows)
    val_acc = _accuracy(ade, val_rows) if val_rows else None

    return {
        "backend": "ade.train",
        "train_samples": len(train_rows),
        "val_samples": len(val_rows),
        "num_classes": len(labels),
        "class_distribution": dict(Counter(label for _f, label, _w in rows)),
        "train_ms": train_ms,
        "train_accuracy": train_acc,
        "val_accuracy": val_acc,
        "model_path": str(output_path),
    }


def _train_via_cli(
    rows: list[tuple[list[float], int, float]],
    output_path: Path,
) -> dict[str, Any]:
    cli = _find_train_cli()
    if cli is None:
        raise RuntimeError(
            "当前 core_engine 不支持 ADE.train，且未找到 train_ade_model.exe；"
            "请重新编译 Python 扩展（core_engine）或 ADE 训练工具。"
        )
    tmp_json = output_path.parent / "_rf_train_export.json"
    _export_training_json(rows, tmp_json)
    t0 = time.perf_counter()
    proc = subprocess.run(
        [str(cli), str(tmp_json), str(output_path)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    train_ms = (time.perf_counter() - t0) * 1000.0
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout or "").strip()
        raise RuntimeError(err or f"train_ade_model 退出码 {proc.returncode}")
    try:
        tmp_json.unlink(missing_ok=True)
    except OSError:
        pass
    labels = {label for _f, label, _w in rows}
    return {
        "backend": "train_ade_model.exe",
        "train_samples": len(rows),
        "val_samples": 0,
        "num_classes": len(labels),
        "class_distribution": dict(Counter(label for _f, label, _w in rows)),
        "train_ms": train_ms,
        "train_accuracy": None,
        "val_accuracy": None,
        "model_path": str(output_path),
    }


def train_rf_from_jsonl(
    *,
    output_path: Path | None = None,
    include_exploration: bool = True,
    backup_existing: bool = True,
) -> dict[str, Any]:
    """
    Load JSONL, train RF, write ``default_model.bin``.

    Returns summary dict for UI / logs.
    """
    store = get_training_store()
    samples = store.load(validate=True)
    rows = build_rf_rows(samples, include_exploration=include_exploration)
    if len(rows) < MIN_RF_SAMPLES:
        raise ValueError(
            f"可用于 RF 的样本不足：{len(rows)} 条（至少需要 {MIN_RF_SAMPLES} 条，"
            "且需含有效算法标签与 20 维特征）"
        )
    labels = {label for _f, label, _w in rows}
    if len(labels) < MIN_RF_CLASSES:
        raise ValueError(f"算法类别不足（{len(labels)} 类），请继续收集不同算法的压缩样本")

    out = output_path or resolve_rf_model_output_path()
    if backup_existing and out.is_file():
        stamp = time.strftime("%Y%m%d_%H%M%S")
        backup = out.with_suffix(out.suffix + f".bak-{stamp}")
        shutil.copy2(out, backup)
        logger.info("[rf_train] backed up model to %s", backup)

    from gui.engine.compressor import CompressionEngine

    engine = CompressionEngine()
    if not engine.available:
        raise RuntimeError("压缩引擎不可用，无法训练 RF")

    core = engine._engine
    ade = core.ADE()
    if hasattr(ade, "train"):
        result = _train_via_ade(ade, core, rows, out)
    else:
        result = _train_via_cli(rows, out)

    result["jsonl_total"] = len(samples)
    result["rf_rows"] = len(rows)
    _update_train_stats(result)
    logger.info("[rf_train] complete: %s", result)
    return result


def _update_train_stats(result: dict[str, Any]) -> None:
    store = get_training_store()
    store._stats["last_rf_train_time"] = time.time()
    store._stats["last_rf_train_samples"] = int(result.get("rf_rows", 0))
    store._stats["last_rf_train_backend"] = result.get("backend", "")
    store._stats["last_rf_model_path"] = result.get("model_path", "")
    store._save_stats()


def format_train_summary(result: dict[str, Any]) -> str:
    dist = result.get("class_distribution") or {}
    lines = [
        f"后端: {result.get('backend', '?')}",
        f"JSONL 样本: {result.get('jsonl_total', '?')}",
        f"RF 训练行: {result.get('rf_rows', result.get('train_samples', '?'))}",
        f"训练耗时: {result.get('train_ms', 0):.0f} ms",
        f"模型路径:\n  {result.get('model_path', '')}",
    ]
    if result.get("train_accuracy") is not None:
        lines.append(f"训练集准确率: {result['train_accuracy'] * 100:.1f}%")
    if result.get("val_accuracy") is not None:
        lines.append(f"验证集准确率: {result['val_accuracy'] * 100:.1f}%")
    if dist:
        parts = []
        for label, count in sorted(dist.items(), key=lambda x: -x[1]):
            name = ADE_LABEL_NAMES.get(int(label), str(label))
            parts.append(f"{name}={count}")
        lines.append("类别分布: " + ", ".join(parts))
    return "\n".join(lines)


@dataclass
class RFTrainResult:
    ok: bool
    message: str
    summary: dict[str, Any] | None = None


class RFTrainWorker(QThread):
    """Background RF training for the decision-engine dialog."""

    finished_with_result = pyqtSignal(object)
    progress = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)

    def run(self) -> None:
        try:
            self.progress.emit("正在加载 JSONL 训练数据…")
            summary = train_rf_from_jsonl()
            self.progress.emit("正在加载新模型到决策引擎…")
            from gui.ade.engine import DecisionEngine

            reloaded = DecisionEngine.get().reload_rf_model()
            if not reloaded:
                logger.warning("[rf_train] model saved but reload failed")
            self.finished_with_result.emit(
                RFTrainResult(
                    ok=True,
                    message=format_train_summary(summary),
                    summary=summary,
                )
            )
        except Exception as e:
            logger.exception("[rf_train] training failed")
            self.finished_with_result.emit(
                RFTrainResult(ok=False, message=str(e), summary=None)
            )
