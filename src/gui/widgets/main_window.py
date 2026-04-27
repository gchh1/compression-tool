from __future__ import annotations

from PyQt6.QtCore import Qt, QMimeData, QUrl, QThread, pyqtSignal
from PyQt6.QtGui import QAction, QDragEnterEvent, QDropEvent
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QMessageBox, QToolBar, QWidget,
    QStatusBar, QProgressBar, QLabel, QVBoxLayout, QTableWidget,
    QTableWidgetItem, QComboBox
)

WINDOW_WIDTH = 900
WINDOW_HEIGHT = 700

COL_NAME = 0
COL_SIZE = 1
COL_TYPE = 2
COL_STATUS = 3
COL_RATIO = 4


class CompressionWorker(QThread):
    """后台压缩工作线程"""
    
    progress = pyqtSignal(int, str)  # (行号, 状态)
    finished_row = pyqtSignal(int, object)  # (行号, CompressionResult)
    error = pyqtSignal(int, str)  # (行号, 错误信息)
    
    def __init__(self, file_list: list[tuple[int, str]]):
        super().__init__()
        self._file_list = file_list
    
    def run(self):
        from gui.core.engine import CompressionEngine
        from gui.core.models import AlgorithmType
        
        engine = CompressionEngine()
        
        for row_idx, file_path in self._file_list:
            try:
                self.progress.emit(row_idx, "压缩中...")
                
                with open(file_path, 'rb') as f:
                    data = f.read()
                
                if engine.available:
                    result = engine.compress(data, AlgorithmType.DEFLATE)
                    ratio = f"{result.compression_ratio:.1f}%"
                else:
                    import zlib
                    compressed = zlib.compress(data)
                    ratio = f"{(1 - len(compressed)/len(data)) * 100:.1f}%"
                
                self.finished_row.emit(row_idx, {
                    'ratio': ratio,
                    'original_size': len(data),
                    'compressed_size': len(compressed) if not engine.available else result.compressed_size
                })
                self.progress.emit(row_idx, "已完成")
                
            except Exception as e:
                self.error.emit(row_idx, str(e))
                self.progress.emit(row_idx, "失败")


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("WebCompress Pro")
        self.resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        
        # 启用拖拽
        self.setAcceptDrops(True)
        
        # 压缩线程
        self._worker = None
        
        self._setup_menu()
        self._setup_toolbar()
        self._setup_statusbar()
        self._setup_central()

    def _setup_menu(self) -> None:
        bar = self.menuBar()

        file_menu = bar.addMenu("文件 (&F)")

        add_file_action = QAction("添加文件 (&F)", self)
        add_file_action.setShortcut("Ctrl+F")
        add_file_action.setToolTip("选择要压缩的文件")
        add_file_action.triggered.connect(self._on_browse)
        file_menu.addAction(add_file_action)

        add_folder_action = QAction("添加文件夹 (&D)", self)
        add_folder_action.setShortcut("Ctrl+D")
        add_folder_action.setToolTip("选择要压缩的文件夹")
        add_folder_action.triggered.connect(self._on_add_folders)
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

    def _setup_toolbar(self) -> None:
        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)

        # 算法选择
        self._algo_combo = QComboBox()
        self._algo_combo.addItem("Deflate (推荐)", "deflate")
        self._algo_combo.addItem("LZSS", "lzss")
        self._algo_combo.setToolTip("选择压缩算法")
        toolbar.addWidget(self._algo_combo)

        toolbar.addSeparator()

        self._compress_action = QAction("▶️ 开始压缩", self)
        self._compress_action.setToolTip("开始压缩选中的文件")
        self._compress_action.triggered.connect(self._on_compress)
        toolbar.addAction(self._compress_action)

        toolbar.addSeparator()

        self._export_action = QAction("💾 导出结果", self)
        self._export_action.setToolTip("导出压缩后的文件")
        self._export_action.triggered.connect(self._on_export)
        toolbar.addAction(self._export_action)

    def _setup_statusbar(self) -> None:
        bar = QStatusBar()
        self.setStatusBar(bar)

        self._status_label = QLabel("就绪")
        bar.addPermanentWidget(self._status_label, stretch=1)

        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setFixedWidth(200)
        self._progress.setTextVisible(True)
        bar.addPermanentWidget(self._progress)

    def _setup_central(self) -> None:
        central = QWidget()
        layout = QVBoxLayout(central)

        self._table = QTableWidget()
        self._table.setColumnCount(5)
        self._table.setHorizontalHeaderLabels(["文件名", "大小", "类型", "状态", "压缩率"])
        self._table.horizontalHeader().setStretchLastSection(True)
        self._table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self._table.setAlternatingRowColors(True)

        self._table.setColumnWidth(COL_NAME, 280)
        self._table.setColumnWidth(COL_SIZE, 100)
        self._table.setColumnWidth(COL_TYPE, 80)
        self._table.setColumnWidth(COL_STATUS, 100)
        self._table.setColumnWidth(COL_RATIO, 80)

        layout.addWidget(self._table)
        self.setCentralWidget(central)

    def _set_status(self, text: str) -> None:
        self._status_label.setText(text)

    def _set_progress(self, value: int) -> None:
        self._progress.setValue(value)

    def _on_browse(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(
            self, "选择文件", "", "所有文件 (*)"
        )
        if paths:
            self._add_paths(paths)

    def _on_add_folders(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if folder:
            self._add_paths([folder])

    # ========== 拖拽支持 ==========
    def dragEnterEvent(self, event: QDragEnterEvent) -> None:
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event: QDropEvent) -> None:
        urls = event.mimeData().urls()
        paths = [url.toLocalFile() for url in urls if url.isLocalFile()]
        if paths:
            self._add_paths(paths)
    
    def _on_clear(self) -> None:
        self._table.setRowCount(0)
        self._set_status("已清空列表")

    def _add_paths(self, paths: list[str]) -> None:
        import os
        ext_map = {
            ".html": "网页", ".htm": "网页",
            ".css": "样式", ".js": "脚本",
            ".json": "数据", ".xml": "数据",
            ".png": "图片", ".jpg": "图片", ".jpeg": "图片",
            ".gif": "图片", ".svg": "图片", ".ico": "图标",
            ".txt": "文本", ".md": "文本",
        }
        count_files = 0
        count_dirs = 0
        
        for path in paths:
            if os.path.isdir(path):
                self._add_folder_row(path, ext_map)
                count_dirs += 1
            elif os.path.isfile(path):
                self._add_file_row(path, ext_map)
                count_files += 1
        
        msg_parts = []
        if count_files > 0:
            msg_parts.append(f"{count_files} 个文件")
        if count_dirs > 0:
            msg_parts.append(f"{count_dirs} 个文件夹")
        
        if msg_parts:
            self._set_status(f"已添加 {', '.join(msg_parts)}")

    def _add_file_row(self, path: str, ext_map: dict) -> None:
        import os
        name = os.path.basename(path)
        size = os.path.getsize(path)
        ext = os.path.splitext(path)[1].lower()
        file_type = ext_map.get(ext, "其他")
        
        row = self._table.rowCount()
        self._table.insertRow(row)
        
        name_item = QTableWidgetItem(name)
        name_item.setData(Qt.ItemDataRole.UserRole, path)
        
        self._table.setItem(row, COL_NAME, name_item)
        self._table.setItem(row, COL_SIZE, QTableWidgetItem(self._format_size(size)))
        self._table.setItem(row, COL_TYPE, QTableWidgetItem(file_type))
        self._table.setItem(row, COL_STATUS, QTableWidgetItem("等待中"))
        self._table.setItem(row, COL_RATIO, QTableWidgetItem("--"))

    def _add_folder_row(self, path: str, ext_map: dict) -> None:
        import os
        name = os.path.basename(path)
        
        # 统计文件夹内文件数和总大小
        file_count = 0
        total_size = 0
        for f in os.listdir(path):
            fp = os.path.join(path, f)
            if os.path.isfile(fp):
                file_count += 1
                total_size += os.path.getsize(fp)
        
        row = self._table.rowCount()
        self._table.insertRow(row)
        
        name_item = QTableWidgetItem(f"[{name}]")
        name_item.setData(Qt.ItemDataRole.UserRole, path)
        name_item.setForeground(Qt.GlobalColor.blue)  # 文件夹用蓝色标识
        
        self._table.setItem(row, COL_NAME, name_item)
        self._table.setItem(row, COL_SIZE, QTableWidgetItem(f"{file_count} 文件 / {self._format_size(total_size)}"))
        self._table.setItem(row, COL_TYPE, QTableWidgetItem("📁 文件夹"))
        self._table.setItem(row, COL_STATUS, QTableWidgetItem("等待中"))
        self._table.setItem(row, COL_RATIO, QTableWidgetItem("--"))

    def _format_size(self, size: int) -> str:
        if size < 1024:
            return f"{size} B"
        elif size < 1024 * 1024:
            return f"{size / 1024:.1f} KB"
        else:
            return f"{size / (1024 * 1024):.1f} MB"

    def _on_compress(self) -> None:
        if self._worker and self._worker.isRunning():
            QMessageBox.warning(self, "提示", "压缩任务正在进行中...")
            return
        
        # 收集所有文件路径
        file_list = []
        for row in range(self._table.rowCount()):
            item = self._table.item(row, COL_NAME)
            if item:
                path = item.data(Qt.ItemDataRole.UserRole)
                import os
                if os.path.isfile(path):
                    file_list.append((row, path))
        
        if not file_list:
            QMessageBox.warning(self, "提示", "请先添加文件！")
            return
        
        # 禁用按钮
        self._compress_action.setEnabled(False)
        
        # 创建工作线程
        self._worker = CompressionWorker(file_list)
        self._worker.progress.connect(self._update_row_status)
        self._worker.finished_row.connect(self._update_row_result)
        self._worker.error.connect(self._show_error)
        self._worker.finished.connect(self._on_compression_finished)
        
        # 更新状态
        total = len(file_list)
        self._set_status(f"开始压缩 {total} 个文件...")
        self._set_progress(0)
        
        # 启动线程
        self._worker.start()

    def _update_row_status(self, row: int, status: str) -> None:
        self._table.setItem(row, COL_STATUS, QTableWidgetItem(status))
        # 更新进度
        done_count = 0
        total = self._table.rowCount()
        for r in range(total):
            item = self._table.item(r, COL_STATUS)
            if item and item.text() in ("已完成", "失败"):
                done_count += 1
        progress = int(done_count / total * 100) if total > 0 else 0
        self._set_progress(progress)

    def _update_row_result(self, row: int, result: dict) -> None:
        ratio = result.get('ratio', '--')
        self._table.setItem(row, COL_RATIO, QTableWidgetItem(ratio))

    def _show_error(self, row: int, error_msg: str) -> None:
        self._table.setItem(row, COL_STATUS, QTableWidgetItem("失败"))
        self._table.item(row, COL_STATUS).setForeground(Qt.GlobalColor.red)

    def _on_compression_finished(self) -> None:
        self._compress_action.setEnabled(True)
        self._set_progress(100)
        self._set_status("压缩完成")
        QMessageBox.information(self, "完成", "所有文件已处理完毕！")

    def _on_export(self) -> None:
        export_dir = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not export_dir:
            return
        
        import os
        exported = 0
        for row in range(self._table.rowCount()):
            item = self._table.item(row, COL_STATUS)
            if item and item.text() == "已完成":
                name_item = self._table.item(row, COL_NAME)
                if name_item:
                    original_name = name_item.text()
                    export_path = os.path.join(export_dir, f"{original_name}.compressed")
                    # TODO: 实际保存压缩数据
                    with open(export_path, 'wb') as f:
                        f.write(b'compressed_data_placeholder')
                    exported += 1
        
        if exported > 0:
            self._set_status(f"已导出 {exported} 个文件到 {export_dir}")
            QMessageBox.information(self, "导出完成", f"已成功导出 {exported} 个文件！")
        else:
            QMessageBox.warning(self, "提示", "没有可导出的文件（请先压缩）")

    def _on_about(self) -> None:
        QMessageBox.about(
            self,
            "关于 WebCompress Pro",
            "WebCompress Pro v1.0\n\n"
            "一个基于 Deflate/LZSS 算法的网页压缩工具。\n\n"
            "© 2026 数据结构课程设计"
        )
