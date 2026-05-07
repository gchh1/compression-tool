from __future__ import annotations

import logging
import os
from pathlib import Path

from PyQt6.QtCore import Qt, QMimeData, QUrl, QThread, pyqtSignal
from PyQt6.QtGui import QAction, QDragEnterEvent, QDropEvent, QBrush, QColor
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QMessageBox, QToolBar, QWidget,
    QStatusBar, QProgressBar, QLabel, QVBoxLayout, QHBoxLayout,
    QTableWidget, QTableWidgetItem, QComboBox, QMenu, QDialog, QPushButton,
    QTabWidget, QFormLayout, QSpinBox, QGroupBox, QScrollArea, QColorDialog
)

logger = logging.getLogger("gui.main_window")

from gui.core.models import Record, FileRecord, FolderRecord, CompressionStatus, AlgorithmType, ResourceType, formatted_size, ALGORITHM_PARAMS, get_default_config, LZMINE_DP_VIZ_MAX_SIZE
from gui.core.theme import ThemeManager


def _compressed_size(record: Record) -> int:
    """Get compressed size, supporting both in-memory and file-path streaming modes."""
    if hasattr(record, 'compressed_data') and record.compressed_data is not None:
        return len(record.compressed_data)
    if hasattr(record, 'compressed_path') and record.compressed_path and Path(record.compressed_path).exists():
        return Path(record.compressed_path).stat().st_size
    if hasattr(record, 'size'):
        return record.size
    return 0


WINDOW_WIDTH = 900
WINDOW_HEIGHT = 700


# ============================================================
#  压缩工作线程
# ============================================================

class CompressionWorker(QThread):
    progress = pyqtSignal(int, str)
    row_started = pyqtSignal(int)
    finished_row = pyqtSignal(int)
    error = pyqtSignal(int)

    def __init__(self, tasks: list[tuple[int, Record]], algorithm: AlgorithmType = AlgorithmType.LZMINE):
        super().__init__()
        self.tasks = tasks
        self.algorithm = algorithm
        self._is_cancelled = False

    def cancel(self):
        self._is_cancelled = True

    def single_compress(self, row_idx: int, record: Record, folder_ref: FolderRecord | None = None) -> None:
        import traceback
        if self._is_cancelled:
            record.status = CompressionStatus.FAILED
            record.error_message = "已取消"
            self.error.emit(row_idx)
            return
        self.row_started.emit(row_idx)
        logger.info("[compress] START row=%d file=%s algo=%s", row_idx, getattr(record, 'path', '?'), self.algorithm.value)
        try:
            from gui.core.engine import CompressionEngine
            engine = CompressionEngine()
            if not engine.available:
                raise RuntimeError("C++ core_engine not available")

            use_streaming = hasattr(record, 'size') and engine.should_use_streaming(record.size)

            if isinstance(record, FileRecord):
                record.algorithm = self.algorithm
                if self.algorithm == AlgorithmType.AUTO:
                    record.algorithm = AlgorithmType.LZMINE

            if use_streaming and hasattr(record, 'path'):
                import os
                logger.info("[compress] STREAMING mode for %d bytes (file-to-file)", record.size)
                out_path = record.path + ".wcx"
                result = engine.smart_compress_file(record.path, out_path, record.algorithm)
                logger.info("[compress] streaming compress done: %d -> %d bytes", record.size, result.compressed_size)

                original_size = record.size
                compressed_size = result.compressed_size

                if compressed_size >= original_size:
                    logger.info("[compress] EXPANSION detected (streaming), copying raw")
                    import shutil
                    shutil.copy2(record.path, out_path)
                    record.compressed_path = out_path
                    record.compressed_data = None
                    record.algorithm = AlgorithmType.NONE
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = 1.0
                    record.is_stored = True
                else:
                    record.compressed_path = out_path
                    record.compressed_data = None
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
                    record.is_stored = False

                if hasattr(result, 'error_message') and result.error_message:
                    record.status = CompressionStatus.FAILED
                    record.error_message = result.error_message
                    self.error.emit(row_idx)
                else:
                    record.status = CompressionStatus.DONE
                    if folder_ref is not None:
                        folder_ref.total_original += original_size
                        folder_ref.total_compressed += compressed_size
                        folder_ref.total_time_ms += record.compression_time_ms
                        folder_ref.compression_ratio = (
                            folder_ref.total_compressed / folder_ref.total_original
                            if folder_ref.total_original > 0 else 1.0
                        )
                        folder_ref.compression_time_ms = folder_ref.total_time_ms
                        done_count = sum(1 for f in folder_ref.files if f.status == CompressionStatus.DONE)
                        if done_count == len(folder_ref.files):
                            folder_ref.status = CompressionStatus.DONE
                        else:
                            folder_ref.status = CompressionStatus.COMPRESSING
                    self.finished_row.emit(row_idx)

            else:
                record.load_raw_data()
                record.extract_features()
                logger.info("[compress] loaded raw data: %d bytes", len(record.raw_data))

                logger.info("[compress] compressing with %s ...", record.algorithm.value)
                result = engine.smart_compress(record.raw_data, record.algorithm)
                logger.info("[compress] compress done: %d -> %d bytes", len(record.raw_data), result.compressed_size)

                original_size = len(record.raw_data)
                compressed_size = result.compressed_size

                if compressed_size >= original_size:
                    logger.info("[compress] EXPANSION detected: %d >= %d, falling back to stored (raw)",
                                 compressed_size, original_size)
                    record.compressed_data = record.raw_data
                    record.algorithm = AlgorithmType.NONE
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = 1.0
                    record.is_stored = True
                else:
                    record.compressed_data = bytes(result.data)
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
                    record.is_stored = False

                if hasattr(result, 'error_message') and result.error_message:
                    record.status = CompressionStatus.FAILED
                    record.error_message = result.error_message
                    self.error.emit(row_idx)
                else:
                    record.status = CompressionStatus.DONE
                    if folder_ref is not None:
                        folder_ref.total_original += original_size
                        folder_ref.total_compressed += _compressed_size(record)
                        folder_ref.total_time_ms += record.compression_time_ms
                        folder_ref.compression_ratio = (
                            folder_ref.total_compressed / folder_ref.total_original
                            if folder_ref.total_original > 0 else 1.0
                        )
                        folder_ref.compression_time_ms = folder_ref.total_time_ms
                        done_count = sum(1 for f in folder_ref.files if f.status == CompressionStatus.DONE)
                        if done_count == len(folder_ref.files):
                            folder_ref.status = CompressionStatus.DONE
                        else:
                            folder_ref.status = CompressionStatus.COMPRESSING
                    self.finished_row.emit(row_idx)

        except Exception as e:
            logger.error("[compress] CRASH row=%d file=%s: %s\n%s", row_idx, getattr(record, 'path', '?'), e, traceback.format_exc())
            record.status = CompressionStatus.FAILED
            record.error_message = str(e)
            if folder_ref is not None:
                folder_ref.error_messages.append(f"{record.name}: {e}")
            self.error.emit(row_idx)

    def run(self) -> None:
        for row_idx, record in self.tasks:
            if self._is_cancelled:
                break
            if isinstance(record, FolderRecord):
                record.total_original = 0
                record.total_compressed = 0
                record.total_time_ms = 0.0
                record.compression_ratio = 1.0
                record.error_messages.clear()
                for f in record.files:
                    f.status = CompressionStatus.PENDING
                    f.compressed_data = None
                    f.is_stored = False
                record.status = CompressionStatus.PENDING
                for filerecord in record.files:
                    if self._is_cancelled:
                        break
                    self.single_compress(row_idx, filerecord, folder_ref=record)
            elif isinstance(record, FileRecord):
                record.status = CompressionStatus.PENDING
                record.compressed_data = None
                record.is_stored = False
                self.single_compress(row_idx, record)


# ============================================================
#  文件表格组件
# ============================================================

