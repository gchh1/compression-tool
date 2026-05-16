from __future__ import annotations

import logging
import os
from pathlib import Path

from PyQt6.QtCore import Qt, QItemSelectionModel, pyqtSignal
from PyQt6.QtGui import QAction, QBrush, QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QTableWidget,
    QTableWidgetItem,
    QComboBox,
    QMenu,
    QHeaderView,
    QMessageBox,
)

from gui.models import (
    AlgorithmType,
    CompressionStatus,
    FileRecord,
    FolderRecord,
    Record,
    ResourceType,
    formatted_size,
    compressed_payload_size,
)
from gui.config.theme import ThemeManager

logger = logging.getLogger('gui.table')

class FileTableWidget(QTableWidget):
    COL_CHECK = 0
    COL_NAME = 1
    COL_SIZE = 2
    COL_TYPE = 3
    COL_STATUS = 4
    COL_ALGORITHM = 5
    COL_RATIO = 6

    Record_Role = Qt.ItemDataRole.UserRole
    Check_Role = Qt.ItemDataRole.UserRole + 1

    selection_changed = pyqtSignal()
    request_demo = pyqtSignal(int)
    request_heatmap = pyqtSignal(int)
    request_comparison = pyqtSignal(int)
    request_network = pyqtSignal(int)
    request_webpage_heatmap = pyqtSignal(int)
    request_folder_summary = pyqtSignal(int)
    request_decision_detail = pyqtSignal(int)

    _SEL_TOGGLE = (
        QItemSelectionModel.SelectionFlag.Toggle | QItemSelectionModel.SelectionFlag.Rows
    )

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        self.setColumnCount(7)
        self.setHorizontalHeaderLabels(["☐", "文件名", "大小", "类型", "状态", "算法", "压缩率"])
        self.horizontalHeader().setStretchLastSection(True)
        self.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.setAlternatingRowColors(True)

        self.setColumnWidth(self.COL_CHECK, 36)
        self.setColumnWidth(self.COL_NAME, 260)
        self.setColumnWidth(self.COL_SIZE, 100)
        self.setColumnWidth(self.COL_TYPE, 80)
        self.setColumnWidth(self.COL_STATUS, 130)
        self.setColumnWidth(self.COL_ALGORITHM, 100)
        self.setColumnWidth(self.COL_RATIO, 170)
        self.setStyleSheet(ThemeManager.table_sheet())
        self.itemSelectionChanged.connect(self._sync_checkboxes_from_selection)

    def refresh_theme(self):
        self.setStyleSheet(ThemeManager.table_sheet())
        self._sync_checkboxes_from_selection()

    def _sync_checkboxes_from_selection(self) -> None:
        """Keep ☑ column / row brush in sync with Qt selection (含框选 rubber band)."""
        try:
            rows = {ix.row() for ix in self.selectionModel().selectedRows()}
            sel_color = ThemeManager.color("bg_selection")
            for row in range(self.rowCount()):
                check_item = self.item(row, self.COL_CHECK)
                if check_item is None:
                    continue
                checked = row in rows
                check_item.setData(self.Check_Role, checked)
                check_item.setText("☑" if checked else "☐")
                brush = QBrush(sel_color) if checked else QBrush()
                for col in range(1, self.columnCount()):
                    ci = self.item(row, col)
                    if ci is not None:
                        ci.setBackground(brush)
            self.selection_changed.emit()
        except Exception as e:
            logger.exception("_sync_checkboxes_from_selection: %s", e)

    def mousePressEvent(self, event) -> None:
        try:
            item = self.itemAt(event.pos())
            if item is not None:
                idx = self.model().index(item.row(), 0)
                self.selectionModel().select(idx, self._SEL_TOGGLE)
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
            
            if isinstance(record, FolderRecord):
                folder_summary_action = menu.addAction("📋 文件夹压缩报告")
                folder_comparison_action = menu.addAction("⚖ 算法对比")
                webpage_heatmap_action = menu.addAction("🌐 网页资源热力图")
                menu.addSeparator()
                delete_action = menu.addAction("🗑 移除")
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
                detail_action = menu.addAction("📊 查看决策详情...")
                menu.addSeparator()
                open_location_action = menu.addAction("📁 打开文件位置")
                copy_path_action = menu.addAction("📋 复制文件路径")
                menu.addSeparator()
                
                if record.status == CompressionStatus.DONE and record.compressed_data:
                    demo_action = menu.addAction("🔧 压缩演示")
                    heatmap_action = menu.addAction("📊 压缩热力图")
                    comparison_action = menu.addAction("⚖ 算法对比")
                    network_action = menu.addAction("🌐 网络传输模拟")
                else:
                    demo_action = None
                    heatmap_action = None
                    comparison_action = None
                    network_action = None
                
                menu.addSeparator()
                delete_action = menu.addAction("🗑 移除")
                action = menu.exec(event.globalPos())
                
                if action == detail_action:
                    self.request_decision_detail.emit(row)
                elif action == open_location_action:
                    self._open_file_location(record)
                elif action == copy_path_action:
                    self._copy_file_path(record)
                elif action == delete_action:
                    self.remove_row(row)
                elif demo_action and action == demo_action:
                    self.request_demo.emit(row)
                elif heatmap_action and action == heatmap_action:
                    self.request_heatmap.emit(row)
                elif comparison_action and action == comparison_action:
                    self.request_comparison.emit(row)
                elif network_action and action == network_action:
                    self.request_network.emit(row)
            else:
                delete_action = menu.addAction("🗑 移除")
                action = menu.exec(event.globalPos())
                if action == delete_action:
                    self.remove_row(row)
        except Exception as e:
            logger.exception("contextMenuEvent crash: %s", e)

    def remove_row(self, row: int) -> None:
        try:
            self.removeRow(row)
        except Exception as e:
            logger.exception("remove_row crash: row=%d, err=%s", row, e)

    def _open_file_location(self, record: Record) -> None:
        try:
            import subprocess
            import sys
            file_path = Path(record.path).resolve()
            if not file_path.exists():
                QMessageBox.warning(self, "错误", f"文件不存在:\n{file_path}")
                return
            
            if sys.platform == 'win32':
                subprocess.run(['explorer', '/select,', str(file_path)], check=False)
            elif sys.platform == 'darwin':
                subprocess.run(['open', '-R', str(file_path)], check=False)
            else:
                subprocess.run(['xdg-open', str(file_path.parent)], check=False)
        except Exception as e:
            logger.error("打开文件位置失败: %s", e)
            QMessageBox.warning(self, "错误", f"无法打开文件位置:\n{e}")

    def _copy_file_path(self, record: Record) -> None:
        try:
            from PyQt6.QtWidgets import QApplication
            clipboard = QApplication.clipboard()
            clipboard.setText(str(Path(record.path).resolve()))
            logger.info("已复制路径到剪贴板: %s", record.path)
        except Exception as e:
            logger.error("复制路径失败: %s", e)
            QMessageBox.warning(self, "错误", f"无法复制路径:\n{e}")

    def select_all(self, checked: bool = True) -> None:
        if checked:
            self.selectAll()
        else:
            self.clearSelection()

    @property
    def selected_rows(self) -> list[int]:
        return sorted({ix.row() for ix in self.selectionModel().selectedRows()})

    @property
    def is_all_selected(self) -> bool:
        n = self.rowCount()
        return n > 0 and len(self.selected_rows) == n

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
            self.setItem(row, self.COL_ALGORITHM, QTableWidgetItem("-"))
            self.setItem(row, self.COL_RATIO, QTableWidgetItem("--"))
            return row
        except Exception as e:
            logger.exception("add_file crash: path=%s, err=%s", path, e)
            return -1

    def add_folder(self, path: str) -> int:
        try:
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
            self.setItem(row, self.COL_ALGORITHM, QTableWidgetItem("-"))
            self.setItem(row, self.COL_RATIO, QTableWidgetItem("--"))
            return row
        except Exception as e:
            logger.exception("add_folder crash: path=%s, err=%s", path, e)
            return -1

    def add_paths(self, paths: list[str]) -> tuple[int, int]:
        count_files = 0
        count_dirs = 0
        sm = self.selectionModel()
        self.blockSignals(True)
        if sm is not None:
            sm.blockSignals(True)
        try:
            for path in paths:
                if os.path.isdir(path):
                    if self.add_folder(path) >= 0:
                        count_dirs += 1
                elif os.path.isfile(path):
                    if self.add_file(path) >= 0:
                        count_files += 1
        finally:
            self.blockSignals(False)
            if sm is not None:
                sm.blockSignals(False)
        self._sync_checkboxes_from_selection()
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
                
                if hasattr(record, 'algorithm') and record.algorithm and record.status == CompressionStatus.DONE:
                    algo_name = record.algorithm.value if hasattr(record.algorithm, 'value') else str(record.algorithm)
                    algo_item = QTableWidgetItem(algo_name)
                    algo_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                    self.setItem(row, self.COL_ALGORITHM, algo_item)
                elif record.status != CompressionStatus.PENDING:
                    self.setItem(row, self.COL_ALGORITHM, QTableWidgetItem("-"))
                
                if record.status == CompressionStatus.DONE and record.size > 0:
                    comp_sz = compressed_payload_size(record)
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

    def row_for_record(self, record: Record) -> int | None:
        """Current table row displaying ``record`` (top-level row or folder containing file)."""
        for r in range(self.rowCount()):
            top = self.get_record(r)
            if top is None:
                continue
            if top is record:
                return r
            if isinstance(top, FolderRecord):
                top.ensure_files_loaded()
                if record in top.files:
                    return r
        return None

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

    def mark_error_record(self, record: Record) -> None:
        """Like ``mark_error`` but resolves row by record (survives row reorder/remove during compress)."""
        row = self.row_for_record(record)
        if row is None:
            logger.warning("[mark_error_record] record not in table")
            return
        try:
            record.status = CompressionStatus.FAILED
            self.setItem(row, self.COL_STATUS, QTableWidgetItem(CompressionStatus.FAILED.value))
            status_item = self.item(row, self.COL_STATUS)
            if status_item:
                status_item.setForeground(QBrush(Qt.GlobalColor.red))
            self.update_row(row)
        except Exception as e:
            logger.error("[mark_error_record] CRASH: %s", e, exc_info=True)

    def clear_all(self) -> None:
        self.clearSelection()
        self.setRowCount(0)

