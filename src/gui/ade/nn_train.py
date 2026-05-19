"""Train Stage2a parameter regressor (C++ MLP in core_engine) from ``ade_training_v3.jsonl``."""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass
from typing import Any

from PyQt6.QtCore import QThread, pyqtSignal

from gui.ade.params import ParameterRegressor
from gui.ade.retrain import ingest_v3_into_param_regressor
from gui.ade.training import algorithm_type_from_v3, get_training_store

logger = logging.getLogger(__name__)

# C++ MLP trains from >=4 rows internally; more samples recommended.
MIN_NN_SAMPLES = 4
MIN_NN_SAMPLES_RECOMMENDED = 20


def count_nn_ready_samples(*, include_exploration: bool = True) -> int:
    """JSONL rows with algorithm label + ``params_used`` usable by the regressor."""
    samples = get_training_store().load(validate=True)
    n = 0
    for sample in samples:
        if not sample.success or not sample.params_used:
            continue
        if sample.is_exploration and not include_exploration:
            continue
        algo = algorithm_type_from_v3(sample)
        if algo is None or algo not in ParameterRegressor.ALGORITHM_MAP:
            continue
        n += 1
    return n


def describe_nn_sample_gap() -> str:
    """Human-readable reason when few rows qualify for param regression."""
    samples = get_training_store().load(validate=True)
    with_params = 0
    supported = 0
    unsupported_algo: dict[str, int] = {}
    for sample in samples:
        if not sample.success or not sample.params_used:
            continue
        with_params += 1
        algo = algorithm_type_from_v3(sample)
        if algo is None:
            unsupported_algo["(未知)"] = unsupported_algo.get("(未知)", 0) + 1
        elif algo not in ParameterRegressor.ALGORITHM_MAP:
            name = algo.value if hasattr(algo, "value") else str(algo)
            unsupported_algo[name] = unsupported_algo.get(name, 0) + 1
        else:
            supported += 1
    lines = [
        f"JSONL 中带 params_used 的成功样本: {with_params}",
        f"可用于 NN 训练 (DEFLATE/LZSS/LZDP/DPFLATE/BROTLI/ZSTD): {supported}",
    ]
    if unsupported_algo:
        detail = ", ".join(f"{k}={v}" for k, v in sorted(unsupported_algo.items()))
        lines.append(f"暂不支持参数回归的算法: {detail}")
    lines.append(
        "请用上述支持算法压缩并开启静默探索(L2)，确保样本写入 params_used。"
    )
    return "\n".join(lines)


def train_nn_from_jsonl(
    *,
    epochs: int = 50,
    lr: float = 0.001,
    batch_size: int = 32,
) -> dict[str, Any]:
    reg = ParameterRegressor.get()
    if not reg._has_cpp:
        raise RuntimeError(
            "C++ ParamRegressorNet 不可用，请重新编译 core_engine（Stage2a 在 C++ 端训练，不依赖 PyTorch）"
        )

    ingest_stats = ingest_v3_into_param_regressor(reg, clear_existing=True)
    if ingest_stats["ingested"] < MIN_NN_SAMPLES:  # same as UI gate
        raise ValueError(
            f"可用于 NN 的样本不足：{ingest_stats['ingested']} 条 "
            f"(需要 ≥{MIN_NN_SAMPLES}，且须含 params_used 与算法标签)"
        )

    t0 = time.perf_counter()
    metrics = reg.train(epochs=epochs, lr=lr, batch_size=batch_size)
    train_ms = (time.perf_counter() - t0) * 1000.0
    if not reg.save_model():
        raise RuntimeError("NN 训练完成但保存 param_regressor.bin 失败")

    store = get_training_store()
    stats = store.get_stats()
    stats["last_nn_train_time"] = time.time()
    stats["last_nn_train_samples"] = ingest_stats["ingested"]
    store._stats.update(stats)
    store._save_stats()

    result = {
        "ingest": ingest_stats,
        "per_algorithm": reg.count_by_algorithm(),
        "metrics": metrics,
        "train_ms": train_ms,
        "model_path": str(reg._get_default_model_path()),
    }
    logger.info("[nn_train] complete: %s", result)
    return result


def format_nn_train_summary(result: dict[str, Any]) -> str:
    ingest = result.get("ingest") or {}
    metrics = result.get("metrics") or {}
    lines = [
        f"摄入样本: {ingest.get('ingested', '?')} (跳过 {ingest.get('skipped', 0)})",
        f"训练耗时: {result.get('train_ms', 0):.0f} ms",
        f"模型路径:\n  {result.get('model_path', '')}",
    ]
    if metrics:
        lines.append(
            f"最终 loss: train={metrics.get('final_train_loss', 0):.6f} "
            f"val={metrics.get('final_val_loss', 0):.6f}"
        )
        lines.append(f"完成 epoch: {metrics.get('epochs_completed', '?')}")
    per_algo = result.get("per_algorithm") or {}
    if per_algo:
        lines.append("按算法槽位: " + ", ".join(f"{k}={v}" for k, v in sorted(per_algo.items())))
    return "\n".join(lines)


@dataclass
class NNTrainResult:
    ok: bool
    message: str
    summary: dict[str, Any] | None = None


class NNTrainWorker(QThread):
    finished_with_result = pyqtSignal(object)
    progress = pyqtSignal(str)

    def run(self) -> None:
        try:
            self.progress.emit("正在从 JSONL 加载参数回归样本…")
            logger.info("[nn_train] worker started")
            summary = train_nn_from_jsonl()
            logger.info("[nn_train] worker train finished")
            self.progress.emit("正在加载 NN 模型…")
            reg = ParameterRegressor.get()
            reg.load_model()
            self.finished_with_result.emit(
                NNTrainResult(ok=True, message=format_nn_train_summary(summary), summary=summary)
            )
        except Exception as e:
            logger.exception("[nn_train] failed")
            self.finished_with_result.emit(NNTrainResult(ok=False, message=str(e), summary=None))
        finally:
            logger.info("[nn_train] worker exiting")
