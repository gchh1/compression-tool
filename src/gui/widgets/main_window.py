"""Main window — Bandizip-style toolbar + CompressPage / ArchivePage."""

from __future__ import annotations

import logging
import os

from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QAction
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QMessageBox, QToolBar, QWidget,
    QStatusBar, QProgressBar, QLabel, QVBoxLayout, QHBoxLayout,
    QPushButton, QStackedWidget, QApplication, QFrame, QDialog,
)

logger = logging.getLogger("gui.main_window")

from gui.core.models import (
    Record, FileRecord, FolderRecord, ArchiveEntry,
    CompressionStatus, AlgorithmType, formatted_size,
)

WINDOW_WIDTH = 1000
WINDOW_HEIGHT = 650


# ═══════════════════════════════════════════════════════
#  Workers
# ═══════════════════════════════════════════════════════

class CompressWorker(QThread):
    """Compress files/folders to disk using streaming backend."""

    progress = pyqtSignal(int, str)
    item_finished = pyqtSignal(int, dict)
    all_finished = pyqtSignal()

    def __init__(self, tasks: list[tuple[Record, str]],
                 algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        super().__init__()
        self.tasks = tasks
        self.algorithm = algorithm

    def run(self) -> None:
        from gui.core.engine import CompressionEngine
        engine = CompressionEngine()
        if not engine.available:
            for i, _ in enumerate(self.tasks):
                self.item_finished.emit(i, {
                    'success': False, 'error_message': 'C++ core_engine not available'
                })
            return

        for i, (rec, output_path) in enumerate(self.tasks):
            name = getattr(rec, 'name', '')
            self.progress.emit(i, f"正在压缩 {name} ...")

            try:
                if isinstance(rec, FolderRecord):
                    result = engine.compress_directory(rec.path, output_path,
                                                       self.algorithm)
                else:
                    result = engine.compress_file(rec.path, output_path,
                                                  self.algorithm)

                rec.status = CompressionStatus.DONE if result['success'] else CompressionStatus.FAILED
                if result.get('error_message'):
                    rec.error_message = result['error_message']
                rec.compression_ratio = result.get('compression_ratio', 1.0)
                rec.compression_time_ms = result.get('time_ms', 0)
                rec.block_profile = result.get('block_profile')
                if result['success']:
                    rec.compressed_data = b''  # mark as done (data is on disk)

                self.item_finished.emit(i, result)

            except Exception as e:
                logger.error("[CompressWorker] %s: %s", name, e, exc_info=True)
                rec.status = CompressionStatus.FAILED
                rec.error_message = str(e)
                self.item_finished.emit(i, {'success': False, 'error_message': str(e)})

        self.all_finished.emit()


class ArchiveListWorker(QThread):
    """Read a .compressed archive and return its entry list."""

    finished = pyqtSignal(list, str)      # entries, archive_path
    error = pyqtSignal(str)

    def __init__(self, archive_path: str):
        super().__init__()
        self.archive_path = archive_path

    def run(self) -> None:
        try:
            with open(self.archive_path, 'rb') as f:
                data = f.read()

            from gui.core.engine import _get_engine
            engine = _get_engine()
            if engine is None:
                self.error.emit("C++ core_engine not available")
                return

            web_files = engine.decompress_and_unpack(list(data))
            entries = [ArchiveEntry(name=wf.name, size=len(wf.content))
                       for wf in web_files]
            self.finished.emit(entries, self.archive_path)

        except Exception as e:
            logger.error("[ArchiveListWorker] %s", e, exc_info=True)
            self.error.emit(str(e))


class ArchiveExtractWorker(QThread):
    """Extract a .compressed archive to disk."""

    progress = pyqtSignal(str)
    finished = pyqtSignal(bool, str)

    def __init__(self, archive_path: str, output_dir: str):
        super().__init__()
        self.archive_path = archive_path
        self.output_dir = output_dir

    def run(self) -> None:
        try:
            self.progress.emit("正在解压...")
            from gui.core.engine import CompressionEngine
            engine = CompressionEngine()
            if not engine.available:
                self.finished.emit(False, "C++ core_engine not available")
                return

            result = engine.decompress_and_unpack_to_disk(
                self.archive_path, self.output_dir)
            if result['success']:
                self.finished.emit(True,
                    f"已解压到 {self.output_dir} ({formatted_size(result['original_size'])} → {formatted_size(result['compressed_size'])})")
            else:
                self.finished.emit(False, result.get('error_message', '未知错误'))

        except Exception as e:
            logger.error("[ArchiveExtractWorker] %s", e, exc_info=True)
            self.finished.emit(False, str(e))


# ═══════════════════════════════════════════════════════
#  ArchivePage — browse archive contents
# ═══════════════════════════════════════════════════════

class ArchivePage(QWidget):
    """Shows the contents of an opened .compressed archive."""

    extract_requested = pyqtSignal()
    close_requested = pyqtSignal()
    entry_double_clicked = pyqtSignal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._archive_path: str = ""
        self._entries: list[ArchiveEntry] = []
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)

        # Header
        self._title = QLabel("压缩包内容")
        self._title.setStyleSheet("font-size: 18px; font-weight: bold; padding: 0 0 4px 0;")
        layout.addWidget(self._title)

        self._info = QLabel("")
        self._info.setStyleSheet("color: #888; font-size: 12px; padding-bottom: 8px;")
        layout.addWidget(self._info)

        # Content tree
        from gui.widgets.resource_tree import ResourceTree
        self.tree = ResourceTree()
        self.tree.file_double_clicked.connect(self.entry_double_clicked.emit)
        layout.addWidget(self.tree)

        # Buttons
        btn_layout = QHBoxLayout()
        extract_btn = QPushButton("📂 解压全部")
        extract_btn.setStyleSheet(self._btn_style("#27ae60"))
        extract_btn.clicked.connect(self.extract_requested.emit)
        btn_layout.addWidget(extract_btn)

        extract_sel_btn = QPushButton("📄 解压所选")
        extract_sel_btn.setStyleSheet(self._btn_style("#4a90d9"))
        extract_sel_btn.clicked.connect(self._on_extract_selected)
        btn_layout.addWidget(extract_sel_btn)

        close_btn = QPushButton("✕ 关闭压缩包")
        close_btn.setStyleSheet(self._btn_style("#888"))
        close_btn.clicked.connect(self.close_requested.emit)
        btn_layout.addWidget(close_btn)

        btn_layout.addStretch()
        layout.addLayout(btn_layout)

    @staticmethod
    def _btn_style(color: str) -> str:
        return (
            f"QPushButton {{ background: {color}; color: white; font-weight: bold; "
            "padding: 8px 20px; border-radius: 6px; font-size: 13px; }}"
        )

    def _on_extract_selected(self) -> None:
        selected = self.tree.get_selected_records()
        if not selected:
            QMessageBox.information(self, "提示", "请先选择要解压的文件")
        else:
            self.extract_requested.emit()

    def set_entries(self, entries: list[ArchiveEntry], archive_path: str) -> None:
        self._entries = entries
        self._archive_path = archive_path
        name = os.path.basename(archive_path)
        total_size = sum(e.size for e in entries)
        self._title.setText(f"📦 {name}")
        self._info.setText(f"{len(entries)} 个文件，总大小 {formatted_size(total_size)}")
        self.tree.load_archive_entries(entries)

    @property
    def archive_path(self) -> str:
        return self._archive_path


