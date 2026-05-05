"""Main window — three-column IDE-style layout."""

from __future__ import annotations

import logging
import os
import tempfile

from PyQt6.QtCore import Qt, QMimeData, QUrl, pyqtSignal, QThread
from PyQt6.QtGui import QAction, QDragEnterEvent, QDropEvent
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QMessageBox, QToolBar, QWidget,
    QStatusBar, QProgressBar, QLabel, QVBoxLayout, QHBoxLayout,
    QPushButton, QComboBox, QSplitter, QStackedWidget, QApplication,
    QFrame,
)

logger = logging.getLogger("gui.main_window")

from gui.core.models import (
    Record, FileRecord, FolderRecord, CompressionStatus,
    AlgorithmType, formatted_size,
)

# ── Views ──
from gui.widgets.dashboard_view import DashboardView
from gui.widgets.compression_view import CompressionView
from gui.widgets.analysis_view import AnalysisView
from gui.widgets.comparison_view import ComparisonView
from gui.widgets.network_view import NetworkView

# ── Panels ──
from gui.widgets.resource_tree import ResourceTree
from gui.widgets.property_panel import PropertyPanel

WINDOW_WIDTH = 1200
WINDOW_HEIGHT = 800

NAV_BUTTON_STYLE = """
QPushButton {
    text-align: left;
    padding: 10px 12px;
    border: none;
    border-radius: 6px;
    font-size: 13px;
    background: transparent;
}
QPushButton:hover { background: #e8e8e8; }
QPushButton:checked { background: #4a90d9; color: white; font-weight: bold; }
"""


# ═══════════════════════════════════════════════════════
#  Compression worker (preserved from original)
# ═══════════════════════════════════════════════════════

