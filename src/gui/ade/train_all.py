r"""ADE 全阶段训练管道：Stage1(RF) -> Stage2a(NN) -> Stage2b(EA)

用法:
    cd d:\AAA_C\compression-tool
    python -m src.gui.ade.train_all

    或指定阶段:
    python -m src.gui.ade.train_all --stage rf       # 仅 RF
    python -m src.gui.ade.train_all --stage nn       # 仅 NN
    python -m src.gui.ade.train_all --stage ea       # 仅 EA
    python -m src.gui.ade.train_all --stage rf,nn    # RF + NN
"""

from __future__ import annotations

import argparse
import logging
import sys
import time
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("ade.train_all")

_RF_READY = "[OK]    Stage1 RF "
_RF_SKIP = "[SKIP]  Stage1 RF "
_RF_FAIL = "[FAIL]  Stage1 RF "
_NN_READY = "[OK]    Stage2a NN "
_NN_SKIP = "[SKIP]  Stage2a NN "
_NN_FAIL = "[FAIL]  Stage2a NN "
_EA_READY = "[OK]    Stage2b EA "
_EA_SKIP = "[SKIP]  Stage2b EA "
_EA_FAIL = "[FAIL]  Stage2b EA "


def _ensure_path() -> None:
    root = Path(__file__).resolve().parent.parent.parent.parent
    src = root / "src"
    for p in (str(root), str(src)):
        if p not in sys.path:
            sys.path.insert(0, p)


def show_status(data_dir: Path | None = None) -> None:
    from gui.ade.rf_train import (
        MIN_RF_SAMPLES,
        MIN_RF_CLASSES,
        count_rf_ready_samples,
    )
    from gui.ade.nn_train import count_nn_ready_samples, MIN_NN_SAMPLES
    from gui.ade.training import get_training_store, TrainingDataStore
    from gui.ade.params import ParameterRegressor

    if data_dir is not None:
        import gui.ade.training as _ts
        store = TrainingDataStore(base_dir=data_dir)
        _ts._store_instance = store
    else:
        store = get_training_store()
    stats = store.get_stats()
    total = int(stats.get("total_samples", 0) or 0)
    valid = int(stats.get("valid_samples", 0) or 0)

    print("=" * 60)
    print("  ADE \u8bad\u7ec3\u72b6\u6001")
    print("=" * 60)
    print(f"  JSONL \u6837\u672c: {total} \u603b / {valid} \u6709\u6548")
    print(f"  \u6570\u636e\u6587\u4ef6: {store._data_file}")

    algo_dist = stats.get("algorithm_distribution", {})
    if algo_dist:
        items = sorted(algo_dist.items(), key=lambda x: -x[1])
        print(f"  \u7b97\u6cd5\u5206\u5e03: {', '.join(f'{k}={v}' for k, v in items[:8])}")

    rf_ready = count_rf_ready_samples()
    print(f"\n  [Stage1 RF]  \u53ef\u7528\u6837\u672c: {rf_ready} (\u9700\u2265{MIN_RF_SAMPLES}\u6761, \u2265{MIN_RF_CLASSES}\u7c7b)")

    nn_ready = count_nn_ready_samples()
    reg = ParameterRegressor.get()
    print(f"  [Stage2a NN] \u53ef\u7528\u6837\u672c: {nn_ready} (\u9700\u2265{MIN_NN_SAMPLES}\u6761)")
    print(f"  [Stage2a NN] C++ ParamRegressorNet: {'\u2714\ufe0f \u53ef\u7528' if reg._has_cpp else '\u2718 \u4e0d\u53ef\u7528'}")

    from gui.ade.ea_tune import resolve_ea_priors_path
    ea_path = resolve_ea_priors_path()
    print(f"  [Stage2b EA] \u5148\u9a8c\u6587\u4ef6: {ea_path} {'(\u5df2\u5b58\u5728)' if ea_path.is_file() else '(\u672a\u751f\u6210)'}")

    from gui.ade.rf_train import resolve_rf_model_output_path
    rf_path = resolve_rf_model_output_path()
    print(f"  [Stage1 RF]  \u6a21\u578b\u6587\u4ef6: {rf_path} {'(\u5df2\u5b58\u5728)' if rf_path.is_file() else '(\u672a\u751f\u6210)'}")

    nn_path = reg._get_default_model_path()
    print(f"  [Stage2a NN] \u6a21\u578b\u6587\u4ef6: {nn_path} {'(\u5df2\u5b58\u5728)' if nn_path.is_file() else '(\u672a\u751f\u6210)'}")
    print("=" * 60)


