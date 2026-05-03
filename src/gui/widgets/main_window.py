from __future__ import annotations

import logging
import os
import tempfile

from PyQt6.QtCore import Qt, QMimeData, QUrl, QThread, pyqtSignal
from PyQt6.QtGui import QAction, QDragEnterEvent, QDropEvent, QBrush
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QMessageBox, QToolBar, QWidget,
    QStatusBar, QProgressBar, QLabel, QVBoxLayout, QHBoxLayout,
    QTableWidget, QTableWidgetItem, QComboBox, QMenu, QDialog, QPushButton
)

logger = logging.getLogger("gui.main_window")

from gui.core.models import Record, FileRecord, FolderRecord, CompressionStatus, AlgorithmType, formatted_size


WINDOW_WIDTH = 900
WINDOW_HEIGHT = 700








# ============================================================
#  压缩工作线程
# ============================================================

class CompressionWorker(QThread):
    """后台压缩工作线程"""

    progress = pyqtSignal(int, str) # 考虑删除
    finished_row = pyqtSignal(int)  # (行号)
    error = pyqtSignal(int)

    def __init__(self, tasks: list[tuple[int, Record]], algorithm: AlgorithmType = AlgorithmType.DEFLATE):
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
                    try:
                        import strategy
                        decision_engine = strategy.RamdomForest()
                        record.algorithm = decision_engine.decide(record)
                    except Exception as e:
                        logger.warning("[compress] AUTO decision failed, fallback: %s", e)
                        record.algorithm = AlgorithmType.DEFLATE

            # Streaming compress: file → temp file, constant memory
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
                # Update folder record status after all child files done
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
                    record.status = CompressionStatus.DONE  # partial
                record.compression_time_ms = sum(
                    f.compression_time_ms for f in record.files)
                self.finished_row.emit(row_idx)
            elif isinstance(record, FileRecord):
                self.single_compress(row_idx, record)





# ============================================================
#  解压工作线程
# ============================================================

class DecompressWorker(QThread):
    finished_file = pyqtSignal(str, bool, str)  # path, success, message

    def __init__(self, tasks: list[tuple[str, str]]):
        """tasks: list of (input_path, output_path)"""
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


# ============================================================
#  文件表格组件
# ============================================================