class CompressionWorker(QThread):

    progress = pyqtSignal(int, str)
    finished_row = pyqtSignal(int)
    error = pyqtSignal(int)

    def __init__(self, tasks: list[tuple[int, Record]],
                 algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        super().__init__()
        self.tasks = tasks
        self.algorithm = algorithm

    def single_compress(self, row_idx: int, record: Record,
                        notify: bool = True) -> None:
        import traceback
        logger.info("[compress] START row=%d file=%s algo=%s",
                    row_idx, getattr(record, 'path', '?'), self.algorithm.value)
        try:
            from gui.core.engine import CompressionEngine
            engine = CompressionEngine()
            if not engine.available:
                raise RuntimeError("C++ core_engine not available")

            if isinstance(record, FileRecord):
                record.algorithm = self.algorithm
                if self.algorithm == AlgorithmType.AUTO:
                    record.algorithm = AlgorithmType.DEFLATE

            suffix = '.compressed'
            fd, tmp_path = tempfile.mkstemp(suffix=suffix)
            os.close(fd)
            try:
                logger.info("[compress] streaming %s -> %s", record.path, tmp_path)
                result = engine.compress_file(record.path, tmp_path,
                                              self.algorithm)
                logger.info("[compress] done: %d -> %d bytes",
                            result['original_size'], result['compressed_size'])

                if result['success']:
                    with open(tmp_path, 'rb') as f:
                        record.compressed_data = f.read()
                    record.status = CompressionStatus.DONE
                    record.compression_ratio = (
                        result['compressed_size'] / record.size
                        if record.size > 0 else 0)
                    record.compression_time_ms = result['time_ms']
                    record.block_profile = result.get('block_profile')
                    if notify:
                        self.finished_row.emit(row_idx)
                else:
                    record.status = CompressionStatus.FAILED
                    record.error_message = result.get('error_message', '')
                    if notify:
                        self.error.emit(row_idx)
            finally:
                try:
                    os.unlink(tmp_path)
                except OSError:
                    pass

        except Exception as e:
            logger.error("[compress] CRASH row=%d file=%s: %s\n%s",
                         row_idx, getattr(record, 'path', '?'), e,
                         traceback.format_exc())
            record.status = CompressionStatus.FAILED
            record.error_message = str(e)
            self.error.emit(row_idx)

    def run(self) -> None:
        for row_idx, record in self.tasks:
            if isinstance(record, FolderRecord):
                for filerecord in record.files:
                    self.single_compress(row_idx, filerecord, notify=False)
                failed = sum(1 for f in record.files
                             if f.status == CompressionStatus.FAILED)
                if failed == 0:
                    record.status = CompressionStatus.DONE
                    record.compression_ratio = (
                        sum(len(f.compressed_data) for f in record.files
                            if f.compressed_data) / record.size
                        if record.size > 0 else 1.0)
                elif failed == record.filenum:
                    record.status = CompressionStatus.FAILED
                else:
                    record.status = CompressionStatus.DONE
                record.compression_time_ms = sum(
                    f.compression_time_ms for f in record.files)
                self.finished_row.emit(row_idx)
            elif isinstance(record, FileRecord):
                self.single_compress(row_idx, record)


class DecompressWorker(QThread):
    finished_file = pyqtSignal(str, bool, str)

    def __init__(self, tasks: list[tuple[str, str]]):
        super().__init__()
        self.tasks = tasks

    def run(self) -> None:
        from gui.core.engine import CompressionEngine
        engine = CompressionEngine()
        if not engine.available:
            for inp, _ in self.tasks:
                self.finished_file.emit(inp, False, "core_engine not available")
            return
        for inp, out in self.tasks:
            try:
                result = engine.decompress_file(inp, out)
                if result['success']:
                    self.finished_file.emit(inp, True,
                        f"{result['original_size']} -> {result['compressed_size']} bytes")
                else:
                    self.finished_file.emit(inp, False, result['error_message'])
            except Exception as e:
                self.finished_file.emit(inp, False, str(e))


# ═══════════════════════════════════════════════════════
#  Navigation sidebar
# ═══════════════════════════════════════════════════════

class NavSidebar(QFrame):
    """Left navigation sidebar with view-switching buttons and resource tree."""

    view_changed = pyqtSignal(int)

    NAV_ITEMS = [
        ("📊  概览", 0),
        ("⚙  压缩", 1),
        ("🔬  分析", 2),
        ("⚖  对比", 3),
        ("📡  传输", 4),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedWidth(220)
        self.setFrameShape(QFrame.Shape.NoFrame)
        self.setStyleSheet("background: #2b2b2b;")

        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 8, 6, 8)
        layout.setSpacing(2)

        # App title
        title = QLabel("WebCompress")
        title.setStyleSheet("font-size: 15px; font-weight: bold; padding: 8px 12px; color: #333;")
        layout.addWidget(title)

        # Nav buttons
        self._nav_buttons: list[QPushButton] = []
        for label, idx in self.NAV_ITEMS:
            btn = QPushButton(label)
            btn.setCheckable(True)
            btn.setStyleSheet(NAV_BUTTON_STYLE)
            btn.clicked.connect(lambda checked, i=idx: self._on_click(i))
            if idx == 0:
                btn.setChecked(True)
                btn.setStyleSheet(btn.styleSheet() +
                    "QPushButton:checked { background: #4a90d9; color: white; font-weight: bold; }")
            self._nav_buttons.append(btn)
            layout.addWidget(btn)

        # Separator
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet("color: #ddd; margin: 8px 4px;")
        layout.addWidget(sep)

        # Resource tree
        self.resource_tree = ResourceTree()
        layout.addWidget(self.resource_tree)

        layout.addStretch()

    def _on_click(self, index: int) -> None:
        for i, btn in enumerate(self._nav_buttons):
            if i == index:
                btn.setChecked(True)
            else:
                btn.setChecked(False)
        self.view_changed.emit(index)

    def set_view(self, index: int) -> None:
        self._on_click(index)


# ═══════════════════════════════════════════════════════
#  Main window
# ═══════════════════════════════════════════════════════

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("WebCompress — 网页资源压缩系统")
        self.resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        self.setAcceptDrops(True)

        self._records: list[Record] = []
        self._worker: CompressionWorker | None = None
        self._decompress_worker: DecompressWorker | None = None

        self._setup_menu()
        self._setup_central()
        self._setup_statusbar()

    # ── Menu ──────────────────────────────────────────

    def _setup_menu(self) -> None:
        bar = self.menuBar()

        file_menu = bar.addMenu("文件 (&F)")
        add_file_action = QAction("添加文件 (&F)", self)
        add_file_action.setShortcut("Ctrl+F")
        add_file_action.triggered.connect(self._on_add_files)
        file_menu.addAction(add_file_action)

        add_folder_action = QAction("添加文件夹 (&D)", self)
        add_folder_action.setShortcut("Ctrl+D")
        add_folder_action.triggered.connect(self._on_add_folder)
        file_menu.addAction(add_folder_action)

        file_menu.addSeparator()
        clear_action = QAction("清空列表 (&C)", self)
        clear_action.setShortcut("Ctrl+Shift+C")
        clear_action.triggered.connect(self._on_clear)
        file_menu.addAction(clear_action)

        export_action = QAction("导出结果 (&E)", self)
        export_action.setShortcut("Ctrl+E")
        export_action.triggered.connect(self._on_export)
        file_menu.addAction(export_action)

        file_menu.addSeparator()
        decompress_action = QAction("解压文件...", self)
        decompress_action.triggered.connect(self._on_decompress)
        file_menu.addAction(decompress_action)

        help_menu = bar.addMenu("帮助 (&H)")
        about_action = QAction("关于 (&A)", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)

    # ── Central layout ────────────────────────────────

    def _setup_central(self) -> None:
        central = QWidget()
        main_layout = QHBoxLayout(central)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        # Left: nav sidebar
        self._nav = NavSidebar()
        self._nav.view_changed.connect(self._on_view_changed)
        main_layout.addWidget(self._nav)

        # Vertical separator
        vsep = QFrame()
        vsep.setFrameShape(QFrame.Shape.VLine)
        vsep.setStyleSheet("color: #ddd;")
        main_layout.addWidget(vsep)

        # Center: stacked views
        self._stack = QStackedWidget()

        self._dashboard = DashboardView()
        self._compression_view = CompressionView()
        self._analysis = AnalysisView()
        self._comparison = ComparisonView()
        self._network = NetworkView()

        self._stack.addWidget(self._dashboard)       # index 0
        self._stack.addWidget(self._compression_view) # index 1
        self._stack.addWidget(self._analysis)         # index 2
        self._stack.addWidget(self._comparison)       # index 3
        self._stack.addWidget(self._network)          # index 4

        main_layout.addWidget(self._stack, stretch=1)

        # Vertical separator
        vsep2 = QFrame()
        vsep2.setFrameShape(QFrame.Shape.VLine)
        vsep2.setStyleSheet("color: #ddd;")
        main_layout.addWidget(vsep2)

        # Right: property panel
        self._property_panel = PropertyPanel()
        main_layout.addWidget(self._property_panel)

        self.setCentralWidget(central)

        # Wire signals
        self._nav.resource_tree.file_selected.connect(self._on_file_selected)
        self._nav.resource_tree.file_double_clicked.connect(self._on_file_analyze)
        self._nav.resource_tree.compress_requested.connect(self._on_single_compress)

        self._compression_view.compress_all.connect(self._on_compress)
        self._compression_view.incremental_compress.connect(self._on_incremental_compress)
        self._compression_view.pack_requested.connect(self._on_pack)

    def _on_view_changed(self, index: int) -> None:
        self._stack.setCurrentIndex(index)
        # Refresh views when switching to them
        if index == 0:
            self._dashboard.update_from_records(self._records)
        elif index == 1:
            self._compression_view.load_records(self._records)
        elif index == 2:
            self._analysis.set_records(self._records)
        elif index == 4:
            self._network.update_from_records(self._records)

    # ── Status bar ────────────────────────────────────

    def _setup_statusbar(self) -> None:
        bar = QStatusBar()
        self.setStatusBar(bar)

        self._status_label = QLabel("就绪")
        bar.addWidget(self._status_label, stretch=1)

        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setFixedWidth(200)
        self._progress.setTextVisible(True)
        bar.addPermanentWidget(self._progress)

        self._mem_label = QLabel("")
        self._mem_label.setStyleSheet("color: #888; font-size: 11px; padding: 0 8px;")
        bar.addPermanentWidget(self._mem_label)

    def _set_status(self, text: str, progress: int = -1) -> None:
        self._status_label.setText(text)
        if progress >= 0:
            self._progress.setValue(progress)

    # ── File operations ───────────────────────────────

    def _on_add_files(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(self, "选择文件", "", "所有文件 (*)")
        if paths:
            self._add_paths(paths)

    def _on_add_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if folder:
            self._add_paths([folder])

    def _add_paths(self, paths: list[str]) -> None:
        for path in paths:
            if os.path.isdir(path):
                rec = FolderRecord(path)
            elif os.path.isfile(path):
                rec = FileRecord(path)
            else:
                continue
            self._records.append(rec)

        self._nav.resource_tree.load_records(self._records)
        self._dashboard.update_from_records(self._records)
        self._compression_view.load_records(self._records)
        self._network.update_from_records(self._records)

        count = len(paths)
        self._set_status(f"已添加 {count} 个资源", 0)

    def _on_clear(self) -> None:
        self._records.clear()
        self._nav.resource_tree.load_records([])
        self._dashboard.update_from_records([])
        self._compression_view.load_records([])
        self._network.update_from_records([])
        self._property_panel.set_record(None)
        self._set_status("已清空列表", 0)

    # ── File selection ────────────────────────────────

    def _on_file_selected(self, rec: Record) -> None:
        if isinstance(rec, FileRecord):
            self._property_panel.set_record(rec)
            self._comparison.set_file(
                rec.path,
                {
                    'compressed_size': len(rec.compressed_data) if rec.compressed_data else rec.size,
                    'compression_ratio': rec.compression_ratio,
                    'time_ms': rec.compression_time_ms,
                } if rec.status == CompressionStatus.DONE else None
            )

    def _on_file_analyze(self, rec: Record) -> None:
        """Double-click: jump to analysis view."""
        if isinstance(rec, FileRecord):
            self._nav.set_view(2)  # Analysis view
            self._analysis.set_records(self._records)

    def _on_single_compress(self, rec: Record) -> None:
        """Right-click: compress single file."""
        if isinstance(rec, FileRecord):
            self._records = [rec]
            self._on_compress()

    # ── Compression ───────────────────────────────────

    def _on_compress(self) -> None:
        if self._worker and self._worker.isRunning():
            QMessageBox.warning(self, "提示", "压缩任务正在进行中...")
            return

        records = self._records
        if not records:
            QMessageBox.warning(self, "提示", "请先添加文件！")
            return

        tasks = list(enumerate(records))

        self._worker = CompressionWorker(tasks, AlgorithmType.DEFLATE)
        self._worker.finished_row.connect(self._on_row_finished)
        self._worker.finished.connect(self._on_compression_finished)

        file_count = sum(
            len(r.files) if isinstance(r, FolderRecord) else 1
            for r in records
        )
        self._set_status(f"正在压缩 {file_count} 个文件...", 0)
        self._worker.start()

    def _on_incremental_compress(self) -> None:
        QMessageBox.information(self, "提示", "增量压缩功能将在后续版本中实现")

    def _on_pack(self) -> None:
        QMessageBox.information(self, "提示",
            "打包功能请先完成压缩，然后使用 文件→导出结果")

    def _on_row_finished(self, row: int) -> None:
        try:
            rec = self._records[row] if row < len(self._records) else None
            if rec:
                if isinstance(rec, FolderRecord):
                    for f in rec.files:
                        self._nav.resource_tree.update_compression_result(f)
                else:
                    self._nav.resource_tree.update_compression_result(rec)

            # Update all views
            self._compression_view.load_records(self._records)
            self._dashboard.update_from_records(self._records)
            self._network.update_from_records(self._records)
            self._analysis.set_records(self._records)

            # Show the last compressed file's profile
            if isinstance(rec, FileRecord) and rec.block_profile:
                self._property_panel.set_record(rec)
        except Exception as e:
            logger.error("[_on_row_finished] CRASH row=%d: %s", row, e, exc_info=True)

    def _on_compression_finished(self) -> None:
        self._set_status("压缩完成", 100)
        QMessageBox.information(self, "完成", "所有文件已处理完毕！")

    # ── Export ────────────────────────────────────────

    def _on_export(self) -> None:
        export_dir = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not export_dir:
            return

        exported = 0
        for rec in self._records:
            files = rec.files if isinstance(rec, FolderRecord) else [rec]
            for f in files:
                if f.status == CompressionStatus.DONE and f.compressed_data:
                    export_path = os.path.join(export_dir, f"{f.name}.compressed")
                    with open(export_path, 'wb') as out:
                        data = f.compressed_data
                        if isinstance(data, list):
                            data = bytes(data)
                        out.write(data)
                    exported += 1

        if exported > 0:
            self._set_status(f"已导出 {exported} 个文件到 {export_dir}")
            QMessageBox.information(self, "导出完成",
                                    f"已成功导出 {exported} 个文件！")
        else:
            QMessageBox.warning(self, "提示",
                                "没有可导出的文件（请先压缩）")

    # ── Decompress ────────────────────────────────────

    def _on_decompress(self) -> None:
        if self._decompress_worker and self._decompress_worker.isRunning():
            QMessageBox.warning(self, "提示", "解压任务正在进行中...")
            return

        paths, _ = QFileDialog.getOpenFileNames(
            self, "选择要解压的文件", "",
            "压缩文件 (*.compressed);;所有文件 (*)")
        if not paths:
            return

        tasks = []
        for p in paths:
            out = p
            if out.endswith('.compressed'):
                out = out[:-11]
            else:
                out = p + '.decompressed'
            tasks.append((p, out))

        self._decompress_worker = DecompressWorker(tasks)
        self._decompress_worker.finished_file.connect(self._on_decompress_finished)
        self._decompress_worker.finished.connect(
            lambda: self._set_status("解压完成"))
        self._set_status(f"正在解压 {len(tasks)} 个文件...")
        self._decompress_worker.start()

    def _on_decompress_finished(self, path: str, success: bool, msg: str):
        if success:
            self._set_status(f"解压成功: {os.path.basename(path)} ({msg})")
        else:
            logger.error("解压失败: %s - %s", path, msg)
            QMessageBox.warning(self, "解压失败", f"{path}\n{msg}")

    # ── Drag & drop ───────────────────────────────────

    def dragEnterEvent(self, event: QDragEnterEvent) -> None:
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event: QDropEvent) -> None:
        urls = event.mimeData().urls()
        paths = [url.toLocalFile() for url in urls if url.isLocalFile()]
        if paths:
            self._add_paths(paths)

    # ── About ─────────────────────────────────────────

    def _on_about(self) -> None:
        QMessageBox.about(
            self,
            "关于 WebCompress",
            "WebCompress — 网页资源压缩系统\n\n"
            "基于 Deflate/LZ77 与 Huffman 编码的高效压缩工具。\n"
            "支持 HTML/CSS/JS/PNG/JPG 等网页常见资源。\n\n"
            "特性:\n"
            "• 流式压缩/解压，常量内存占用\n"
            "• 块级压缩剖析与 Huffman 树可视化\n"
            "• 与 gzip/ZIP 对比评测\n"
            "• 多网络环境传输模拟\n\n"
            "© 2026 数据结构课程设计"
        )

    # ── Cleanup ───────────────────────────────────────

    def closeEvent(self, event) -> None:
        if self._worker and self._worker.isRunning():
            self._worker.quit()
            self._worker.wait()
        if self._decompress_worker and self._decompress_worker.isRunning():
            self._decompress_worker.quit()
            self._decompress_worker.wait()
        event.accept()