def _train_rf() -> bool:
    from gui.ade.rf_train import (
        MIN_RF_CLASSES,
        MIN_RF_SAMPLES,
        count_rf_ready_samples,
        train_rf_from_jsonl,
        format_train_summary,
        resolve_rf_model_output_path,
    )

    n = count_rf_ready_samples()
    if n < MIN_RF_SAMPLES:
        print(f"{_RF_SKIP} \u6837\u672c\u4e0d\u8db3({n} < {MIN_RF_SAMPLES})")
        return False

    labels = _count_rf_labels()
    if labels < MIN_RF_CLASSES:
        print(f"{_RF_SKIP} \u7b97\u6cd5\u7c7b\u522b\u4e0d\u8db3({labels} < {MIN_RF_CLASSES})")
        return False

    print(f"\n>>> \u5f00\u59cb Stage1 RF \u8bad\u7ec3 ({n} \u6761\u6837\u672c, {labels} \u7c7b\u7b97\u6cd5) ...")
    t0 = time.perf_counter()
    try:
        result = train_rf_from_jsonl()
        elapsed = time.perf_counter() - t0
        print(f"{_RF_READY} \u5b8c\u6210 ({elapsed:.1f}s)")
        print(format_train_summary(result))
        return True
    except Exception as e:
        elapsed = time.perf_counter() - t0
        print(f"{_RF_FAIL} \u5931\u8d25 ({elapsed:.1f}s): {e}")
        logger.exception("[train_all] RF train failed")
        return False


def _count_rf_labels() -> int:
    from gui.ade.rf_train import build_rf_rows
    from gui.ade.training import get_training_store

    store = get_training_store()
    samples = store.load(validate=True)
    rows = build_rf_rows(samples, include_exploration=True)
    return len({label for _f, label, _w in rows})


def _train_nn() -> bool:
    from gui.ade.nn_train import (
        MIN_NN_SAMPLES,
        count_nn_ready_samples,
        train_nn_from_jsonl,
        format_nn_train_summary,
    )
    from gui.ade.params import ParameterRegressor

    reg = ParameterRegressor.get()
    if not reg._has_cpp:
        print(f"{_NN_SKIP} C++ ParamRegressorNet \u4e0d\u53ef\u7528\uff0c\u8bf7\u91cd\u65b0\u7f16\u8bd1 core_engine")
        return False

    n = count_nn_ready_samples()
    if n < MIN_NN_SAMPLES:
        print(f"{_NN_SKIP} \u6837\u672c\u4e0d\u8db3({n} < {MIN_NN_SAMPLES})")
        return False

    print(f"\n>>> \u5f00\u59cb Stage2a NN \u8bad\u7ec3 ({n} \u6761\u6837\u672c) ...")
    t0 = time.perf_counter()
    try:
        result = train_nn_from_jsonl()
        elapsed = time.perf_counter() - t0
        print(f"{_NN_READY} \u5b8c\u6210 ({elapsed:.1f}s)")
        print(format_nn_train_summary(result))
        return True
    except Exception as e:
        elapsed = time.perf_counter() - t0
        print(f"{_NN_FAIL} \u5931\u8d25 ({elapsed:.1f}s): {e}")
        logger.exception("[train_all] NN train failed")
        return False


def _train_ea() -> bool:
    from gui.ade.ea_tune import (
        build_ea_priors_from_jsonl,
        format_ea_priors_summary,
        resolve_ea_priors_path,
    )

    print("\n>>> \u5f00\u59cb Stage2b EA \u5148\u9a8c\u6821\u51c6 ...")
    t0 = time.perf_counter()
    try:
        result = build_ea_priors_from_jsonl()
        elapsed = time.perf_counter() - t0
        print(f"{_EA_READY} \u5b8c\u6210 ({elapsed:.1f}s)")
        print(format_ea_priors_summary(result))
        return True
    except Exception as e:
        elapsed = time.perf_counter() - t0
        print(f"{_EA_FAIL} \u5931\u8d25 ({elapsed:.1f}s): {e}")
        logger.exception("[train_all] EA train failed")
        return False


def main() -> None:
    _ensure_path()

    parser = argparse.ArgumentParser(description="ADE 全阶段训练管道")
    parser.add_argument(
        "--stage",
        default="all",
        help="训练阶段: all | rf | nn | ea | rf,nn (default: all)",
    )
    parser.add_argument(
        "--status",
        action="store_true",
        help="仅显示训练状态，不执行训练",
    )
    parser.add_argument(
        "--data-dir",
        default=None,
        help="训练数据目录 (默认: 项目根/ade, GUI路径: Package/ade)",
    )
    args = parser.parse_args()

    data_dir = Path(args.data_dir) if args.data_dir else None

    if args.status:
        show_status(data_dir=data_dir)
        return

    stages = set(s.strip().lower() for s in args.stage.split(","))
    if "all" in stages:
        stages = {"rf", "nn", "ea"}

    show_status(data_dir=data_dir)

    results: dict[str, bool] = {}

    if "rf" in stages:
        results["rf"] = _train_rf()

    if "nn" in stages:
        results["nn"] = _train_nn()

    if "ea" in stages:
        results["ea"] = _train_ea()

    print("\n" + "=" * 60)
    print("  训练结果")
    print("=" * 60)
    for stage, ok in results.items():
        name = {"rf": "Stage1 RF", "nn": "Stage2a NN", "ea": "Stage2b EA"}.get(stage, stage)
        icon = "[OK]" if ok else "[FAIL]"
        print(f"  {icon} {name}")
    print("=" * 60)


if __name__ == "__main__":
    main()