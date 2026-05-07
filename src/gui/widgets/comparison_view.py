"""Comparison view — benchmark against gzip, ZIP, brotli."""

from __future__ import annotations

import subprocess
import tempfile
import os
import time

from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QCheckBox,
    QTableWidget, QTableWidgetItem, QHeaderView, QFrame, QMessageBox,
)

from gui.core.models import formatted_size
from gui.core.theme import ThemeManager


class ComparisonWorker(QThread):
    """Run gzip/ZIP/brotli benchmarks in background."""

    progress = pyqtSignal(str)
    finished = pyqtSignal(dict)

    def __init__(self, file_path: str, tools: list[str]):
        super().__init__()
        self._file_path = file_path
        self._tools = tools

    def run(self) -> None:
        results = {}
        original_size = os.path.getsize(self._file_path)

        for tool in self._tools:
            self.progress.emit(f"正在运行 {tool} ...")
            tmp_out = tempfile.mktemp(suffix=f'.{tool}')
            try:
                t0 = time.time()
                if tool == "gzip":
                    subprocess.run(["gzip", "-k", "-c", self._file_path],
                                   stdout=open(tmp_out, 'wb'), stderr=subprocess.DEVNULL,
                                   check=True)
                elif tool == "zip":
                    subprocess.run(["zip", "-q", tmp_out, self._file_path],
                                   check=True)
                elif tool == "brotli":
                    subprocess.run(["brotli", "-c", self._file_path],
                                   stdout=open(tmp_out, 'wb'), stderr=subprocess.DEVNULL,
                                   check=True)
                else:
                    continue
                t1 = time.time()
                compressed_size = os.path.getsize(tmp_out)
                ratio = compressed_size / original_size if original_size > 0 else 1

                # Decompress timing
                t2 = time.time()
                if tool == "gzip":
                    subprocess.run(["gzip", "-d", "-c", tmp_out],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   check=True)
                elif tool == "zip":
                    subprocess.run(["unzip", "-p", tmp_out],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   check=True)
                elif tool == "brotli":
                    subprocess.run(["brotli", "-d", "-c", tmp_out],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   check=True)
                t3 = time.time()

                results[tool] = {
                    "compressed_size": compressed_size,
                    "ratio": ratio,
                    "compress_time_ms": (t1 - t0) * 1000,
                    "decompress_time_ms": (t3 - t2) * 1000,
                }
            except FileNotFoundError:
                results[tool] = {"error": f"{tool} 未安装"}
            except subprocess.CalledProcessError as e:
                results[tool] = {"error": str(e)}
            finally:
                if os.path.exists(tmp_out):
                    os.unlink(tmp_out)

        self.finished.emit(results)


class ComparisonView(QWidget):
    """View 4: Benchmark against general-purpose compression tools."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._file_path: str = ""
        self._our_result: dict | None = None
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)

        title = QLabel("通用工具对比")
        title.setStyleSheet("font-size: 18px; font-weight: bold; padding: 0 0 8px 0;")
        layout.addWidget(title)

        desc = QLabel("选择已压缩的文件，与 gzip / ZIP / brotli 进行压缩率和耗时对比")
        desc.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; padding-bottom: 12px;")
        layout.addWidget(desc)

        # Tool selection
        tools_layout = QHBoxLayout()
        tools_layout.addWidget(QLabel("对比工具:"))
        self._gzip_cb = QCheckBox("gzip")
        self._gzip_cb.setChecked(True)
        self._zip_cb = QCheckBox("ZIP")
        self._zip_cb.setChecked(True)
        self._brotli_cb = QCheckBox("brotli")
        tools_layout.addWidget(self._gzip_cb)
        tools_layout.addWidget(self._zip_cb)
        tools_layout.addWidget(self._brotli_cb)
        tools_layout.addStretch()

        self._run_btn = QPushButton("▶ 运行对比测试")
        self._run_btn.setStyleSheet(
            f"QPushButton {{ background: {ThemeManager.hex('warning')}; color: white; font-weight: bold; "
            "padding: 8px 20px; border-radius: 6px; }"
            f"QPushButton:hover {{ background: {ThemeManager.hex('accent_hover')}; }}"
        )
        self._run_btn.clicked.connect(self._run_comparison)
        tools_layout.addWidget(self._run_btn)
        layout.addLayout(tools_layout)

        # Results table
        self._table = QTableWidget()
        self._table.setColumnCount(5)
        self._table.setHorizontalHeaderLabels(
            ["工具", "压缩后大小", "压缩率", "压缩耗时", "解压耗时"])
        self._table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self._table.setColumnWidth(1, 100)
        self._table.setColumnWidth(2, 80)
        self._table.setColumnWidth(3, 100)
        self._table.setColumnWidth(4, 100)
        self._table.setAlternatingRowColors(True)
        layout.addWidget(self._table)

        self._status = QLabel("")
        self._status.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; font-size: 11px;")
        layout.addWidget(self._status)

    def set_file(self, file_path: str, our_result: dict | None = None) -> None:
        self._file_path = file_path
        self._our_result = our_result
        self._status.setText(f"已选择: {os.path.basename(file_path)} — 点击运行对比测试")

    def _run_comparison(self) -> None:
        if not self._file_path or not os.path.exists(self._file_path):
            QMessageBox.warning(self, "提示", "请先在压缩视图完成压缩")
            return

        tools = []
        if self._gzip_cb.isChecked():
            tools.append("gzip")
        if self._zip_cb.isChecked():
            tools.append("zip")
        if self._brotli_cb.isChecked():
            tools.append("brotli")

        if not tools:
            QMessageBox.warning(self, "提示", "请至少选择一个对比工具")
            return

        self._run_btn.setEnabled(False)
        self._status.setText("运行中...")

        self._worker = ComparisonWorker(self._file_path, tools)
        self._worker.progress.connect(lambda msg: self._status.setText(msg))
        self._worker.finished.connect(self._on_results)
        self._worker.start()

    def _on_results(self, results: dict) -> None:
        self._run_btn.setEnabled(True)
        self._status.setText("对比完成")

        rows = 0
        if self._our_result:
            rows += 1
        rows += len([r for r in results.values() if "error" not in r])

        self._table.setRowCount(rows)
        row = 0

        # Our system first
        if self._our_result:
            self._add_row(row, "本系统",
                         self._our_result.get('compressed_size', 0),
                         self._our_result.get('compression_ratio', 1),
                         self._our_result.get('time_ms', 0),
                         None)
            row += 1

        for tool, data in results.items():
            if "error" in data:
                continue
            self._add_row(row, tool,
                         data["compressed_size"],
                         data["ratio"],
                         data["compress_time_ms"],
                         data["decompress_time_ms"])
            row += 1

    def _add_row(self, row: int, name: str, size: int, ratio: float,
                 comp_time: float | None, decomp_time: float | None) -> None:
        self._table.setItem(row, 0, QTableWidgetItem(name))
        self._table.setItem(row, 1, QTableWidgetItem(formatted_size(size)))
        self._table.setItem(row, 2, QTableWidgetItem(f"{ratio * 100:.1f}%"))
        self._table.setItem(row, 3, QTableWidgetItem(
            f"{comp_time:.0f} ms" if comp_time is not None else "—"))
        self._table.setItem(row, 4, QTableWidgetItem(
            f"{decomp_time:.0f} ms" if decomp_time is not None else "—"))