# ═══════════════════════════════════════════════════════
#  Main window
# ═══════════════════════════════════════════════════════

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("WebCompress — 压缩工具")
        self.resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        self.setAcceptDrops(True)

        self._records: list[Record] = []
        self._current_archive: str = ""

        # Workers
        self._compress_worker: CompressWorker | None = None
        self._list_worker: ArchiveListWorker | None = None
        self._extract_worker: ArchiveExtractWorker | None = None

        self._setup_menu()
        self._setup_central()
        self._setup_statusbar()

    # ── Menu ──────────────────────────────────────────

    def _setup_menu(self) -> None:
        bar = self.menuBar()

        file_menu = bar.addMenu("文件 (&F)")
        for label, slot, shortcut in [
            ("添加文件 (&F)", self._on_add_files, "Ctrl+O"),
            ("添加文件夹 (&D)", self._on_add_folder, "Ctrl+D"),
            ("打开压缩包 (&P)", self._on_open_archive, "Ctrl+Shift+O"),
        ]:
            action = QAction(label, self)
            action.setShortcut(shortcut)
            action.triggered.connect(slot)
            file_menu.addAction(action)
        file_menu.addSeparator()
        exit_action = QAction("退出 (&Q)", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        tools_menu = bar.addMenu("工具 (&T)")
        for label, slot in [
            ("📊 项目概览", self._open_dashboard),
            ("🔬 压缩分析", self._open_analysis),
            ("⚖ 工具对比", self._open_comparison),
            ("📡 网络模拟", self._open_network),
        ]:
            action = QAction(label, self)
            action.triggered.connect(slot)
            tools_menu.addAction(action)

        help_menu = bar.addMenu("帮助 (&H)")
        about_action = QAction("关于 (&A)", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)

    # ── Central layout ────────────────────────────────

    def _setup_central(self) -> None:
        central = QWidget()
        layout = QVBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Toolbar
        self._setup_toolbar()

        # Stacked widget: compress page / archive page
        self._stack = QStackedWidget()

        from gui.widgets.compression_view import CompressPage
        self._compress_page = CompressPage()
        self._compress_page.add_files_requested.connect(self._on_add_files)
        self._compress_page.add_folder_requested.connect(self._on_add_folder)
        self._compress_page.compress_requested.connect(self._on_compress)
        self._compress_page.clear_requested.connect(self._on_clear)
        self._compress_page.file_selected.connect(self._on_file_selected)
        self._stack.addWidget(self._compress_page)  # index 0

        self._archive_page = ArchivePage()
        self._archive_page.extract_requested.connect(self._on_extract)
        self._archive_page.close_requested.connect(self._on_close_archive)
        self._archive_page.entry_double_clicked.connect(self._on_entry_properties)
        self._stack.addWidget(self._archive_page)  # index 1

        layout.addWidget(self._stack)
        self.setCentralWidget(central)

    def _setup_toolbar(self) -> None:
        tb = QToolBar("主工具栏")
        tb.setMovable(False)
        tb.setStyleSheet(
            "QToolBar { background: #f5f5f5; border-bottom: 1px solid #ddd; "
            "padding: 4px 8px; spacing: 6px; }"
        )

        btn_style = (
            "QPushButton { padding: 6px 14px; border: 1px solid #ccc; "
            "border-radius: 4px; font-size: 12px; background: white; }"
            "QPushButton:hover { background: #e8e8e8; }"
        )

        for label, slot in [
            ("+ 添加文件", self._on_add_files),
            ("+ 添加文件夹", self._on_add_folder),
        ]:
            btn = QPushButton(label)
            btn.setStyleSheet(btn_style)
            btn.clicked.connect(slot)
            tb.addWidget(btn)

        self._compress_tb_btn = QPushButton("▶ 压缩")
        self._compress_tb_btn.setStyleSheet(
            "QPushButton { padding: 6px 18px; background: #27ae60; color: white; "
            "font-weight: bold; border: none; border-radius: 4px; font-size: 12px; }"
            "QPushButton:hover { background: #219a52; }"
        )
        self._compress_tb_btn.clicked.connect(self._on_compress)
        tb.addWidget(self._compress_tb_btn)

        open_btn = QPushButton("📂 打开压缩包")
        open_btn.setStyleSheet(btn_style)
        open_btn.clicked.connect(self._on_open_archive)
        tb.addWidget(open_btn)

        tb.addSeparator()

        for label, slot in [
            ("📊 概览", self._open_dashboard),
            ("🔬 分析", self._open_analysis),
            ("⚖ 对比", self._open_comparison),
            ("📡 传输", self._open_network),
        ]:
            btn = QPushButton(label)
            btn.setStyleSheet(btn_style)
            btn.clicked.connect(slot)
            tb.addWidget(btn)

        self.addToolBar(tb)

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

    def _set_status(self, text: str, progress: int = -1) -> None:
        self._status_label.setText(text)
        if progress >= 0:
            self._progress.setValue(progress)

    # ── File operations ───────────────────────────────

    def _on_add_files(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(self, "选择文件", "", "所有文件 (*);;压缩包 (*.compressed)")
        if paths:
            self._add_paths(paths)

    def _on_add_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if folder:
            self._add_paths([folder])

    def _add_paths(self, paths: list[str]) -> None:
        for path in paths:
            if path.endswith('.compressed') and os.path.isfile(path):
                self._open_archive_path(path)
                continue
            if os.path.isdir(path):
                rec = FolderRecord(path)
            elif os.path.isfile(path):
                rec = FileRecord(path)
            else:
                continue
            self._records.append(rec)

        self._compress_page.load_records(self._records)
        count = len(self._records)
        self._set_status(f"已添加 {count} 个项目", 0)

    def _on_clear(self) -> None:
        self._records.clear()
        self._compress_page.load_records([])
        self._set_status("已清空列表", 0)

    def _on_file_selected(self, rec: Record) -> None:
        if isinstance(rec, (FileRecord, ArchiveEntry)):
            from gui.widgets.property_panel import show_property_dialog
            # Don't auto-show dialog on selection; handled via double-click
            pass

    def _on_entry_properties(self, entry: ArchiveEntry) -> None:
        from gui.widgets.property_panel import show_property_dialog
        show_property_dialog(self, entry)

    # ── Compression ───────────────────────────────────

    def _on_compress(self) -> None:
        if self._compress_worker and self._compress_worker.isRunning():
            QMessageBox.warning(self, "提示", "压缩任务正在进行中...")
            return

        records = self._records
        if not records:
            QMessageBox.warning(self, "提示", "请先添加文件或文件夹！")
            return

        tasks = []
        for rec in records:
            name = getattr(rec, 'name', 'file')
            default_name = f"{name}.compressed"
            out_path, _ = QFileDialog.getSaveFileName(
                self, f"保存压缩文件 — {name}",
                default_name, "压缩文件 (*.compressed)")
            if out_path:
                tasks.append((rec, out_path))

        if not tasks:
            return

        self._compress_worker = CompressWorker(tasks, AlgorithmType.DEFLATE)
        self._compress_worker.progress.connect(
            lambda i, msg: self._set_status(msg))
        self._compress_worker.item_finished.connect(self._on_compress_item_done)
        self._compress_worker.all_finished.connect(self._on_compress_all_done)
        self._set_status(f"正在压缩 {len(tasks)} 个项目...", 0)
        self._compress_worker.start()

    def _on_compress_item_done(self, idx: int, result: dict) -> None:
        if result.get('success'):
            self._set_status(f"完成: 压缩率 {result.get('compression_ratio', 1) * 100:.1f}%")
        else:
            self._set_status(f"失败: {result.get('error_message', '未知错误')}")
        self._compress_page.load_records(self._records)

    def _on_compress_all_done(self) -> None:
        self._set_status("全部压缩完成", 100)
        QMessageBox.information(self, "完成", "所有项目已压缩完毕！")
        self._compress_page.load_records(self._records)

    # ── Archive operations ────────────────────────────

    def _on_open_archive(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "打开压缩包", "",
            "压缩文件 (*.compressed);;所有文件 (*)")
        if path:
            self._open_archive_path(path)

    def _open_archive_path(self, path: str) -> None:
        if self._list_worker and self._list_worker.isRunning():
            return

        self._set_status(f"正在读取 {os.path.basename(path)} ...")
        self._list_worker = ArchiveListWorker(path)
        self._list_worker.finished.connect(self._on_archive_listed)
        self._list_worker.error.connect(self._on_archive_error)
        self._list_worker.start()

    def _on_archive_listed(self, entries: list[ArchiveEntry], archive_path: str) -> None:
        self._current_archive = archive_path
        self._archive_page.set_entries(entries, archive_path)
        self._stack.setCurrentIndex(1)
        self._set_status(f"已打开 {os.path.basename(archive_path)} — {len(entries)} 个文件")

    def _on_archive_error(self, msg: str) -> None:
        self._set_status("打开压缩包失败")
        QMessageBox.warning(self, "错误", f"无法打开压缩包:\n{msg}")

    def _on_extract(self) -> None:
        if not self._current_archive:
            return
        if self._extract_worker and self._extract_worker.isRunning():
            QMessageBox.warning(self, "提示", "解压任务正在进行中...")
            return

        # Suggest directory name based on archive name
        default_dir = os.path.splitext(self._current_archive)[0]
        out_dir = QFileDialog.getExistingDirectory(self, "选择解压目录")
        if not out_dir:
            return

        self._extract_worker = ArchiveExtractWorker(self._current_archive, out_dir)
        self._extract_worker.progress.connect(lambda msg: self._set_status(msg))
        self._extract_worker.finished.connect(self._on_extract_done)
        self._set_status("正在解压...")
        self._extract_worker.start()

    def _on_extract_done(self, success: bool, msg: str) -> None:
        if success:
            self._set_status("解压完成", 100)
            QMessageBox.information(self, "解压完成", msg)
        else:
            self._set_status("解压失败")
            QMessageBox.warning(self, "解压失败", msg)

    def _on_close_archive(self) -> None:
        self._current_archive = ""
        self._stack.setCurrentIndex(0)
        self._set_status("已关闭压缩包", 0)

    # ── Drag & drop ───────────────────────────────────

    def dragEnterEvent(self, event) -> None:
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event) -> None:
        urls = event.mimeData().urls()
        paths = [url.toLocalFile() for url in urls if url.isLocalFile()]
        if paths:
            for p in paths:
                if p.endswith('.compressed') and os.path.isfile(p):
                    self._open_archive_path(p)
                elif os.path.isdir(p) or os.path.isfile(p):
                    self._add_paths([p])

    # ── Analysis dialogs ──────────────────────────────

    def _open_dashboard(self) -> None:
        from gui.widgets.dashboard_view import DashboardView
        dlg = QDialog(self)
        dlg.setWindowTitle("项目概览")
        dlg.resize(750, 520)
        layout = QVBoxLayout(dlg)
        view = DashboardView()
        view.update_from_records(self._records)
        layout.addWidget(view)
        dlg.exec()

    def _open_analysis(self) -> None:
        from gui.widgets.analysis_view import AnalysisView
        dlg = QDialog(self)
        dlg.setWindowTitle("压缩分析")
        dlg.resize(800, 600)
        layout = QVBoxLayout(dlg)
        view = AnalysisView()
        view.set_records(self._records)
        layout.addWidget(view)
        dlg.exec()

    def _open_comparison(self) -> None:
        from gui.widgets.comparison_view import ComparisonView
        dlg = QDialog(self)
        dlg.setWindowTitle("工具对比")
        dlg.resize(700, 450)
        layout = QVBoxLayout(dlg)
        view = ComparisonView()
        # Set the first available file with compression results
        for rec in self._records:
            files = rec.files if isinstance(rec, FolderRecord) else [rec]
            for f in files:
                if isinstance(f, FileRecord) and f.status == CompressionStatus.DONE:
                    view.set_file(
                        f.path,
                        {
                            'compressed_size': len(f.compressed_data) if f.compressed_data else f.size,
                            'compression_ratio': f.compression_ratio,
                            'time_ms': f.compression_time_ms,
                        }
                    )
                    break
        layout.addWidget(view)
        dlg.exec()

    def _open_network(self) -> None:
        from gui.widgets.network_view import NetworkView
        dlg = QDialog(self)
        dlg.setWindowTitle("网络传输模拟")
        dlg.resize(700, 500)
        layout = QVBoxLayout(dlg)
        view = NetworkView()
        view.update_from_records(self._records)
        layout.addWidget(view)
        dlg.exec()

    # ── About ─────────────────────────────────────────

    def _on_about(self) -> None:
        QMessageBox.about(
            self,
            "关于 WebCompress",
            "WebCompress — 压缩工具\n\n"
            "基于 Deflate/LZ77 与 Huffman 编码的高效压缩。\n"
            "支持文件夹打包、压缩包浏览与解压。\n\n"
            "© 2026"
        )

    # ── Cleanup ───────────────────────────────────────

    def closeEvent(self, event) -> None:
        for w in [self._compress_worker, self._list_worker, self._extract_worker]:
            if w and w.isRunning():
                w.quit()
                w.wait()
        event.accept()