class FileTableWidget(QTableWidget):
    COL_CHECK = 0
    COL_NAME = 1
    COL_SIZE = 2
    COL_TYPE = 3
    COL_STATUS = 4
    COL_RATIO = 5

    Record_Role = Qt.ItemDataRole.UserRole
    Check_Role = Qt.ItemDataRole.UserRole + 1

    selection_changed = pyqtSignal()
    request_demo = pyqtSignal(int)
    request_heatmap = pyqtSignal(int)
    request_comparison = pyqtSignal(int)
    request_network = pyqtSignal(int)
    request_webpage_heatmap = pyqtSignal(int)
    request_folder_summary = pyqtSignal(int)

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
        self.setColumnWidth(self.COL_STATUS, 130)
        self.setColumnWidth(self.COL_RATIO, 170)

    def mousePressEvent(self, event) -> None:
        try:
            item = self.itemAt(event.pos())
            if item is not None:
                self._toggle_row(item.row())
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
            record = self.get_record(row)
            menu = QMenu(self)
            delete_action = menu.addAction("🗑 删除该行")
            menu.addSeparator()

            if isinstance(record, FolderRecord):
                folder_summary_action = menu.addAction("📋 文件夹压缩报告")
                folder_comparison_action = menu.addAction("⚖ 算法对比")
                webpage_heatmap_action = menu.addAction("🌐 网页资源热力图")
                action = menu.exec(event.globalPos())
                if action == delete_action:
                    self.remove_row(row)
                elif action == folder_summary_action:
                    self.request_folder_summary.emit(row)
                elif action == folder_comparison_action:
                    self.request_comparison.emit(row)
                elif action == webpage_heatmap_action:
                    self.request_webpage_heatmap.emit(row)
            elif isinstance(record, FileRecord):
                if record.status == CompressionStatus.DONE and record.compressed_data:
                    demo_action = menu.addAction("🔧 压缩演示")
                    heatmap_action = menu.addAction("📊 压缩热力图")
                    comparison_action = menu.addAction("⚖ 算法对比")
                    network_action = menu.addAction("🌐 网络传输模拟")
                    action = menu.exec(event.globalPos())
                    if action == delete_action:
                        self.remove_row(row)
                    elif action == demo_action:
                        self.request_demo.emit(row)
                    elif action == heatmap_action:
                        self.request_heatmap.emit(row)
                    elif action == comparison_action:
                        self.request_comparison.emit(row)
                    elif action == network_action:
                        self.request_network.emit(row)
                else:
                    action = menu.exec(event.globalPos())
                    if action == delete_action:
                        self.remove_row(row)
            else:
                action = menu.exec(event.globalPos())
                if action == delete_action:
                    self.remove_row(row)
        except Exception as e:
            logger.exception("contextMenuEvent crash: %s", e)

    def remove_row(self, row: int) -> None:
        try:
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
        except Exception as e:
            logger.exception("remove_row crash: row=%d, err=%s", row, e)

    def _toggle_row(self, row: int) -> None:
        try:
            check_item = self.item(row, self.COL_CHECK)
            if check_item is None:
                return
            checked = check_item.data(self.Check_Role) or False
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
            self.setItem(row, self.COL_STATUS, QTableWidgetItem(CompressionStatus.PENDING.value))
            self.setItem(row, self.COL_RATIO, QTableWidgetItem("--"))
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
        self.setItem(row, self.COL_STATUS, QTableWidgetItem(CompressionStatus.PENDING.value))
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

    def update_row(self, row: int) -> None:
        try:
            item = self.item(row, self.COL_NAME)
            if not item:
                return
            record = item.data(self.Record_Role)
            if not record:
                return

            if isinstance(record, FolderRecord):
                done_count = record.success_count
                total_count = record.filenum
                stored_count = sum(1 for f in record.files if getattr(f, 'is_stored', False))
                if record.status == CompressionStatus.DONE:
                    time_str = f" ({record.total_time_ms:.0f}ms)" if record.total_time_ms > 0 else ""
                    stored_str = f", {stored_count} stored" if stored_count > 0 else ""
                    status_text = f"done {done_count}/{total_count}{stored_str}{time_str}"
                elif record.status == CompressionStatus.COMPRESSING:
                    status_text = f"compressing {done_count}/{total_count}"
                elif record.status == CompressionStatus.FAILED:
                    status_text = f"failed {done_count}/{total_count}"
                else:
                    status_text = record.status.value
                self.setItem(row, self.COL_STATUS, QTableWidgetItem(status_text))

                if record.total_original > 0:
                    ratio_str = f"{record.compression_ratio * 100:.2f}%({formatted_size(record.total_compressed)}/{formatted_size(record.total_original)})"
                else:
                    ratio_str = "--"
                self.setItem(row, self.COL_RATIO, QTableWidgetItem(ratio_str))
            else:
                if record.status == CompressionStatus.DONE:
                    time_str = f" ({record.compression_time_ms:.0f}ms)" if record.compression_time_ms > 0 else ""
                    stored_tag = " [stored]" if getattr(record, 'is_stored', False) else ""
                    self.setItem(row, self.COL_STATUS, QTableWidgetItem(f"done{stored_tag}{time_str}"))
                else:
                    self.setItem(row, self.COL_STATUS, QTableWidgetItem(record.status.value))
                if record.status == CompressionStatus.DONE and record.size > 0:
                    comp_sz = _compressed_size(record)
                    ratio_str = f"{record.compression_ratio * 100:.2f}%({formatted_size(comp_sz)}/{formatted_size(record.size)})"
                else:
                    ratio_str = "--"
                self.setItem(row, self.COL_RATIO, QTableWidgetItem(ratio_str))
        except Exception as e:
            logger.error("[update_row] CRASH row=%d: %s", row, e, exc_info=True)

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
            item = self.item(row, self.COL_NAME)
            if not item:
                return
            record = item.data(self.Record_Role)
            if record:
                record.status = CompressionStatus.FAILED
            self.setItem(row, self.COL_STATUS, QTableWidgetItem(CompressionStatus.FAILED.value))
            status_item = self.item(row, self.COL_STATUS)
            if status_item:
                status_item.setForeground(QBrush(Qt.GlobalColor.red))
        except Exception as e:
            logger.error("[mark_error] CRASH row=%d: %s", row, e, exc_info=True)

    def clear_all(self) -> None:
        self._selected.clear()
        self.setRowCount(0)


# ============================================================
#  状态栏组件
# ============================================================

