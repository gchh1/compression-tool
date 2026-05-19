"""Combined Stage2 training: NN param regressor + EA JSONL priors."""

from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Any

from PyQt6.QtCore import QThread, pyqtSignal

from gui.ade.ea_tune import build_ea_priors_from_jsonl, format_ea_priors_summary
from gui.ade.nn_train import (
    MIN_NN_SAMPLES,
    format_nn_train_summary,
    train_nn_from_jsonl,
)

logger = logging.getLogger(__name__)


@dataclass
class Stage2TrainResult:
    ok: bool
    message: str
    nn_summary: dict[str, Any] | None = None
    ea_summary: dict[str, Any] | None = None


class Stage2TrainWorker(QThread):
    """Train NN then calibrate EA priors from JSONL."""

    finished_with_result = pyqtSignal(object)
    progress = pyqtSignal(str)

    def __init__(self, parent=None, *, train_nn: bool = True, calibrate_ea: bool = True):
        super().__init__(parent)
        self._train_nn = train_nn
        self._calibrate_ea = calibrate_ea

    def run(self) -> None:
        nn_summary = None
        ea_summary = None
        parts: list[str] = []
        try:
            logger.info("[stage2_train] worker started (nn=%s ea=%s)", self._train_nn, self._calibrate_ea)
            if self._train_nn:
                self.progress.emit("正在训练 NN 参数回归…")
                from gui.config.settings import get_ade_stage2_settings

                epochs = int(get_ade_stage2_settings().get("nn_train_epochs", 50))
                nn_summary = train_nn_from_jsonl(epochs=epochs)
                from gui.ade.params import ParameterRegressor

                ParameterRegressor.get().load_model()
                parts.append("【NN】\n" + format_nn_train_summary(nn_summary))

            if self._calibrate_ea:
                self.progress.emit("正在从 JSONL 校准 EA 先验…")
                ea_summary = build_ea_priors_from_jsonl()
                parts.append("【EA 先验】\n" + format_ea_priors_summary(ea_summary))

            if not parts:
                raise ValueError("未选择任何训练步骤")

            logger.info("[stage2_train] worker finished ok")
            self.finished_with_result.emit(
                Stage2TrainResult(ok=True, message="\n\n".join(parts), nn_summary=nn_summary, ea_summary=ea_summary)
            )
        except Exception as e:
            logger.exception("[stage2_train] failed")
            msg = str(e)
            if parts:
                msg = "\n\n".join(parts) + "\n\n失败: " + msg
            self.finished_with_result.emit(
                Stage2TrainResult(ok=False, message=msg, nn_summary=nn_summary, ea_summary=ea_summary)
            )
        finally:
            logger.info("[stage2_train] worker exiting")

def count_stage2_ready_nn() -> int:
    from gui.ade.nn_train import count_nn_ready_samples

    return count_nn_ready_samples()