class FileTableWidget(QTableWidget):
    """文件列表表格，封装文件添加、状态更新等操作"""
    COL_CHECK = 0
    COL_NAME = 1
    COL_SIZE = 2
    COL_TYPE = 3
    COL_STATUS = 4
    COL_RATIO = 5

    Record_Role = Qt.ItemDataRole.UserRole
    Check_Role = Qt.ItemDataRole.UserRole + 1

    selection_changed = pyqtSignal()
    request_demo = pyqtSignal(int)  # row

    def __init__(self, parent=None):
        super().__init__(parent)
        self._selected: set[int] = set()
        self._setup_ui()

    def _setup_ui(self) -> None:
        self.setColumnCount(6)
        self.setHorizontalHeaderLabels(["☐", "文件名", "大小", "类型", "状态", "压缩率"])
        self.horizontalHeader().setStretchLastSection(True)
        self.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.setAlternatingRowColors(True)

        self.setColumnWidth(self.COL_CHECK, 36)
        self.setColumnWidth(self.COL_NAME, 260)
        self.setColumnWidth(self.COL_SIZE, 100)
        self.setColumnWidth(self.COL_TYPE, 80)
        self.setColumnWidth(self.COL_STATUS, 100)
        self.setColumnWidth(self.COL_RATIO, 80)

    def mousePressEvent(self, event) -> None:
        try:
            item = self.itemAt(event.pos())
            if item is not None:
                row = item.row()
                self._toggle_row(row)
                return
            super().mousePressEvent(event)
        except Exception as e:
            logger.exception("mousePressEvent crash: %s", e)

    def contextMenuEvent(self, event) -> None:
        try:
            item = self.itemAt(event.pos())
            if item is None:
                return
            row = item.row()
            logger.debug("contextMenuEvent: row=%d", row)
            menu = QMenu(self)
            delete_action = menu.addAction("🗑 删除该行")
            menu.addSeparator()
            demo_action = menu.addAction("🔧 压缩演示")
            action = menu.exec(event.globalPos())
            if action == delete_action:
                logger.info("右键删除行: %d", row)
                self.remove_row(row)
            elif action == demo_action:
                logger.info("右键压缩演示: 行%d", row)
                self.request_demo.emit(row)
        except Exception as e:
            logger.exception("contextMenuEvent crash: %s", e)

    def remove_row(self, row: int) -> None:
        try:
            logger.info("remove_row: 删除行%d, 当前选中=%s", row, sorted(self._selected))
            self._selected.discard(row)
            self.removeRow(row)
            reindex = set()
            for r in self._selected:
                if r > row:
                    reindex.add(r - 1)
                else:
                    reindex.add(r)
            self._selected = reindex
            self.selection_changed.emit()
            logger.info("remove_row 完成: 选中=%s", sorted(self._selected))
        except Exception as e:
            logger.exception("remove_row crash: row=%d, err=%s", row, e)

    def _toggle_row(self, row: int) -> None:
        try:
            check_item = self.item(row, self.COL_CHECK)
            if check_item is None:
                logger.warning("_toggle_row: 行%d 无check_item", row)
                return
            checked = check_item.data(self.Check_Role) or False
            logger.debug("_toggle_row: row=%d, checked=%s -> %s", row, checked, not checked)
            check_item.setData(self.Check_Role, not checked)
            check_item.setText("☑" if not checked else "☐")

            bg = QBrush(Qt.GlobalColor.lightGray) if not checked else QBrush()
            for col in range(1, self.columnCount()):
                ci = self.item(row, col)
                if ci:
                    ci.setBackground(bg)

            if not checked:
                self._selected.add(row)
            else:
                self._selected.discard(row)
            self.selection_changed.emit()
            logger.debug("_toggle_row 完成: row=%d, selected=%s", row, sorted(self._selected))
        except Exception as e:
            logger.exception("_toggle_row crash: row=%d, err=%s", row, e)

    def select_all(self, checked: bool = True) -> None:
        for row in range(self.rowCount()):
            check_item = self.item(row, self.COL_CHECK)
            if check_item is None:
                continue
            current = check_item.data(self.Check_Role) or False
            if current != checked:
                self._toggle_row(row)
        if not checked:
            self._selected.clear()
        self.selection_changed.emit()

    @property
    def selected_rows(self) -> list[int]:
        return sorted(self._selected)

    @property
    def is_all_selected(self) -> bool:
        return self.rowCount() > 0 and len(self._selected) == self.rowCount()


    def add_file(self, path: str) -> int:
        try:
            logger.info("add_file: %s", path)
            record = FileRecord(path)

            row = self.rowCount()
            self.insertRow(row)

            check_item = QTableWidgetItem("☐")
            check_item.setData(self.Check_Role, False)
            self.setItem(row, self.COL_CHECK, check_item)

            name_item = QTableWidgetItem(record.name)
            name_item.setData(self.Record_Role, record)

            self.setItem(row, self.COL_NAME, name_item)
            self.setItem(row, self.COL_SIZE, QTableWidgetItem(formatted_size(record.size)))
            self.setItem(row, self.COL_TYPE, QTableWidgetItem(record.type.value))
            self.setItem(row, self.COL_STATUS, QTableWidgetItem(record.status.value))
            self.setItem(row, self.COL_RATIO, QTableWidgetItem("--"))

            logger.debug("add_file 完成: row=%d, name=%s", row, record.name)
            return row
        except Exception as e:
            logger.exception("add_file crash: path=%s, err=%s", path, e)
            return -1

    def add_folder(self, path: str) -> int:
        record = FolderRecord(path)

        row = self.rowCount()
        self.insertRow(row)

        check_item = QTableWidgetItem("☐")
        check_item.setData(self.Check_Role, False)
        self.setItem(row, self.COL_CHECK, check_item)

        name_item = QTableWidgetItem(f"[{record.name}]")
        name_item.setData(self.Record_Role, record)
        name_item.setForeground(Qt.GlobalColor.gray)

        self.setItem(row, self.COL_NAME, name_item)
        self.setItem(row, self.COL_SIZE, QTableWidgetItem(f"{record.filenum} 文件 / {formatted_size(record.size)}"))
        self.setItem(row, self.COL_TYPE, QTableWidgetItem("Folder"))
        self.setItem(row, self.COL_STATUS, QTableWidgetItem(record.status.value))
        self.setItem(row, self.COL_RATIO, QTableWidgetItem("--"))
        return row

    def add_paths(self, paths: list[str]) -> tuple[int, int]:
        count_files = 0
        count_dirs = 0
        for path in paths:
            if os.path.isdir(path):
                self.add_folder(path)
                count_dirs += 1
            elif os.path.isfile(path):
                self.add_file(path)
                count_files += 1
        return count_files, count_dirs

    def update(self, row: int) -> None:
        try:
            item = self.item(row, self.COL_NAME)
            if not item:
                return
            record = item.data(self.Record_Role)
            if not record:
                return
            self.setItem(row, self.COL_STATUS, QTableWidgetItem(record.status.value))
            ratio_str = f"{record.compression_ratio:.2f}%" if (record.status == CompressionStatus.DONE and record.size > 0) else "--"
            self.setItem(row, self.COL_RATIO, QTableWidgetItem(ratio_str))
        except Exception as e:
            logger.error("[FileTableWidget.update] CRASH row=%d: %s", row, e, exc_info=True)

    def get_record(self, row: int) -> Record | None:
        item = self.item(row, self.COL_NAME)
        if not item:
            return None
        return item.data(self.Record_Role)

    def get_records(self, row_list: list[int] | None = None) -> list[Record]:
        if row_list is None:
            row_list = self.selected_rows
        records: list[Record] = []
        for row in row_list:
            rec = self.get_record(row)
            if rec is not None:
                records.append(rec)
        return records

    def mark_error(self, row: int) -> None:
        try:
            self.setItem(row, self.COL_STATUS,
                         QTableWidgetItem(CompressionStatus.FAILED.value))
            item = self.item(row, self.COL_STATUS)
            if item:
                item.setForeground(Qt.GlobalColor.red)
        except Exception as e:
            logger.error("[mark_error] CRASH row=%d: %s", row, e, exc_info=True)

    def clear_all(self) -> None:
        self._selected.clear()
        self.setRowCount(0)