class StatusBarWidget(QWidget):
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
    ALGORITHMS = [
        ("LZMine (KMP+DP)", AlgorithmType.LZMINE),
        ("MyFlate (KMP+Huffman)", AlgorithmType.MYFLATE),
        ("LZSS", AlgorithmType.LZSS),
        ("Deflate", AlgorithmType.DEFLATE),
        ("Gzip (zlib标准)", AlgorithmType.GZIP),
        ("Transformer (beta) 🧠", AlgorithmType.TRANSFORMER),
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

# ============================================================
#  算法配置对话框
# ============================================================

class ThemeConfigDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("主题配置")
        self.setMinimumSize(580, 600)
        self._color_buttons: dict[str, QPushButton] = {}
        self._setup_ui()

    def _setup_ui(self) -> None:
        try:
            from gui.core.theme import ThemeManager, THEME_FIELDS, LABELS_CN
            from gui.core.app_config import get_theme_config

            layout = QVBoxLayout(self)

            title_label = QLabel("自定义界面主题颜色")
            title_label.setStyleSheet(f"font-size: 14px; font-weight: bold; color: {ThemeManager.hex('text_primary')}; padding: 8px;")
            layout.addWidget(title_label)

            desc_label = QLabel(
                "点击颜色方块可选择自定义颜色，或使用下方预设快速切换。\n"
                "修改后点击\"应用\"生效，设置会自动保存到配置文件。"
            )
            desc_label.setWordWrap(True)
            desc_label.setStyleSheet(f"font-size: 11px; color: {ThemeManager.hex('text_muted')}; padding: 4px 0 12px;")
            layout.addWidget(desc_label)

            scroll = QScrollArea()
            scroll.setWidgetResizable(True)
            scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            form_inner = QWidget()
            form = QFormLayout(form_inner)
            form.setSpacing(10)
            form.setContentsMargins(8, 8, 8, 8)

            current_theme = get_theme_config()

            for field_name in THEME_FIELDS:
                label_cn = LABELS_CN.get(field_name, field_name)
                row_layout = QHBoxLayout()
                row_layout.setSpacing(8)

                color_btn = QPushButton()
                color_btn.setFixedSize(36, 28)
                color_val = current_theme.get(field_name, ThemeManager.hex(field_name))
                color_btn.setStyleSheet(
                    f"background: {color_val}; border: 2px solid {ThemeManager.hex('border')}; border-radius: 4px;"
                )
                color_btn.setToolTip(f"点击选择颜色: {label_cn}")
                color_btn.clicked.connect(lambda checked, f=field_name: self._pick_color(f))
                self._color_buttons[field_name] = color_btn

                hex_label = QLabel(color_val.upper())
                hex_label.setMinimumWidth(70)
                hex_label.setStyleSheet(f"font-family: Consolas, monospace; font-size: 11px; color: {ThemeManager.hex('text_secondary')};")
                setattr(self, f"_theme_hex_{field_name}", hex_label)

                name_label = QLabel(label_cn)
                name_label.setStyleSheet(f"color: {ThemeManager.hex('text_primary')};")

                row_layout.addWidget(color_btn)
                row_layout.addWidget(hex_label)
                row_layout.addStretch()

                form.addRow(name_label, row_layout)

            scroll.setWidget(form_inner)
            layout.addWidget(scroll, 1)

            preset_group = QGroupBox("快速预设")
            preset_group.setStyleSheet(f"QGroupBox {{ font-weight: bold; color: {ThemeManager.hex('text_primary')}; border: 1px solid {ThemeManager.hex('border')}; border-radius: 6px; margin-top: 8px; padding-top: 16px; }} QGroupBox::title {{ subcontrol-origin: margin; left: 12px; padding: 0 6px; }}")
            preset_layout = QHBoxLayout(preset_group)

            dark_btn = QPushButton("🌙 暗色模式")
            dark_btn.setToolTip("一键切换到暗色主题预设")
            dark_btn.clicked.connect(self._on_switch_dark)
            preset_layout.addWidget(dark_btn)

            light_btn = QPushButton("☀️ 亮色模式")
            light_btn.setToolTip("恢复到亮色主题默认值")
            light_btn.clicked.connect(self._on_switch_light)
            preset_layout.addWidget(light_btn)

            reset_btn = QPushButton("↩️ 恢复默认")
            reset_btn.setToolTip("重置所有颜色为程序默认值")
            reset_btn.clicked.connect(self._on_reset_all)
            preset_layout.addWidget(reset_btn)

            preset_layout.addStretch()
            layout.addWidget(preset_group)

            btn_layout = QHBoxLayout()
            btn_layout.addStretch()

            cancel_btn = QPushButton("取消")
            cancel_btn.clicked.connect(self.reject)
            btn_layout.addWidget(cancel_btn)

            apply_btn = QPushButton("应用")
            apply_btn.setDefault(True)
            apply_btn.clicked.connect(self._on_apply)
            btn_layout.addWidget(apply_btn)

            layout.addLayout(btn_layout)

        except Exception as e:
            logger.error("[ThemeConfigDialog] _setup_ui failed: %s", e, exc_info=True)
            error_label = QLabel(f"初始化主题配置对话框失败:\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            layout = QVBoxLayout(self)
            layout.addWidget(error_label)

    def _pick_color(self, field_name: str) -> None:
        from gui.core.theme import ThemeManager, LABELS_CN
        current_hex = ThemeManager.hex(field_name)
        color = QColorDialog.getColor(QColor(current_hex), self, f"选择颜色: {LABELS_CN.get(field_name, field_name)}")
        if color.isValid():
            hex_val = color.name()
            self._update_color_button(field_name, hex_val)

    def _update_color_button(self, field_name: str, hex_val: str) -> None:
        from gui.core.theme import ThemeManager
        btn = self._color_buttons.get(field_name)
        if btn:
            btn.setStyleSheet(
                f"background: {hex_val}; border: 2px solid {ThemeManager.hex('border')}; border-radius: 4px;"
            )
        hex_label = getattr(self, f"_theme_hex_{field_name}", None)
        if hex_label:
            hex_label.setText(hex_val.upper())

    def _on_switch_dark(self) -> None:
        from gui.core.theme import ThemeManager as TM, THEME_FIELDS
        TM.set_dark()
        for fname in THEME_FIELDS:
            self._update_color_button(fname, TM.hex(fname))

    def _on_switch_light(self) -> None:
        from gui.core.theme import ThemeManager as TM, DEFAULT_THEME, THEME_FIELDS
        TM.reset_to_default()
        for fname in THEME_FIELDS:
            self._update_color_button(fname, TM.hex(fname))

    def _on_reset_all(self) -> None:
        from gui.core.theme import ThemeManager as TM, DEFAULT_THEME, THEME_FIELDS
        TM.reset_to_default()
        for fname in THEME_FIELDS:
            self._update_color_button(fname, TM.hex(fname))

    def _on_apply(self) -> None:
        from gui.core.app_config import save_theme
        from gui.core.theme import ThemeManager

        theme_dict = {}
        for fname, btn in self._color_buttons.items():
            hex_label = getattr(self, f"_theme_hex_{fname}", None)
            if hex_label:
                theme_dict[fname] = hex_label.text().lstrip("#")
        theme_obj = ThemeManager.from_dict(theme_dict)
        ThemeManager.apply(theme_obj)
        save_theme(theme_dict)

        self.accept()


class AlgorithmConfigDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("算法配置")
        self.setMinimumSize(640, 520)
        self._spinboxes: dict[AlgorithmType, dict[str, QSpinBox]] = {}
        self._streaming_threshold_spin: QSpinBox | None = None
        self._streaming_chunk_spin: QSpinBox | None = None
        self._setup_ui()

    def _setup_ui(self) -> None:
        try:
            from gui.core.engine import CompressionEngine
            from gui.core.models import (
                STREAMING_THRESHOLD_MB, STREAMING_CHUNK_SIZE_KB,
            )
            from gui.core.theme import ThemeManager

            layout = QVBoxLayout(self)
            current_config = CompressionEngine.get_config()

            tabs = QTabWidget()
            algo_labels = {
                AlgorithmType.DEFLATE: "Deflate",
                AlgorithmType.LZMINE: "LZMine",
                AlgorithmType.LZSS: "LZSS",
                AlgorithmType.MYFLATE: "MyFlate",
                AlgorithmType.GZIP: "Gzip",
            }

            for algo in [AlgorithmType.DEFLATE, AlgorithmType.LZMINE, AlgorithmType.LZSS, AlgorithmType.MYFLATE, AlgorithmType.GZIP]:
                params = ALGORITHM_PARAMS.get(algo, [])
                if not params:
                    continue

                tab = QWidget()
                form = QFormLayout(tab)
                form.setContentsMargins(12, 12, 12, 12)
                self._spinboxes[algo] = {}

                cfg = current_config.get(algo, {})

                for p in params:
                    spin = QSpinBox()
                    spin.setMinimum(p.min_val)
                    spin.setMaximum(p.max_val)
                    spin.setSingleStep(p.step)
                    spin.setValue(cfg.get(p.key, p.default))
                    spin.setSuffix(p.suffix)
                    spin.setToolTip(f"范围: {p.min_val} ~ {p.max_val}")
                    form.addRow(f"{p.label}:", spin)
                    self._spinboxes[algo][p.key] = spin

                tabs.addTab(tab, algo_labels.get(algo, algo.value))

            stream_tab = QWidget()
            stream_form = QFormLayout(stream_tab)
            stream_form.setContentsMargins(12, 12, 12, 12)

            from gui.core.engine import CompressionEngine as _CE
            cur_threshold = _CE.get_streaming_threshold()

            self._streaming_threshold_spin = QSpinBox()
            self._streaming_threshold_spin.setMinimum(1)
            self._streaming_threshold_spin.setMaximum(10240)
            self._streaming_threshold_spin.setSingleStep(1)
            self._streaming_threshold_spin.setValue(int(cur_threshold))
            self._streaming_threshold_spin.setSuffix(" MB")
            self._streaming_threshold_spin.setToolTip(
                "当文件大小超过此阈值时，自动切换到流式分块压缩/解压模式\n"
                "流式模式内存占用恒定，适合处理超大文件"
            )
            stream_form.addRow("使用流式的大小阈值:", self._streaming_threshold_spin)

            self._streaming_chunk_spin = QSpinBox()
            self._streaming_chunk_spin.setMinimum(64)
            self._streaming_chunk_spin.setMaximum(64 * 1024)
            self._streaming_chunk_spin.setSingleStep(64)
            self._streaming_chunk_spin.setValue(STREAMING_CHUNK_SIZE_KB)
            self._streaming_chunk_spin.setSuffix(" KB")
            self._streaming_chunk_spin.setToolTip(
                "流式模式下每个数据块的大小\n"
                "较大的块提高压缩率，较小的块降低内存占用"
            )
            stream_form.addRow("流式分块大小:", self._streaming_chunk_spin)

            desc_label = QLabel(
                "流式模式说明：文件超过阈值时自动启用，\n"
                "采用 terminator 格式 ([4B长度][数据]...[4B 0]) 分块处理，\n"
                "内存占用与文件大小无关，仅取决于分块大小。"
            )
            desc_label.setWordWrap(True)
            desc_label.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; font-size: 11px; padding: 4px 0;")
            stream_form.addRow(desc_label)

            tabs.addTab(stream_tab, "流式设置")

            layout.addWidget(tabs)

            btn_layout = QHBoxLayout()
            reset_btn = QPushButton("恢复默认")
            reset_btn.clicked.connect(self._on_reset)
            btn_layout.addWidget(reset_btn)
            btn_layout.addStretch()

            cancel_btn = QPushButton("取消")
            cancel_btn.clicked.connect(self.reject)
            btn_layout.addWidget(cancel_btn)

            apply_btn = QPushButton("应用")
            apply_btn.setDefault(True)
            apply_btn.clicked.connect(self._on_apply)
            btn_layout.addWidget(apply_btn)

            layout.addLayout(btn_layout)

        except Exception as e:
            logger.error("[AlgorithmConfigDialog] _setup_ui failed: %s", e, exc_info=True)
            error_label = QLabel(f"初始化算法配置对话框失败:\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            layout = QVBoxLayout(self)
            layout.addWidget(error_label)

    def _on_reset(self) -> None:
        from gui.core.models import STREAMING_THRESHOLD_MB, STREAMING_CHUNK_SIZE_KB
        from gui.core.engine import CompressionEngine

        CompressionEngine.reset_to_defaults()
        defaults = CompressionEngine.get_config()
        for algo, spins in self._spinboxes.items():
            cfg = defaults.get(algo, {})
            for key, spin in spins.items():
                spin.setValue(cfg.get(key, spin.minimum()))
        if self._streaming_threshold_spin:
            self._streaming_threshold_spin.setValue(int(CompressionEngine.get_streaming_threshold()))
        if self._streaming_chunk_spin:
            self._streaming_chunk_spin.setValue(STREAMING_CHUNK_SIZE_KB)

    def _on_apply(self) -> None:
        from gui.core.engine import CompressionEngine

        config: dict[AlgorithmType, dict[str, int]] = {}
        for algo, spins in self._spinboxes.items():
            config[algo] = {key: spin.value() for key, spin in spins.items()}

        CompressionEngine.set_config(config)

        if self._streaming_threshold_spin:
            CompressionEngine.set_streaming_threshold(
                float(self._streaming_threshold_spin.value())
            )

        self.accept()


class CompressDemoDialog(QDialog):
    def __init__(self, record: FileRecord, algorithm: AlgorithmType, parent=None):
        super().__init__(parent)
        self._record = record
        self._algorithm = algorithm
        self.setWindowTitle(f"压缩演示 - {record.name}")
        self.setMinimumWidth(500)
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        info_layout = QVBoxLayout()
        info_layout.addWidget(QLabel(f"文件: {self._record.path}"))
        info_layout.addWidget(QLabel(f"大小: {formatted_size(self._record.size)}"))
        info_layout.addWidget(QLabel(f"类型: {self._record.type.value}"))
        info_layout.addWidget(QLabel(f"算法: {self._algorithm.value}"))

        status_text = self._record.status.value
        info_layout.addWidget(QLabel(f"状态: {status_text}"))

        if self._record.compressed_data:
            stored_tag = " [stored]" if getattr(self._record, 'is_stored', False) else ""
            info_layout.addWidget(QLabel(f"压缩后: {formatted_size(_compressed_size(self._record))}{stored_tag}"))
            info_layout.addWidget(QLabel(f"压缩率: {self._record.compression_ratio * 100:.1f}%"))
            info_layout.addWidget(QLabel(f"耗时: {self._record.compression_time_ms:.1f}ms"))

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

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("WebCompress")
        self.resize(WINDOW_WIDTH, WINDOW_HEIGHT)

        self.setAcceptDrops(True)
        self._worker: CompressionWorker | None = None

        self._table = FileTableWidget()
        self._statusbar = StatusBarWidget()
        self._algo_selector = AlgorithmSelector()

        self._setup_menu()
        self._setup_toolbar()
        self._setup_central()

        bar = QStatusBar()
        self.setStatusBar(bar)
        bar.addPermanentWidget(self._statusbar)

        self._table.request_demo.connect(self._on_compress_demo)
        self._table.request_heatmap.connect(self._on_view_heatmap_row)
        self._table.request_comparison.connect(self._on_view_comparison_row)
        self._table.request_network.connect(self._on_view_network_row)
        self._table.request_webpage_heatmap.connect(self._on_webpage_heatmap)
        self._table.request_folder_summary.connect(self._on_folder_summary)

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

        adv_menu = bar.addMenu("高级 (&A)")

        algo_config_action = QAction("算法配置 (&C)", self)
        algo_config_action.setShortcut("Ctrl+Shift+C")
        algo_config_action.triggered.connect(self._on_algo_config)
        adv_menu.addAction(algo_config_action)

        theme_config_action = QAction("主题配置 (&T)", self)
        theme_config_action.setShortcut("Ctrl+Shift+T")
        theme_config_action.setToolTip("自定义界面主题颜色和外观设置")
        theme_config_action.triggered.connect(self._on_theme_config)
        adv_menu.addAction(theme_config_action)

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

        self._compress_action = QAction("▶ 开始压缩", self)
        self._compress_action.setToolTip("开始压缩选中的文件")
        self._compress_action.triggered.connect(self._on_compress)
        toolbar.addAction(self._compress_action)

        self._cancel_compress_action = QAction("⏹ 取消", self)
        self._cancel_compress_action.setToolTip("取消正在进行的压缩")
        self._cancel_compress_action.triggered.connect(self._on_cancel_compress)
        self._cancel_compress_action.setEnabled(False)
        toolbar.addAction(self._cancel_compress_action)

        toolbar.addSeparator()

        self._decompress_action = QAction("🔓 解压", self)
        self._decompress_action.setToolTip("解压选中的压缩文件")
        self._decompress_action.triggered.connect(self._on_decompress)
        toolbar.addAction(self._decompress_action)

        toolbar.addSeparator()

        export_action = QAction("💾 导出结果", self)
        export_action.setToolTip("导出压缩后的文件")
        export_action.triggered.connect(self._on_export)
        toolbar.addAction(export_action)

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
            logger.info("[add] adding %d files", len(paths))
            self._add_paths(paths)

    def _on_add_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if folder:
            logger.info("[add] adding folder: %s", folder)
            self._add_paths([folder])

    def _add_paths(self, paths: list[str]) -> None:
        files, dirs = self._table.add_paths(paths)
        logger.info("[add] added %d files, %d dirs from %d paths", files, dirs, len(paths))
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
        else:
            self._select_all_action.setText("☐ 全选")

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
        logger.info("[compress] _on_compress called")
        if self._worker and self._worker.isRunning():
            logger.warning("[compress] worker already running")
            return

        selected = self._table.selected_rows
        logger.info("[compress] selected_rows=%s", selected)
        if not selected:
            QMessageBox.warning(self, "提示", "请先选中文件！")
            return

        records = self._table.get_records(selected)
        algo = self._algo_selector.current_algorithm
        logger.info("[compress] algorithm=%s, records=%d", algo.value, len(records))
        for i, r in enumerate(records):
            logger.info("[compress]   task[%d]: type=%s, name=%s, status=%s",
                         i, type(r).__name__, getattr(r, 'name', '?'), getattr(r, 'status', '?'))
        tasks = list(zip(selected, records))

        for row, record in tasks:
            if isinstance(record, FileRecord):
                record.status = CompressionStatus.COMPRESSING
            elif isinstance(record, FolderRecord):
                record.status = CompressionStatus.COMPRESSING
                for f in record.files:
                    f.status = CompressionStatus.COMPRESSING
            self._table.update_row(row)

        self._compress_action.setEnabled(False)
        self._cancel_compress_action.setEnabled(True)

        self._worker = CompressionWorker(tasks, self._algo_selector.current_algorithm)
        self._worker.row_started.connect(self._on_row_started)
        self._worker.finished_row.connect(self._on_row_finished)
        self._worker.error.connect(self._table.mark_error)
        self._worker.finished.connect(self._on_compression_finished)

        file_count = sum(len(r.files) if isinstance(r, FolderRecord) else 1 for r in records)
        self._statusbar.set_status_text(f"正在压缩 {file_count} 个文件...")
        self._statusbar.set_progress_value(0)
        self._worker.start()

    def _on_cancel_compress(self) -> None:
        if self._worker and self._worker.isRunning():
            logger.info("[compress] cancelling worker")
            self._worker.cancel()
            self._statusbar.set_status_text("正在取消压缩...")

    def _on_row_started(self, row: int) -> None:
        try:
            record = self._table.get_record(row)
            if record and isinstance(record, FileRecord):
                record.status = CompressionStatus.COMPRESSING
                self._table.update_row(row)
        except Exception as e:
            logger.error("[_on_row_started] CRASH row=%d: %s", row, e, exc_info=True)

    def _on_row_finished(self, row: int) -> None:
        try:
            record = self._table.get_record(row)
            logger.info("[compress] row %d finished: name=%s, status=%s, ratio=%.4f",
                         row, getattr(record, 'name', '?'), getattr(record, 'status', '?'),
                         getattr(record, 'compression_ratio', 0))
            self._table.update_row(row)
            done = sum(
                1 for r in range(self._table.rowCount())
                if (item := self._table.item(r, self._table.COL_STATUS))
                and (item.text().startswith(CompressionStatus.DONE.value) or item.text() == CompressionStatus.FAILED.value)
            )
            total = self._table.rowCount()
            progress = int(done / total * 100) if total else 0
            self._statusbar.set_progress_value(progress)
        except Exception as e:
            logger.error("[_on_row_finished] CRASH row=%d: %s", row, e, exc_info=True)

    def _on_compression_finished(self) -> None:
        try:
            self._compress_action.setEnabled(True)
            self._cancel_compress_action.setEnabled(False)
            self._statusbar.set_progress_value(100)
            if self._worker and self._worker._is_cancelled:
                self._statusbar.set_status_text("压缩已取消")
            else:
                self._statusbar.set_status_text("压缩完成")
        except Exception as e:
            logger.error("[_on_compression_finished] CRASH: %s", e, exc_info=True)
            self._compress_action.setEnabled(True)
            self._cancel_compress_action.setEnabled(False)

    # ========== 压缩演示 ==========
    def _on_compress_demo(self, row: int) -> None:
        logger.info("[demo] START row=%d", row)
        record = self._table.get_record(row)
        if not isinstance(record, FileRecord):
            QMessageBox.information(self, "提示", "压缩演示仅支持单文件")
            return
        from gui.core.token_parser import can_parse, get_parser
        from gui.core.models import TEXT_EXTENSIONS, SCRIPT_EXTENSIONS

        ext = Path(record.name).suffix.lower() if "." in record.name else ""
        is_text = ext in (TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"}))
        logger.info("[demo] name=%s algo=%s is_text=%s ext=%s", record.name, record.algorithm.value, is_text, ext)

        if can_parse(record.algorithm) and not getattr(record, 'is_stored', False):
            logger.info("[demo] can_parse=True, getting parser")
            parser = get_parser(record.algorithm)
            if parser is not None:
                logger.info("[demo] parsing compressed_data=%d raw_data=%d", len(record.compressed_data), len(record.raw_data) if record.raw_data else 0)
                pr = parser.parse(record.compressed_data, record.raw_data)
                logger.info("[demo] parse done, tokens=%d", len(pr.tokens))
                if is_text and len(pr.tokens) > 0:
                    text = record.raw_data.decode('utf-8', errors='replace')
                    byte_to_char = []
                    char_idx = 0
                    for ch in text:
                        byte_len = len(ch.encode('utf-8'))
                        for _ in range(byte_len):
                            byte_to_char.append(char_idx)
                        char_idx += 1
                    logger.info("[demo] text len=%d byte_to_char len=%d", len(text), len(byte_to_char))

                    _LZ_ALGOS = {AlgorithmType.LZSS, AlgorithmType.LZMINE}
                    if record.algorithm in _LZ_ALGOS:
                        if record.algorithm == AlgorithmType.LZMINE:
                            logger.info("[demo] LZMine path, getting dp_viz")
                            dp_viz = None
                            if len(record.raw_data) <= LZMINE_DP_VIZ_MAX_SIZE:
                                try:
                                    from gui.core.engine import CompressionEngine
                                    engine = CompressionEngine()
                                    comp = engine._create_compressor(AlgorithmType.LZMINE)
                                    dp_viz = comp.get_dp_visualization(list(record.raw_data))
                                    logger.info("[demo] dp_viz OK, steps=%d path=%d", len(dp_viz.steps), len(dp_viz.optimal_path))
                                except Exception as e:
                                    logger.warning("[demo] get_dp_visualization failed: %s", e, exc_info=True)
                                    dp_viz = None
                            else:
                                logger.info("[demo] raw_data too large (%d) for dp_viz, skipping", len(record.raw_data))
                            logger.info("[demo] creating LZMineDPDialog")
                            from gui.widgets.heatmap_widgets import LZMineDPDialog
                            dlg = LZMineDPDialog(text, pr.tokens, byte_to_char,
                                                 dp_viz, record.name, record.algorithm.value, parent=self)
                            logger.info("[demo] LZMineDPDialog created, calling exec")
                            dlg.exec()
                            logger.info("[demo] LZMineDPDialog closed")
                        else:
                            logger.info("[demo] LZSS path, creating LZSliderDialog")
                            from gui.widgets.heatmap_widgets import LZSliderDialog
                            dlg = LZSliderDialog(text, pr.tokens, byte_to_char,
                                                 record.name, record.algorithm.value, parent=self)
                            logger.info("[demo] LZSliderDialog created, calling exec")
                            dlg.exec()
                            logger.info("[demo] LZSliderDialog closed")
                        return

                    _FLATE_ALGOS = {AlgorithmType.MYFLATE, AlgorithmType.DEFLATE}
                    if record.algorithm in _FLATE_ALGOS:
                        logger.info("[demo] Flate path, creating FlateDemoDialog")
                        from gui.widgets.heatmap_widgets import FlateDemoDialog
                        huffman_trees = pr.huffman_trees if pr.huffman_trees else None
                        dlg = FlateDemoDialog(text, pr.tokens, byte_to_char,
                                              record.name, record.algorithm.value,
                                              huffman_trees=huffman_trees, parent=self)
                        logger.info("[demo] FlateDemoDialog created, calling exec")
                        dlg.exec()
                        logger.info("[demo] FlateDemoDialog closed")
                        return

                    logger.info("[demo] non-LZ/non-Flate path (e.g. pure Huffman), showing info")
                    QMessageBox.information(
                        self, "提示",
                        f"算法「{record.algorithm.value}」暂不支持交互式演示。\n\n"
                        f"请使用「📊 压缩热力图」查看该算法的压缩可视化。"
                    )
                    return

        dlg = CompressDemoDialog(record, self._algo_selector.current_algorithm, parent=self)
        dlg.exec()

    def _decompress_payload(self, engine, header, payload) -> bytes:
        if header.algorithm == AlgorithmType.NONE:
            return payload
        result = engine.smart_decompress(payload, header.algorithm)
        return bytes(result.data)

    # ========== 导出操作 ==========
    def _on_export(self) -> None:
        from gui.core.file_protocol import pack_compressed_file, make_export_filename, pack_folder_archive
        from pathlib import Path as P

        selected = self._table.selected_rows
        if not selected:
            cur = self._table.currentRow()
            if cur >= 0:
                selected = [cur]
            else:
                self._statusbar.set_status_text("没有选中的文件")
                return

        done_count = 0
        for row in selected:
            record = self._table.get_record(row)
            if not record:
                continue
            if isinstance(record, FileRecord) and record.status == CompressionStatus.DONE and record.compressed_data:
                done_count += 1
            elif isinstance(record, FolderRecord) and record.status == CompressionStatus.DONE and record.success_count > 0:
                done_count += 1

        if done_count == 0:
            self._statusbar.set_status_text("选中的文件中没有已完成压缩的文件")
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not export_dir:
            return

        logger.info("[export] rows=%s", selected)
        exported = 0
        for row in selected:
            record = self._table.get_record(row)
            logger.info("[export] row=%d record=%s status=%s type=%s",
                         row, getattr(record, 'name', '?'),
                         getattr(record, 'status', '?'), type(record).__name__)
            if not record or record.status != CompressionStatus.DONE:
                logger.warning("[export] SKIP row=%d: status=%s (need DONE)", row, getattr(record, 'status', None))
                continue
            if isinstance(record, FolderRecord):
                logger.info("[export] FolderRecord: %d files, %d done",
                             len(record.files), record.success_count)
                file_list = []
                folder_root = P(record.path)
                for f in record.files:
                    if f.status != CompressionStatus.DONE or not f.compressed_data:
                        logger.warning("[export] SKIP file %s: status=%s has_data=%s",
                                          f.name, f.status, bool(f.compressed_data))
                        continue
                    try:
                        rel = str(P(f.path).relative_to(folder_root))
                    except ValueError:
                        rel = f.name
                    file_list.append((rel, f.compressed_data, f.algorithm, f.size))
                if not file_list:
                    continue
                archive_data = pack_folder_archive(record.name, file_list)
                export_name = make_export_filename(record.name)
                export_path = os.path.join(export_dir, export_name)
                with open(export_path, 'wb') as file:
                    file.write(archive_data)
                exported += 1
            elif isinstance(record, FileRecord) and record.compressed_data:
                export_name = make_export_filename(record.name, record.algorithm)
                export_path = os.path.join(export_dir, export_name)

                packed = pack_compressed_file(
                    compressed_data=record.compressed_data,
                    algorithm=record.algorithm,
                    original_size=record.size,
                    original_filename=record.name,
                )
                with open(export_path, 'wb') as f:
                    f.write(packed)
                exported += 1

        logger.info("[export] done: exported=%d", exported)
        if exported > 0:
            self._statusbar.set_status_text(f"已导出 {exported} 个文件到 {export_dir}")
        else:
            self._statusbar.set_status_text("没有可导出的文件")

    # ========== 解压操作 ==========
    def _on_decompress(self) -> None:
        from gui.core.file_protocol import (
            unpack_compressed_file, detect_algorithm_from_file,
            unpack_folder_archive, pack_folder_archive,
            CompressedFileHeader,
            UNIFIED_EXTENSION,
        )
        from pathlib import Path as P

        selected = self._table.selected_rows
        if not selected:
            cur = self._table.currentRow()
            if cur >= 0:
                selected = [cur]
            else:
                QMessageBox.warning(self, "提示", "请先选中要解压的文件！")
                return

        decompress_tasks: list[tuple[str, int, Record]] = []
        for row in selected:
            record = self._table.get_record(row)
            if not record:
                continue
            if isinstance(record, FileRecord):
                if record.status == CompressionStatus.DONE and record.compressed_data:
                    decompress_tasks.append(('session', row, record))
                elif P(record.path).suffix.lower() == UNIFIED_EXTENSION:
                    try:
                        from gui.core.file_protocol import unpack_compressed_file
                        record.load_raw_data()
                        header, _ = unpack_compressed_file(record.raw_data)
                        decompress_tasks.append(('disk', row, record))
                    except Exception:
                        pass
            elif isinstance(record, FolderRecord) and record.status == CompressionStatus.DONE and record.success_count > 0:
                decompress_tasks.append(('folder_session', row, record))

        if not decompress_tasks:
            QMessageBox.warning(self, "提示", "选中的文件中没有可解压的压缩文件\n（需为已压缩文件或 .wcx 格式）")
            return

        from gui.core.engine import CompressionEngine
        engine = CompressionEngine()
        if not engine.available:
            QMessageBox.critical(self, "错误", "C++ 核心引擎不可用")
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择解压输出目录")
        if not export_dir:
            return

        success = 0
        for task_type, row, record in decompress_tasks:
            try:
                if task_type == 'folder_session':
                    folder_root = P(record.path)
                    file_list = []
                    for f in record.files:
                        if f.status != CompressionStatus.DONE or not f.compressed_data:
                            continue
                        try:
                            rel = str(P(f.path).relative_to(folder_root))
                        except ValueError:
                            rel = f.name
                        file_list.append((rel, f.compressed_data, f.algorithm, f.size))
                    archive_data = pack_folder_archive(record.name, file_list)
                    inner_files = unpack_folder_archive(archive_data)
                    for inner_hdr, inner_payload in inner_files:
                        output_data = self._decompress_payload(engine, inner_hdr, inner_payload)
                        rel_path = inner_hdr.original_filename or f"unnamed_{success}"
                        out_path = os.path.join(export_dir, rel_path)
                        out_parent = os.path.dirname(out_path)
                        if out_parent:
                            os.makedirs(out_parent, exist_ok=True)
                        with open(out_path, 'wb') as f:
                            f.write(output_data)
                        success += 1
                    continue

                if task_type == 'session':
                    header = CompressedFileHeader(
                        algorithm=record.algorithm,
                        original_size=record.size,
                        compressed_size=_compressed_size(record),
                        original_filename=record.name,
                        is_folder=False,
                    )

                    if hasattr(record, 'compressed_path') and record.compressed_path:
                        original_name = header.original_filename or record.name
                        output_path = os.path.join(export_dir, original_name)
                        logger.info("[decompress] STREAMING file-to-file: %s -> %s", record.compressed_path, output_path)
                        result = engine.smart_decompress_file(record.compressed_path, output_path, record.algorithm)
                        success += 1
                        continue

                    payload = record.compressed_data or b''
                else:
                    record.load_raw_data()
                    header, payload = unpack_compressed_file(record.raw_data)

                if header.is_folder:
                    folder_out = os.path.join(export_dir, header.original_filename or f"folder_{success}")
                    inner_files = unpack_folder_archive(record.raw_data)
                    for inner_hdr, inner_payload in inner_files:
                        output_data = self._decompress_payload(engine, inner_hdr, inner_payload)
                        rel_path = inner_hdr.original_filename or f"unnamed_{success}"
                        out_path = os.path.join(folder_out, rel_path)
                        out_parent = os.path.dirname(out_path)
                        if out_parent:
                            os.makedirs(out_parent, exist_ok=True)
                        with open(out_path, 'wb') as f:
                            f.write(output_data)
                        success += 1
                    continue

                output_data = self._decompress_payload(engine, header, payload)
                original_name = header.original_filename or record.name
                output_path = os.path.join(export_dir, original_name)
                with open(output_path, 'wb') as f:
                    f.write(output_data)
                success += 1
                logger.info("解压成功: %s -> %s", record.name, output_path)

            except Exception as e:
                logger.error("解压失败 %s: %s", record.name, e, exc_info=True)
                QMessageBox.warning(self, "解压失败", f"文件 {record.name} 解压失败:\n{e}")

        if success > 0:
            self._statusbar.set_status_text(f"已解压 {success} 个文件到 {export_dir}")

    # ========== 关于 ==========
    def _on_about(self) -> None:
        QMessageBox.about(
            self,
            "关于 WebCompress Pro",
            "WebCompress Pro v1.0\n"
            "基于 Deflate/LZSS/LZMine/MyFlate 算法的压缩工具\n\n"
            "━━━ WCMP v2 文件协议 ━━━\n\n"
            "统一后缀 .wcx (单文件/文件夹通用)\n"
            "解压时自动从文件头判断类型和算法\n\n"
            "文件头固定部分 (18 字节, 小端序):\n"
            "  偏移  长度  类型    内容\n"
            "  0     4 B   char[4] 魔数 \"WCMP\"\n"
            "  4     1 B   uint8   协议版本 (2)\n"
            "  5     1 B   uint8   算法编码\n"
            "  6     4 B   uint32  原始大小\n"
            "  10    4 B   uint32  压缩大小\n"
            "  14    1 B   uint8   标志位 (bit0=文件夹)\n"
            "  15    2 B   uint16  文件名长度 N\n"
            "  17    1 B   uint8   对齐填充\n"
            "  18    N B   utf-8   文件名(文件夹)或相对路径(子文件)\n\n"
            "算法编码:\n"
            "  1 = Deflate  LZ77 + Huffman\n"
            "  2 = LZSS     LZ77 变体\n"
            "  3 = LZMine   KMP + DP 全局最优\n"
            "  4 = Huffman  纯 Huffman\n"
            "  5 = MyFlate  LZMine+Huffman (DP+熵编码)\n"
            "  6 = Gzip     zlib 标准\n\n"
            "文件夹归档: 外层header(is_folder=1) + [file_count] + 内部文件列表\n"
            "每个内部文件保存相对路径，解压后自动还原目录结构\n\n"
            "© 2026 数据结构课程设计"
        )

    def _on_algo_config(self) -> None:
        dlg = AlgorithmConfigDialog(self)
        dlg.exec()

    def _on_theme_config(self) -> None:
        logger.info("[main_window] opening ThemeConfigDialog")
        dlg = ThemeConfigDialog(self)
        dlg.exec()
        logger.info("[main_window] ThemeConfigDialog closed")

    # ========== 视图：网页可视化 ==========

    def _get_selected_done_record(self) -> FileRecord | None:
        rows = self._table.selected_rows
        logger.info("[view] _get_selected_done_record called, selected_rows=%s", rows)
        if not rows:
            logger.warning("[view] no rows selected")
            QMessageBox.information(self, "提示", "请先勾选一个已压缩的文件（点击行左侧的☐）")
            return None
        record = self._table.get_record(rows[0])
        logger.info("[view] got record: type=%s, name=%s, status=%s",
                     type(record).__name__, getattr(record, 'name', '?'),
                     getattr(record, 'status', '?'))
        if not isinstance(record, FileRecord):
            logger.warning("[view] record is not FileRecord: %s", type(record).__name__)
            QMessageBox.information(self, "提示", "请选中一个文件（不支持文件夹）")
            return None
        if record.status != CompressionStatus.DONE:
            logger.warning("[view] record not done: status=%s", record.status.value)
            QMessageBox.information(self, "提示", f"该文件状态为 {record.status.value}，请先压缩")
            return None
        if not record.compressed_data:
            logger.warning("[view] record has no compressed_data")
            QMessageBox.information(self, "提示", "该文件没有压缩数据")
            return None
        logger.info("[view] valid record: name=%s, size=%d, compressed=%d, algo=%s",
                     record.name, record.size, _compressed_size(record), record.algorithm.value)
        return record

    def _on_view_heatmap_row(self, row: int) -> None:
        logger.info("[view] heatmap from right-click, row=%d", row)
        record = self._table.get_record(row)
        logger.info("[view] record: type=%s, name=%s, status=%s, has_data=%s",
                     type(record).__name__, getattr(record, 'name', '?'),
                     getattr(record, 'status', '?'),
                     bool(getattr(record, 'compressed_data', None)))
        if not isinstance(record, FileRecord) or record.status != CompressionStatus.DONE or not record.compressed_data:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法查看热力图")
            return
        self._open_heatmap(record)

    def _on_view_heatmap(self) -> None:
        logger.info("[view] heatmap from menu")
        record = self._get_selected_done_record()
        if record:
            self._open_heatmap(record)

    def _on_view_comparison_row(self, row: int) -> None:
        logger.info("[view] comparison from right-click, row=%d", row)
        record = self._table.get_record(row)
        if isinstance(record, FolderRecord):
            self._run_folder_comparison(record)
        elif isinstance(record, FileRecord) and record.status == CompressionStatus.DONE:
            self._run_comparison(record)
        else:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法对比")

    def _on_view_network_row(self, row: int) -> None:
        logger.info("[view] network sim from right-click, row=%d", row)
        record = self._table.get_record(row)
        if not isinstance(record, FileRecord) or record.status != CompressionStatus.DONE:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法模拟")
            return
        self._run_network_sim(record)

    def _open_heatmap(self, record: FileRecord) -> None:
        logger.info("[view] opening heatmap for %s (%d bytes, algo=%s)",
                     record.name, record.size, record.algorithm.value)
        try:
            from gui.core.token_parser import can_parse, get_parser
            from gui.core.models import TEXT_EXTENSIONS, SCRIPT_EXTENSIONS

            ext = Path(record.name).suffix.lower() if "." in record.name else ""
            is_text = ext in (TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"}))
            logger.info("[heatmap] ext=%s is_text=%s can_parse=%s", ext, is_text, can_parse(record.algorithm))

            if can_parse(record.algorithm) and not getattr(record, 'is_stored', False):
                parser = get_parser(record.algorithm)
                logger.info("[heatmap] parser=%s, calling parse", type(parser).__name__)
                if parser is not None:
                    pr = parser.parse(record.compressed_data, record.raw_data)
                    logger.info("[heatmap] parse done, tokens=%d", len(pr.tokens))
                    if is_text:
                        text = record.raw_data.decode('utf-8', errors='replace')
                        byte_to_char = []
                        char_idx = 0
                        for ch in text:
                            byte_len = len(ch.encode('utf-8'))
                            for _ in range(byte_len):
                                byte_to_char.append(char_idx)
                            char_idx += 1

                        from gui.widgets.heatmap_widgets import HeatmapDialog
                        stats = {
                            'original_size': record.size,
                            'compressed_size': _compressed_size(record),
                            'ratio': record.compression_ratio,
                            'token_count': len(pr.tokens),
                            'match_count': sum(1 for t in pr.tokens if t.type.value == 'match'),
                            'time_ms': record.compression_time_ms,
                        }
                        huffman_trees = pr.huffman_trees if pr.huffman_trees else None
                        dlg = HeatmapDialog(text, pr.tokens, byte_to_char,
                                            record.name, record.algorithm.value, stats,
                                            huffman_trees=huffman_trees, parent=self)
                        dlg.exec()
                        logger.info("[view] Qt native heatmap opened")
                        return

                    from gui.web.token_heatmap import generate_token_heatmap, open_token_heatmap_html
                    html = generate_token_heatmap(
                        raw_data=record.raw_data,
                        compressed_data=record.compressed_data,
                        algorithm=record.algorithm,
                        filename=record.name,
                        original_size=record.size,
                        time_ms=record.compression_time_ms,
                    )
                    if html is not None:
                        open_token_heatmap_html(html)
                        logger.info("[view] token heatmap opened in browser")
                        return

            from gui.web.heatmap import generate_heatmap, open_heatmap_html
            logger.info("[view] falling back to block-based heatmap")
            html = generate_heatmap(
                raw_data=record.raw_data,
                compressed_data=record.compressed_data,
                block_size=256,
                filename=record.name,
                algorithm=record.algorithm.value,
                original_size=record.size,
                compressed_size=_compressed_size(record),
                time_ms=record.compression_time_ms,
            )
            logger.info("[view] heatmap HTML generated: %d chars", len(html))
            open_heatmap_html(html)
            logger.info("[view] heatmap opened in browser")
        except Exception as e:
            logger.error("[view] heatmap failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "热力图错误", f"生成热力图失败:\n{e}")

    def _on_view_comparison(self) -> None:
        logger.info("[view] comparison from menu")
        record = self._get_selected_done_record()
        if record:
            self._run_comparison(record)

    def _run_comparison(self, record: FileRecord) -> None:
        logger.info("[view] running comparison for %s (%d bytes)", record.name, record.size)
        try:
            from gui.core.engine import CompressionEngine
            engine = CompressionEngine()

            results = []
            for algo in [AlgorithmType.LZSS, AlgorithmType.LZMINE,
                         AlgorithmType.MYFLATE,
                         AlgorithmType.DEFLATE, AlgorithmType.GZIP]:
                try:
                    r = engine.smart_compress(record.raw_data, algo)
                    results.append({
                        "name": algo.value,
                        "compressed_size": r.compressed_size,
                        "ratio": r.compression_ratio,
                        "time_ms": r.time_ms,
                    })
                    logger.info("[view] %s: %d -> %d (%.1f%%) %.1fms",
                                 algo.value, record.size, r.compressed_size,
                                 r.compression_ratio * 100, r.time_ms)
                except Exception as e:
                    logger.warning("[view] %s compress failed: %s", algo.value, e)

            if not results:
                QMessageBox.warning(self, "错误", "所有算法压缩均失败")
                return

            from gui.widgets.heatmap_widgets import ComparisonDialog
            dlg = ComparisonDialog(results, record.name, record.size, parent=self)
            logger.info("[view] comparison dialog created, calling exec")
            dlg.exec()
            logger.info("[view] comparison dialog closed")
        except Exception as e:
            logger.error("[view] comparison failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "对比错误", f"生成算法对比失败:\n{e}")

    def _run_folder_comparison(self, record: FolderRecord) -> None:
        logger.info("[view] running folder comparison for %s (%d files)", record.name, record.filenum)
        try:
            from gui.core.engine import CompressionEngine
            from PyQt6.QtWidgets import QProgressDialog

            files = [f for f in record.files if f.size > 0]
            if not files:
                QMessageBox.information(self, "提示", "文件夹中没有可压缩的文件")
                return

            algos = [AlgorithmType.LZSS, AlgorithmType.LZMINE,
                     AlgorithmType.MYFLATE,
                     AlgorithmType.DEFLATE, AlgorithmType.GZIP]

            progress = QProgressDialog("正在对比文件夹...", "取消", 0, len(algos) * len(files), self)
            progress.setWindowTitle("文件夹算法对比")
            progress.setWindowModality(Qt.WindowModality.WindowModal)
            progress.setMinimumDuration(0)
            progress.setValue(0)

            algo_totals: dict[str, dict] = {a.value: {"compressed": 0, "time_ms": 0.0, "count": 0} for a in algos}
            step = 0

            engine = CompressionEngine()
            for algo in algos:
                for f in files:
                    if progress.wasCanceled():
                        return
                    progress.setValue(step)
                    progress.setLabelText(f"{algo.value}: {f.name}")
                    try:
                        if not f.raw_data:
                            f.load_raw_data()
                        r = engine.smart_compress(f.raw_data, algo)
                        algo_totals[algo.value]["compressed"] += r.compressed_size
                        algo_totals[algo.value]["time_ms"] += r.time_ms
                        algo_totals[algo.value]["count"] += 1
                    except Exception as e:
                        logger.warning("[view] folder comparison %s/%s failed: %s", algo.value, f.name, e)
                    step += 1

            progress.setValue(step)

            total_original = sum(f.size for f in files)
            results = []
            for algo in algos:
                t = algo_totals[algo.value]
                if t["count"] > 0:
                    ratio = t["compressed"] / total_original if total_original > 0 else 1.0
                    results.append({
                        "name": algo.value,
                        "compressed_size": t["compressed"],
                        "ratio": ratio,
                        "time_ms": t["time_ms"],
                    })

            if not results:
                QMessageBox.warning(self, "错误", "所有算法压缩均失败")
                return

            from gui.widgets.heatmap_widgets import ComparisonDialog
            dlg = ComparisonDialog(results, record.name, total_original, parent=self)
            dlg.exec()
        except Exception as e:
            logger.error("[view] folder comparison failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "对比错误", f"文件夹算法对比失败:\n{e}")

    def _on_view_network(self) -> None:
        logger.info("[view] network sim from menu")
        record = self._get_selected_done_record()
        if record:
            self._run_network_sim(record)

    def _run_network_sim(self, record: FileRecord) -> None:
        logger.info("[view] running network sim for %s (orig=%d, comp=%d, time=%.1fms)",
                     record.name, record.size, _compressed_size(record), record.compression_time_ms)
        try:
            from gui.web.network_sim import generate_network_sim, open_network_sim_html
            html = generate_network_sim(
                original_size=record.size,
                compressed_size=_compressed_size(record),
                filename=record.name,
                algorithm=record.algorithm.value,
            )
            logger.info("[view] network sim HTML generated: %d chars", len(html))
            open_network_sim_html(html)
            logger.info("[view] network sim opened in browser")
        except Exception as e:
            logger.error("[view] network sim failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "网络模拟错误", f"生成网络传输模拟失败:\n{e}")

    def _on_webpage_heatmap(self, row: int) -> None:
        logger.info("[view] webpage heatmap from right-click, row=%d", row)
        record = self._table.get_record(row)
        if not isinstance(record, FolderRecord):
            QMessageBox.information(self, "提示", "网页资源热力图仅支持文件夹")
            return
        try:
            html_file = None
            for f in record.files:
                ext = Path(f.name).suffix.lower()
                if ext in (".html", ".htm") and f.raw_data:
                    html_file = f
                    break
            if html_file is None:
                QMessageBox.information(self, "提示", "文件夹中未找到 HTML 文件")
                return

            html_content = html_file.raw_data.decode('utf-8', errors='replace')

            resource_ratios = {}
            for f in record.files:
                if f.status == CompressionStatus.DONE and f.compressed_data:
                    resource_ratios[f.name] = f.compression_ratio

            from gui.web.webpage_heatmap import generate_webpage_heatmap, open_webpage_heatmap
            result = generate_webpage_heatmap(
                html_content=html_content,
                resource_ratios=resource_ratios,
                folder_name=record.name,
                total_original=record.total_original,
                total_compressed=record.total_compressed,
            )
            open_webpage_heatmap(result)
            logger.info("[view] webpage heatmap opened")
        except Exception as e:
            logger.error("[view] webpage heatmap failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "热力图错误", f"生成网页资源热力图失败:\n{e}")

    def _on_folder_summary(self, row: int) -> None:
        logger.info("[view] folder summary from right-click, row=%d", row)
        record = self._table.get_record(row)
        if not isinstance(record, FolderRecord):
            QMessageBox.information(self, "提示", "仅支持文件夹")
            return
        try:
            dialog = QDialog(self)
            dialog.setWindowTitle(f"文件夹压缩报告 - {record.name}")
            dialog.setMinimumSize(900, 600)

            layout = QVBoxLayout(dialog)
            report_widget = FolderReportWidget(record, dialog)
            layout.addWidget(report_widget)

            button_box = QHBoxLayout()
            export_btn = QPushButton("导出HTML")
            export_btn.clicked.connect(lambda: self._export_folder_html(record))
            close_btn = QPushButton("关闭")
            close_btn.clicked.connect(dialog.close)

            button_box.addStretch()
            button_box.addWidget(export_btn)
            button_box.addWidget(close_btn)
            layout.addLayout(button_box)

            dialog.exec()
        except Exception as e:
            logger.error("[view] folder summary failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "报告错误", f"生成文件夹压缩报告失败:\n{e}")

    def _export_folder_html(self, record: FolderRecord):
        """导出文件夹压缩报告为HTML文件"""
        try:
            html = self._generate_folder_summary_html(record)
            file_path, _ = QFileDialog.getSaveFileName(
                self,
                "保存报告",
                f"{record.name}_report.html",
                "HTML文件 (*.html)"
            )
            if file_path:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(html)
                QMessageBox.information(self, "成功", f"报告已保存到:\n{file_path}")
        except Exception as e:
            logger.error("[view] export folder html failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "导出错误", f"导出报告失败:\n{e}")

    def _generate_folder_summary_html(self, record: FolderRecord) -> str:
        import json
        rows_html = ""
        for f in record.files:
            status_str = f.status.value
            size_str = formatted_size(f.size)
            ratio_str = f"{f.compression_ratio * 100:.1f}%({formatted_size(_compressed_size(f))}/{formatted_size(f.size)})" if (f.status == CompressionStatus.DONE and (f.compressed_data or f.compressed_path)) else "--"
            time_str = f"{f.compression_time_ms:.1f}ms" if f.status == CompressionStatus.DONE else "--"
            stored_str = " [stored]" if getattr(f, 'is_stored', False) else ""
            algo_str = f.algorithm.value if f.status == CompressionStatus.DONE else "--"
            type_str = f.type.value if hasattr(f, 'type') else "?"
            rows_html += f"<tr><td>{f.name}</td><td>{type_str}</td><td>{size_str}</td><td>{algo_str}{stored_str}</td><td>{ratio_str}</td><td>{time_str}</td><td>{status_str}</td></tr>\n"

        total_ratio = f"{record.compression_ratio * 100:.1f}%({formatted_size(record.total_compressed)}/{formatted_size(record.total_original)})"
        total_time = f"{record.total_time_ms:.1f}ms"
        success = record.success_count
        total = record.filenum
        stored = sum(1 for f in record.files if getattr(f, 'is_stored', False))

        return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>文件夹压缩报告 - {record.name}</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ font-family: "Microsoft YaHei", "Segoe UI", sans-serif; background: {ThemeManager.hex('bg_primary')}; color: {ThemeManager.hex('text_primary')}; padding: 24px; }}
h1 {{ font-size: 20px; margin-bottom: 8px; }}
.meta {{ color: {ThemeManager.hex('text_secondary')}; font-size: 13px; margin-bottom: 20px; }}
.stats {{ display: flex; gap: 16px; margin-bottom: 24px; flex-wrap: wrap; }}
.stat {{ background: {ThemeManager.hex('bg_surface')}; border-radius: 8px; padding: 14px 18px; min-width: 120px; }}
.stat .num {{ font-size: 22px; font-weight: 700; color: {ThemeManager.hex('text_primary')}; }}
.stat .desc {{ font-size: 12px; color: {ThemeManager.hex('text_secondary')}; margin-top: 4px; }}
table {{ width: 100%; border-collapse: collapse; font-size: 13px; }}
th {{ background: {ThemeManager.hex('bg_surface')}; color: {ThemeManager.hex('text_secondary')}; padding: 10px 12px; text-align: left; border-bottom: 1px solid {ThemeManager.hex('border_dark')}; }}
td {{ padding: 8px 12px; border-bottom: 1px solid {ThemeManager.hex('bg_surface')}; }}
tr:hover {{ background: {ThemeManager.hex('bg_surface')}; }}
.stored {{ color: {ThemeManager.hex('warning')}; }}
</style>
</head>
<body>
<h1>📋 文件夹压缩报告</h1>
<div class="meta">{record.name} &nbsp;|&nbsp; {record.filenum} 个文件 &nbsp;|&nbsp; 成功 {success}/{total} &nbsp;|&nbsp; stored {stored}</div>
<div class="stats">
  <div class="stat"><div class="num">{formatted_size(record.size)}</div><div class="desc">原始总大小</div></div>
  <div class="stat"><div class="num">{formatted_size(record.total_compressed)}</div><div class="desc">压缩后总大小</div></div>
  <div class="stat"><div class="num">{total_ratio}</div><div class="desc">总压缩率</div></div>
  <div class="stat"><div class="num">{total_time}</div><div class="desc">总耗时</div></div>
</div>
<table>
<thead><tr><th>文件名</th><th>类型</th><th>大小</th><th>算法</th><th>压缩率</th><th>耗时</th><th>状态</th></tr></thead>
<tbody>
{rows_html}
</tbody>
</table>
</body>
</html>"""


class FolderReportWidget(QWidget):
    """Qt原生实现的文件夹压缩报告组件，替代HTML渲染"""

    def __init__(self, record: FolderRecord, parent=None):
        super().__init__(parent)
        self._record = record
        self._setup_ui()
        self._populate_data()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(24, 24, 24, 24)
        main_layout.setSpacing(16)

        title = QLabel("📋 文件夹压缩报告")
        title.setStyleSheet(f"font-size: 20px; font-weight: bold; color: {ThemeManager.hex('text_primary')};")
        main_layout.addWidget(title)

        meta = self._create_meta_label()
        main_layout.addWidget(meta)

        stats_widget = self._create_stats_widget()
        main_layout.addWidget(stats_widget)

        table = self._create_table()
        main_layout.addWidget(table)

    def _create_meta_label(self) -> QLabel:
        record = self._record
        success = record.success_count
        total = record.filenum
        stored = sum(1 for f in record.files if getattr(f, 'is_stored', False))
        meta_text = f"{record.name}  |  {total} 个文件  |  成功 {success}/{total}  |  stored {stored}"
        label = QLabel(meta_text)
        label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 13px;")
        return label

    def _create_stats_widget(self) -> QWidget:
        widget = QWidget()
        layout = QHBoxLayout(widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(16)

        record = self._record
        total_ratio = f"{record.compression_ratio * 100:.1f}%({formatted_size(record.total_compressed)}/{formatted_size(record.total_original)})"
        total_time = f"{record.total_time_ms:.1f}ms"

        stats_data = [
            (formatted_size(record.size), "原始总大小"),
            (formatted_size(record.total_compressed), "压缩后总大小"),
            (total_ratio, "总压缩率"),
            (total_time, "总耗时")
        ]

        for num, desc in stats_data:
            stat_card = self._create_stat_card(num, desc)
            layout.addWidget(stat_card)

        layout.addStretch()
        return widget

    def _create_stat_card(self, number: str, description: str) -> QWidget:
        card = QWidget()
        card.setMinimumWidth(120)
        card.setStyleSheet(f"""
            background: {ThemeManager.hex('bg_surface')};
            border-radius: 8px;
            padding: 14px 18px;
        """)
        layout = QVBoxLayout(card)
        layout.setContentsMargins(18, 14, 18, 14)
        layout.setSpacing(4)

        num_label = QLabel(number)
        num_label.setStyleSheet(f"font-size: 22px; font-weight: 700; color: {ThemeManager.hex('text_primary')};")
        layout.addWidget(num_label)

        desc_label = QLabel(description)
        desc_label.setStyleSheet(f"font-size: 12px; color: {ThemeManager.hex('text_secondary')};")
        layout.addWidget(desc_label)

        return card

    def _create_table(self) -> QTableWidget:
        table = QTableWidget()
        table.setColumnCount(7)
        table.setHorizontalHeaderLabels(["文件名", "类型", "大小", "算法", "压缩率", "耗时", "状态"])
        table.horizontalHeader().setStretchLastSection(True)
        table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        table.setAlternatingRowColors(True)
        table.setStyleSheet(f"""
            QTableWidget {{
                gridline-color: {ThemeManager.hex('border_dark')};
                background: {ThemeManager.hex('bg_primary')};
                color: {ThemeManager.hex('text_primary')};
                font-size: 13px;
                border: 1px solid {ThemeManager.hex('border_dark')};
                border-radius: 4px;
            }}
            QTableWidget::item {{
                padding: 8px 12px;
                border-bottom: 1px solid {ThemeManager.hex('bg_surface')};
            }}
            QTableWidget::item:selected {{
                background: {ThemeManager.hex('accent')};
                color: {ThemeManager.hex('text_primary')};
            }}
            QHeaderView::section {{
                background: {ThemeManager.hex('bg_surface')};
                color: {ThemeManager.hex('text_secondary')};
                padding: 10px 12px;
                border: none;
                border-bottom: 2px solid {ThemeManager.hex('border_dark')};
                font-weight: bold;
            }}
        """)
        return table

    def _populate_data(self):
        table = self.findChild(QTableWidget)
        if not table:
            return

        record = self._record
        table.setRowCount(len(record.files))

        for row, f in enumerate(record.files):
            status_str = f.status.value
            size_str = formatted_size(f.size)
            ratio_str = f"{f.compression_ratio * 100:.1f}%({formatted_size(_compressed_size(f))}/{formatted_size(f.size)})" if (f.status == CompressionStatus.DONE and (f.compressed_data or f.compressed_path)) else "--"
            time_str = f"{f.compression_time_ms:.1f}ms" if f.status == CompressionStatus.DONE else "--"
            stored_str = " [stored]" if getattr(f, 'is_stored', False) else ""
            algo_str = f.algorithm.value if f.status == CompressionStatus.DONE else "--"
            type_str = f.type.value if hasattr(f, 'type') else "?"

            name_item = QTableWidgetItem(f.name)
            table.setItem(row, 0, name_item)

            type_item = QTableWidgetItem(type_str)
            table.setItem(row, 1, type_item)

            size_item = QTableWidgetItem(size_str)
            table.setItem(row, 2, size_item)

            algo_item = QTableWidgetItem(f"{algo_str}{stored_str}")
            if stored_str:
                algo_item.setForeground(QColor(ThemeManager.hex('warning')))
            table.setItem(row, 3, algo_item)

            ratio_item = QTableWidgetItem(ratio_str)
            table.setItem(row, 4, ratio_item)

            time_item = QTableWidgetItem(time_str)
            table.setItem(row, 5, time_item)

            status_item = QTableWidgetItem(status_str)
            table.setItem(row, 6, status_item)

        table.resizeColumnsToContents()
