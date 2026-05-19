"""Background worker for real network transfer benchmarks."""

from __future__ import annotations

import logging

from PyQt6.QtCore import QThread, pyqtSignal

from gui.engine.network_transfer import (
    NetworkBenchmarkTarget,
    ProfileBenchmarkResult,
    benchmark_all_profiles,
)

logger = logging.getLogger(__name__)


class NetworkTransferWorker(QThread):
    progress = pyqtSignal(str, int, int)
    finished_ok = pyqtSignal(list)
    failed = pyqtSignal(str)

    def __init__(self, target: NetworkBenchmarkTarget, parent=None):
        super().__init__(parent)
        self._target = target
        self._cancelled = False

    def cancel(self) -> None:
        self._cancelled = True

    def run(self) -> None:
        try:
            from gui.engine.compressor import CompressionEngine

            engine = CompressionEngine()
            if not engine.available:
                self.failed.emit("C++ 核心引擎不可用，无法执行客户端解压实测")
                return

            def _cancel() -> bool:
                return self._cancelled

            def _progress(msg: str, cur: int, tot: int) -> None:
                self.progress.emit(msg, cur, tot)

            results: list[ProfileBenchmarkResult] = benchmark_all_profiles(
                self._target,
                engine,
                cancel=_cancel,
                progress=_progress,
            )
            if self._cancelled:
                self.failed.emit("已取消")
                return
            self.finished_ok.emit(results)
        except InterruptedError:
            self.failed.emit("已取消")
        except Exception as e:
            logger.exception("[network_transfer] benchmark failed")
            self.failed.emit(str(e))