# ============================================================
#  状态栏组件
# ============================================================

class StatusBarWidget(QWidget):
    """自定义状态栏，包含状态文本和进度条"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._status_label = QLabel("就绪")
        layout.addWidget(self._status_label, stretch=1)

        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setFixedWidth(200)
        self._progress.setTextVisible(True)
        layout.addWidget(self._progress)

    def set_status_text(self, text: str) -> None:
        self._status_label.setText(text)

    def set_progress_value(self, value: int) -> None:
        self._progress.setValue(value)

    def reset(self) -> None:
        self.set_status_text("就绪")
        self.set_progress_value(0)


# ============================================================
#  算法选择器
# ============================================================

class AlgorithmSelector(QComboBox):
    """算法选择下拉框"""

    ALGORITHMS = [
        ("Deflate", AlgorithmType.DEFLATE),
        ("Auto", AlgorithmType.AUTO),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        for label, value in self.ALGORITHMS:
            self.addItem(label, value)
        self.setToolTip("选择压缩算法")

    @property
    def current_algorithm(self) -> AlgorithmType:
        return self.currentData()

# ============================================================
#  压缩演示对话框
# ============================================================

class CompressDemoDialog(QDialog):
    def __init__(self, record: Record, algorithm: AlgorithmType, parent=None):
        super().__init__(parent)
        self._record = record
        self._algorithm = algorithm
        self.setWindowTitle(f"压缩演示 - {record.name}")
        self.setMinimumWidth(500)
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        info_layout = QVBoxLayout()
        info_layout.addWidget(QLabel(f"<b>文件:</b> {self._record.path}"))
        info_layout.addWidget(QLabel(f"<b>大小:</b> {formatted_size(self._record.size)}"))
        info_layout.addWidget(QLabel(f"<b>类型:</b> {self._record.type.value}"))
        info_layout.addWidget(QLabel(f"<b>算法:</b> {self._algorithm.value}"))

        status_text = self._record.status.value if hasattr(self._record, 'status') else "未压缩"
        info_layout.addWidget(QLabel(f"<b>状态:</b> {status_text}"))

        if hasattr(self._record, 'compressed_data') and self._record.compressed_data:
            ratio = self._record.compression_ratio if hasattr(self._record, 'compression_ratio') else 0
            info_layout.addWidget(QLabel(f"<b>压缩后:</b> {formatted_size(len(self._record.compressed_data))}"))
            info_layout.addWidget(QLabel(f"<b>压缩率:</b> {ratio:.1f}%"))

        layout.addLayout(info_layout)
        layout.addSpacing(16)

        btn_layout = QHBoxLayout()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addStretch()
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


# ============================================================
#  主窗口
# ============================================================

"""
note:
做一个高级选项
用于激活脚本文件的token字典,用于提高压缩效率
"""

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("WebCompress")
        self.resize(WINDOW_WIDTH, WINDOW_HEIGHT)

        self.setAcceptDrops(True)
        self._worker: CompressionWorker | None = None
        self._decompress_worker: DecompressWorker | None = None

        # 组件实例化
        self._table = FileTableWidget()
        self._statusbar = StatusBarWidget()
        self._algo_selector = AlgorithmSelector()

        self._setup_menu()
        self._setup_toolbar()
        self._setup_central()

        # 安装状态栏
        bar = QStatusBar()
        self.setStatusBar(bar)
        bar.addPermanentWidget(self._statusbar)

        self._table.request_demo.connect(self._on_compress_demo)

    # ========== 菜单设置 ==========
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

        help_menu = bar.addMenu("帮助 (&H)")

        about_action = QAction("关于 (&A)", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)

    # ========== 工具栏设置 ==========
    def _setup_toolbar(self) -> None:
        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)

        self._select_all_action = QAction("☐ 全选", self)
        self._select_all_action.setToolTip("全选/取消全选")
        self._select_all_action.triggered.connect(self._on_toggle_select_all)
        self._table.selection_changed.connect(self._update_select_button)
        toolbar.addAction(self._select_all_action)

        toolbar.addSeparator()

        toolbar.addWidget(self._algo_selector)
        toolbar.addSeparator()

        self._compress_action = QAction("▶️ 开始压缩", self)
        self._compress_action.setToolTip("开始压缩选中的文件")
        self._compress_action.triggered.connect(self._on_compress)
        toolbar.addAction(self._compress_action)

        toolbar.addSeparator()

        export_action = QAction("💾 导出结果", self)
        export_action.setToolTip("导出压缩后的文件")
        export_action.triggered.connect(self._on_export)
        toolbar.addAction(export_action)

        toolbar.addSeparator()

        decompress_action = QAction("🔓 解压文件", self)
        decompress_action.setToolTip("解压 .compressed 文件")
        decompress_action.triggered.connect(self._on_decompress)
        toolbar.addAction(decompress_action)

    # ========== 中央区域 ==========
    def _setup_central(self) -> None:
        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addWidget(self._table)
        self.setCentralWidget(central)

    # ========== 文件操作 ==========
    def _on_add_files(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(self, "选择文件", "", "所有文件 (*)")
        if paths:
            self._add_paths(paths)

    def _on_add_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if folder:
            self._add_paths([folder])

    def _add_paths(self, paths: list[str]) -> None:
        files, dirs = self._table.add_paths(paths)
        parts = []
        if files > 0:
            parts.append(f"{files} 个文件")
        if dirs > 0:
            parts.append(f"{dirs} 个文件夹")
        if parts:
            self._statusbar.set_status_text(f"已添加 {', '.join(parts)}")

    def _on_clear(self) -> None:
        self._table.clear_all()
        self._statusbar.set_status_text("已清空列表")
        self._update_select_button()

    def _on_toggle_select_all(self) -> None:
        self._table.select_all(checked=not self._table.is_all_selected)

    def _update_select_button(self) -> None:
        if self._table.is_all_selected:
            self._select_all_action.setText("☑ 取消全选")
            self._select_all_action.setToolTip("取消全选所有文件")
        else:
            self._select_all_action.setText("☐ 全选")
            self._select_all_action.setToolTip("选中所有文件")

    # ========== 拖拽支持 ==========
    def dragEnterEvent(self, event: QDragEnterEvent) -> None:
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event: QDropEvent) -> None:
        urls = event.mimeData().urls()
        paths = [url.toLocalFile() for url in urls if url.isLocalFile()]
        if paths:
            self._add_paths(paths)

    # ========== 压缩操作 ==========
    def _on_compress(self) -> None:
        if self._worker and self._worker.isRunning():
            QMessageBox.warning(self, "提示", "压缩任务正在进行中...")
            return

        records = self._table.get_records()
        if not records:
            QMessageBox.warning(self, "提示", "请先选中文件！")
            return

        tasks = list(enumerate(records))

        self._compress_action.setEnabled(False)

        self._worker = CompressionWorker(tasks, self._algo_selector.current_algorithm)
        self._worker.finished_row.connect(self._on_row_finished)
        self._worker.error.connect(self._table.mark_error)
        self._worker.finished.connect(self._on_compression_finished)

        file_count = sum(len(r.files) if isinstance(r, FolderRecord) else 1 for r in records)
        self._statusbar.set_status_text(f"开始压缩 {file_count} 个文件...")
        self._statusbar.set_progress_value(0)
        self._worker.start()

    def _on_row_finished(self, row: int) -> None:
        try:
            self._table.update(row)
            done = sum(
                1 for r in range(self._table.rowCount())
                if (item := self._table.item(r, self._table.COL_STATUS)) and item.text() in (CompressionStatus.DONE.value, CompressionStatus.FAILED.value)
            )
            total = self._table.rowCount()
            progress = int(done / total * 100) if total else 0
            self._statusbar.set_progress_value(progress)
        except Exception as e:
            logger.error("[_on_row_finished] CRASH row=%d: %s", row, e, exc_info=True)

    def _on_compression_finished(self) -> None:
        try:
            self._compress_action.setEnabled(True)
            self._statusbar.set_progress_value(100)
            self._statusbar.set_status_text("压缩完成")
            QMessageBox.information(self, "完成", "所有文件已处理完毕！")
        except Exception as e:
            logger.error("[_on_compression_finished] CRASH: %s", e, exc_info=True)

    # ========== 压缩演示 ==========
    def _on_compress_demo(self, row: int) -> None:
        record = self._table.get_record(row)
        if record is None:
            return
        dlg = CompressDemoDialog(record, self._algo_selector.current_algorithm, parent=self)
        dlg.exec()

    # ========== 导出操作 ==========
    def _on_export(self) -> None:
        export_dir = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not export_dir:
            return

        selected = self._table.selected_rows
        if not selected:
            QMessageBox.warning(self, "提示", "没有选中的文件")
            return

        exported = 0
        for row in selected:
            status_item = self._table.item(row, self._table.COL_STATUS)
            if status_item and status_item.text() == CompressionStatus.DONE.value:
                name_item = self._table.item(row, self._table.COL_NAME)
                record = self._table.get_record(row)
                if name_item and record and record.compressed_data:
                    original_name = name_item.text()
                    export_path = os.path.join(export_dir, f"{original_name}.compressed")
                    with open(export_path, 'wb') as f:
                        data = record.compressed_data
                        if isinstance(data, list):
                            data = bytes(data)
                        f.write(data)
                    exported += 1

        if exported > 0:
            self._statusbar.set_status_text(f"已导出 {exported} 个文件到 {export_dir}")
            QMessageBox.information(self, "导出完成", f"已成功导出 {exported} 个文件！")
        else:
            QMessageBox.warning(self, "提示", "没有可导出的文件（请先压缩）")

    # ========== 解压操作 ==========
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
            lambda: self._statusbar.set_status_text("解压完成"))
        self._statusbar.set_status_text(f"解压 {len(tasks)} 个文件...")
        self._decompress_worker.start()

    def _on_decompress_finished(self, path: str, success: bool, msg: str):
        if success:
            self._statusbar.set_status_text(f"解压成功: {os.path.basename(path)} ({msg})")
        else:
            logger.error("解压失败: %s - %s", path, msg)
            QMessageBox.warning(self, "解压失败", f"{path}\n{msg}")

    # ========== 关于 ==========
    def _on_about(self) -> None:
        QMessageBox.about(
            self,
            "关于 WebCompress Pro",
            "WebCompress Pro v1.0\n\n"
            "一个基于 Deflate/LZSS 算法的网页压缩工具。\n\n"
            "© 2026 数据结构课程设计"
        )
