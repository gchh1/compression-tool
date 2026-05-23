from __future__ import annotations

import logging
import os
from pathlib import Path

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QAction, QBrush, QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QTreeWidget,
    QTreeWidgetItem,
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
    formatted_size,
    compressed_payload_size,
)
from gui.config.theme import ThemeManager

logger = logging.getLogger('gui.table')


class FileTableWidget(QTreeWidget):
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
    request_heatmap = pyqtSignal(Record)
    request_comparison = pyqtSignal(Record)
    request_network = pyqtSignal(Record)
    request_webpage_heatmap = pyqtSignal(Record)
    request_folder_summary = pyqtSignal(Record)
    request_decision_detail = pyqtSignal(Record)
    request_view_viz = pyqtSignal(Record)
    request_folder_heatmap = pyqtSignal(Record)
    request_browse_decompress = pyqtSignal(object)  # CompressedFileHeader

    _SEL_TOGGLE = (
        Qt.ItemFlag.ItemIsUserCheckable | Qt.ItemFlag.ItemIsSelectable
    )

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        self.setColumnCount(7)
        self.setHeaderLabels(["☐", "文件名", "大小", "类型", "状态", "算法", "压缩率"])
        self.header().setStretchLastSection(True)
        self.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.setAlternatingRowColors(True)
        self.setAnimated(True)
        self.setExpandsOnDoubleClick(True)
        self.setIndentation(20)

        self.setColumnHidden(self.COL_CHECK, True)
        self.setColumnWidth(self.COL_NAME, 300)
        self.setColumnWidth(self.COL_SIZE, 100)
        self.setColumnWidth(self.COL_TYPE, 80)
        self.setColumnWidth(self.COL_STATUS, 130)
        self.setColumnWidth(self.COL_ALGORITHM, 100)
        self.setColumnWidth(self.COL_RATIO, 170)
        self.setStyleSheet(ThemeManager.table_sheet())

        self.itemChanged.connect(self._on_item_changed)
        self.itemExpanded.connect(self._on_item_expanded)

        ThemeManager().theme_changed.connect(self._on_theme_changed)

    def _on_theme_changed(self):
        self.setStyleSheet(ThemeManager.table_sheet())

    # kept for backward compatibility
    def refresh_theme(self):
        self._on_theme_changed()

    # ── Item creation ─────────────────────────────────────────────

    def _create_file_item(self, record: FileRecord) -> QTreeWidgetItem:
        item = QTreeWidgetItem()
        item.setText(self.COL_NAME, record.name)
        item.setData(self.COL_NAME, self.Record_Role, record)
        item.setText(self.COL_SIZE, formatted_size(record.size))
        item.setText(self.COL_TYPE, record.type.value)
        item.setText(self.COL_STATUS, CompressionStatus.PENDING.value)
        item.setText(self.COL_ALGORITHM, "-")
        item.setText(self.COL_RATIO, "--")
        return item

    def _create_folder_item(self, record: FolderRecord) -> QTreeWidgetItem:
        item = QTreeWidgetItem()
        item.setText(self.COL_NAME, record.name)
        item.setData(self.COL_NAME, self.Record_Role, record)
        item.setForeground(self.COL_NAME, QBrush(ThemeManager.color("status_folder")))
        item.setText(self.COL_SIZE, "...")
        item.setText(self.COL_TYPE, "Folder")
        item.setText(self.COL_STATUS, CompressionStatus.PENDING.value)
        item.setText(self.COL_ALGORITHM, "-")
        item.setText(self.COL_RATIO, "--")
        # Placeholder child so the expand arrow appears
        place = QTreeWidgetItem()
        place.setText(self.COL_NAME, "...")
        item.addChild(place)
        return item

    def _create_browse_folder_item(self, name: str) -> QTreeWidgetItem:
        """Create a browse-mode folder node."""
        item = QTreeWidgetItem()
        item.setText(self.COL_NAME, name)
        item.setText(self.COL_TYPE, "Folder")
        item.setText(self.COL_SIZE, "-")
        item.setText(self.COL_STATUS, "-")
        item.setText(self.COL_ALGORITHM, "-")
        item.setText(self.COL_RATIO, "-")
        item.setForeground(self.COL_NAME, QBrush(ThemeManager.color("status_folder")))
        return item

    def _create_browse_item(self, entry) -> QTreeWidgetItem:
        """Create a browse-mode file item from a scanned WcxEntrySummary."""
        from gui.models import AlgorithmType
        item = QTreeWidgetItem()
        item.setText(self.COL_NAME, Path(entry.filename).name)
        item.setData(self.COL_NAME, self.Record_Role, entry)
        item.setText(self.COL_SIZE, formatted_size(entry.original_size))
        item.setText(self.COL_TYPE, Path(entry.filename).suffix or "-")
        item.setText(self.COL_STATUS, formatted_size(entry.compressed_size))
        algo = AlgorithmType.from_code(entry.algo_code)
        algo_name = algo.value if algo else f"#{entry.algo_code}"
        item.setText(self.COL_ALGORITHM, algo_name)
        ratio_pct = (entry.compressed_size / max(entry.original_size, 1)) * 100
        item.setText(self.COL_RATIO, f"{ratio_pct:.2f}%")
        return item

    def _create_browse_item_from_header(self, hdr, payload_size: int) -> QTreeWidgetItem:
        """Create a browse-mode file item from a CompressedFileHeader."""
        item = QTreeWidgetItem()
        name = Path(hdr.original_filename).name if hdr.original_filename else "?"
        item.setText(self.COL_NAME, name)
        item.setData(self.COL_NAME, self.Record_Role, hdr)
        item.setText(self.COL_SIZE, formatted_size(hdr.original_size))
        item.setText(self.COL_TYPE, Path(hdr.original_filename).suffix or "-")
        item.setText(self.COL_STATUS, formatted_size(payload_size))
        algo_name = hdr.algorithm.value if hdr.algorithm else "-"
        item.setText(self.COL_ALGORITHM, algo_name)
        ratio_pct = (payload_size / max(hdr.original_size, 1)) * 100
        item.setText(self.COL_RATIO, f"{ratio_pct:.2f}%")
        return item

    def _collect_all_browse_items(self) -> list[QTreeWidgetItem]:
        """Walk the entire tree and return all items that hold a
        CompressedFileHeader (browse-mode file entries)."""
        from gui.engine.file_protocol import CompressedFileHeader
        result: list[QTreeWidgetItem] = []

        def _walk(item: QTreeWidgetItem) -> None:
            rec = item.data(self.COL_NAME, self.Record_Role)
            if isinstance(rec, CompressedFileHeader):
                result.append(item)
            for ci in range(item.childCount()):
                _walk(item.child(ci))

        for i in range(self.topLevelItemCount()):
            _walk(self.topLevelItem(i))
        return result

    def _populate_folder_children(self, folder_item: QTreeWidgetItem) -> None:
        """Lazy-load direct children when a folder is expanded."""
        record = folder_item.data(self.COL_NAME, self.Record_Role)
        if not isinstance(record, FolderRecord):
            return

        # Remove placeholder
        while folder_item.childCount() > 0:
            folder_item.removeChild(folder_item.child(0))

        record.ensure_tree_loaded()
        for entry in record.entries:
            if isinstance(entry, FileRecord):
                child = self._create_file_item(entry)
            elif isinstance(entry, FolderRecord):
                child = self._create_folder_item(entry)
            else:
                continue
            folder_item.addChild(child)

        # Update folder summary after loading
        record.ensure_files_loaded()
        folder_item.setText(self.COL_SIZE, f"{record.filenum} 文件 / {formatted_size(record.size)}")
        if record.total_original > 0:
            folder_item.setText(self.COL_RATIO,
                                f"{(record.compression_ratio * 100):.2f}%")
        if record.total_time_ms > 0:
            folder_item.setText(self.COL_STATUS, record.status.value)

    # ── Public API ────────────────────────────────────────────────

    def add_file(self, path: str) -> int:
        """Add a top-level file item. Returns top-level index."""
        try:
            record = FileRecord(path)
            item = self._create_file_item(record)
            self.addTopLevelItem(item)
            return self.topLevelItemCount() - 1
        except Exception as e:
            logger.exception("add_file crash: path=%s, err=%s", path, e)
            return -1

    def add_folder(self, path: str) -> int:
        """Add a top-level folder item. Returns top-level index."""
        try:
            record = FolderRecord(path)
            item = self._create_folder_item(record)
            self.addTopLevelItem(item)
            return self.topLevelItemCount() - 1
        except Exception as e:
            logger.exception("add_folder crash: path=%s, err=%s", path, e)
            return -1

    def add_paths(self, paths: list[str]) -> tuple[int, int]:
        count_files = 0
        count_dirs = 0
        self.blockSignals(True)
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
        self._emit_selection_changed()
        return count_files, count_dirs

    # ── Record retrieval ──────────────────────────────────────────

    def get_record(self, arg) -> Record | None:
        """Get Record from QTreeWidgetItem, top-level row index, or (row, col)."""
        if isinstance(arg, QTreeWidgetItem):
            return arg.data(self.COL_NAME, self.Record_Role)
        if isinstance(arg, int):
            item = self.topLevelItem(arg)
            if item is None:
                return None
            return item.data(self.COL_NAME, self.Record_Role)
        return None

    def get_records(self, selected: bool = False) -> list[Record]:
        """Return records for all top-level items (selected=False) or selected rows."""
        records: list[Record] = []
        if selected:
            sel_rows = self.selected_rows
        for i in range(self.topLevelItemCount()):
            item = self.topLevelItem(i)
            if item is None:
                continue
            if selected and i not in sel_rows:
                continue
            rec = self.get_record(item)
            if rec is not None:
                records.append(rec)
        return records

    def row_for_record(self, record: Record) -> QTreeWidgetItem | None:
        """Find the QTreeWidgetItem (at any depth) that holds this record."""
        for i in range(self.topLevelItemCount()):
            found = self._find_item_by_record(self.topLevelItem(i), record)
            if found is not None:
                return found
        return None

    def _find_item_by_record(self, root: QTreeWidgetItem, record: Record) -> QTreeWidgetItem | None:
        root_rec = root.data(self.COL_NAME, self.Record_Role)
        if root_rec is record:
            return root
        if (isinstance(root_rec, FileRecord) and isinstance(record, FileRecord)
                and root_rec.path == record.path):
            return root
        for i in range(root.childCount()):
            found = self._find_item_by_record(root.child(i), record)
            if found is not None:
                return found
        return None

    def top_level_index_for_record(self, record: Record) -> int | None:
        """Return the top-level index containing ``record`` (file inside folder or top-level)."""
        for i in range(self.topLevelItemCount()):
            top = self.topLevelItem(i)
            top_rec = self.get_record(top)
            if top_rec is record:
                return i
            if isinstance(top_rec, FolderRecord):
                top_rec.ensure_files_loaded()
                if record in top_rec.files:
                    return i
        return None

    # ── Row update ────────────────────────────────────────────────

    def update_row(self, row: int) -> None:
        """Update a top-level item's cells (backward compat)."""
        item = self.topLevelItem(row)
        if item is None:
            return
        self._update_item(item)

    def _update_item(self, item: QTreeWidgetItem) -> None:
        record = self.get_record(item)
        if record is None:
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
            item.setText(self.COL_STATUS, status_text)

            if record.total_original > 0:
                ratio_str = f"{record.compression_ratio * 100:.2f}%({formatted_size(record.total_compressed)}/{formatted_size(record.total_original)})"
            else:
                ratio_str = "--"
            item.setText(self.COL_RATIO, ratio_str)
        else:
            if record.status == CompressionStatus.DONE:
                time_str = f" ({record.compression_time_ms:.0f}ms)" if record.compression_time_ms > 0 else ""
                stored_tag = " [stored]" if getattr(record, 'is_stored', False) else ""
                item.setText(self.COL_STATUS, f"done{stored_tag}{time_str}")
            else:
                item.setText(self.COL_STATUS, record.status.value)

            if hasattr(record, 'algorithm') and record.algorithm and record.status == CompressionStatus.DONE:
                algo_name = record.algorithm.value if hasattr(record.algorithm, 'value') else str(record.algorithm)
                item.setText(self.COL_ALGORITHM, algo_name)
                item.setTextAlignment(self.COL_ALGORITHM, Qt.AlignmentFlag.AlignCenter)
            elif record.status != CompressionStatus.PENDING:
                item.setText(self.COL_ALGORITHM, "-")

            if record.status == CompressionStatus.DONE and record.size > 0:
                comp_sz = compressed_payload_size(record)
                ratio_str = f"{record.compression_ratio * 100:.2f}%({formatted_size(comp_sz)}/{formatted_size(record.size)})"
            else:
                ratio_str = "--"
            item.setText(self.COL_RATIO, ratio_str)

        # Propagate to parent folder
        parent = item.parent()
        if parent is not None:
            self._update_item(parent)

    def update_record(self, record: Record) -> None:
        """Find the item for ``record``, swap in the caller's record, and refresh cells.

        The caller (worker) owns the authoritative ``FileRecord`` with up-to-date
        ``status``, ``viz_path``, ``heat_path``, etc.  Tree items built from
        sub-folder ``ensure_tree_loaded()`` hold *different* Python objects, so
        we must replace the stored reference for context menus and cell reads.
        """
        item = self.row_for_record(record)
        if item is not None:
            item.setData(self.COL_NAME, self.Record_Role, record)
            self._update_item(item)

    # ── Check / selection ─────────────────────────────────────────

    def _on_item_changed(self, item: QTreeWidgetItem, column: int) -> None:
        if column != self.COL_CHECK:
            return
        self.blockSignals(True)
        try:
            state = item.checkState(self.COL_CHECK)
            self._set_children_check(item, state)
            parent = item.parent()
            if parent is not None:
                self._update_parent_check(parent)
        finally:
            self.blockSignals(False)
        self._emit_selection_changed()

    def _set_children_check(self, parent: QTreeWidgetItem, state: Qt.CheckState) -> None:
        self.blockSignals(True)
        try:
            for i in range(parent.childCount()):
                child = parent.child(i)
                child.setCheckState(self.COL_CHECK, state)
                self._set_children_check(child, state)
        finally:
            self.blockSignals(False)

    def _update_parent_check(self, parent: QTreeWidgetItem) -> None:
        checked = 0
        unchecked = 0
        for i in range(parent.childCount()):
            s = parent.child(i).checkState(self.COL_CHECK)
            if s == Qt.CheckState.Checked:
                checked += 1
            elif s == Qt.CheckState.Unchecked:
                unchecked += 1
        if checked == 0:
            parent.setCheckState(self.COL_CHECK, Qt.CheckState.Unchecked)
        elif unchecked == 0:
            parent.setCheckState(self.COL_CHECK, Qt.CheckState.Checked)
        else:
            parent.setCheckState(self.COL_CHECK, Qt.CheckState.PartiallyChecked)

    @property
    def selected_rows(self) -> list[int]:
        """Top-level indices of selected items (deduplicated)."""
        seen: set[int] = set()
        for idx in self.selectedIndexes():
            item = self.itemFromIndex(idx)
            if item is None:
                continue
            # Walk up to the top-level ancestor
            while item.parent() is not None:
                item = item.parent()
            tl_idx = self.indexOfTopLevelItem(item)
            if tl_idx >= 0:
                seen.add(tl_idx)
        return sorted(seen)

    @property
    def selected_items(self) -> list[QTreeWidgetItem]:
        """Return unique selected QTreeWidgetItems at any tree depth."""
        seen: set[int] = set()
        items: list[QTreeWidgetItem] = []
        for idx in self.selectedIndexes():
            item = self.itemFromIndex(idx)
            if item is not None and id(item) not in seen:
                seen.add(id(item))
                items.append(item)
        return items

    def selected_records(self) -> list[Record]:
        """Return records from all selected items (any depth)."""
        return [self.get_record(it) for it in self.selected_items if self.get_record(it) is not None]

    @property
    def is_all_selected(self) -> bool:
        n = self.topLevelItemCount()
        return n > 0 and len(self.selected_rows) == n

    def select_all(self, checked: bool = True) -> None:
        if checked:
            self.selectAll()
        else:
            self.clearSelection()

    def _emit_selection_changed(self) -> None:
        try:
            self.selection_changed.emit()
        except Exception:
            pass

    # ── Error marking ─────────────────────────────────────────────

    def mark_error(self, row: int) -> None:
        item = self.topLevelItem(row)
        if item is None:
            return
        record = self.get_record(item)
        if record:
            record.status = CompressionStatus.FAILED
        item.setText(self.COL_STATUS, CompressionStatus.FAILED.value)
        item.setForeground(self.COL_STATUS, QBrush(ThemeManager.color("status_error")))

    def mark_error_record(self, record: Record) -> None:
        item = self.row_for_record(record)
        if item is None:
            logger.warning("[mark_error_record] record not in tree: %s", getattr(record, 'name', '?'))
            return
        record.status = CompressionStatus.FAILED
        item.setText(self.COL_STATUS, CompressionStatus.FAILED.value)
        item.setForeground(self.COL_STATUS, QBrush(ThemeManager.color("status_error")))
        self._update_item(item)

    # ── Cleanup ───────────────────────────────────────────────────

    def remove_row(self, row: int) -> None:
        """Remove a top-level item."""
        item = self.topLevelItem(row)
        if item is not None:
            idx = self.indexFromItem(item)
            self.takeTopLevelItem(row)
            # Qt may leave orphaned selection; force a repaint
            self.selectionModel().clear()
        self._emit_selection_changed()

    def clear_all(self) -> None:
        self.clear()
        self._emit_selection_changed()

    # ── Events ────────────────────────────────────────────────────

    def _on_item_expanded(self, item: QTreeWidgetItem) -> None:
        """Lazy-load children when a folder is expanded for the first time."""
        record = self.get_record(item)
        if isinstance(record, FolderRecord) and not record._tree_loaded:
            self._populate_folder_children(item)

    def contextMenuEvent(self, event) -> None:
        try:
            item = self.itemAt(event.pos())
            if item is None:
                return
            record = self.get_record(item)
            if record is None:
                return
            menu = QMenu(self)

            if isinstance(record, FolderRecord):
                actions = self._build_folder_menu(menu, record)
            elif isinstance(record, FileRecord):
                actions = self._build_file_menu(menu, record)
            else:
                from gui.engine.file_protocol import CompressedFileHeader
                if isinstance(record, CompressedFileHeader):
                    actions = self._build_browse_file_menu(menu, item, record)
                else:
                    actions = {}
                    actions[menu.addAction("移除")] = lambda: self._remove_item(item)

            chosen = menu.exec(event.globalPos())
            if chosen is not None and chosen in actions:
                actions[chosen]()
        except Exception as e:
            logger.exception("contextMenuEvent crash: %s", e)

    def _build_folder_menu(self, menu: QMenu, record: FolderRecord) -> dict:
        actions = {}
        actions[menu.addAction("文件夹压缩报告")] = lambda: self.request_folder_summary.emit(record)
        actions[menu.addAction("文件夹熵热力图")] = lambda: self.request_folder_heatmap.emit(record)
        actions[menu.addAction("算法对比")] = lambda: self.request_comparison.emit(record)
        actions[menu.addAction("网页资源热力图")] = lambda: self.request_webpage_heatmap.emit(record)
        menu.addSeparator()
        item = self.row_for_record(record)
        actions[menu.addAction("移除")] = lambda it=item: self._remove_item(it) if it else None
        return actions

    def _build_file_menu(self, menu: QMenu, record: FileRecord) -> dict:
        actions = {}
        actions[menu.addAction("查看决策详情...")] = lambda: self.request_decision_detail.emit(record)
        menu.addSeparator()
        actions[menu.addAction("打开文件位置")] = lambda: self._open_file_location(record)
        actions[menu.addAction("复制文件路径")] = lambda: self._copy_file_path(record)
        menu.addSeparator()

        if record.status == CompressionStatus.DONE and getattr(record, "viz_path", None):
            actions[menu.addAction("查看压缩可视化 (.viz)")] = lambda: self.request_view_viz.emit(record)

        if record.status == CompressionStatus.DONE and (record.compressed_data or record.compressed_path):
            actions[menu.addAction("压缩热力图")] = lambda: self.request_heatmap.emit(record)
            actions[menu.addAction("算法对比")] = lambda: self.request_comparison.emit(record)
            actions[menu.addAction("网络传输模拟")] = lambda: self.request_network.emit(record)

        menu.addSeparator()
        item = self.row_for_record(record)
        actions[menu.addAction("移除")] = lambda it=item: self._remove_item(it) if it else None
        return actions

    def _build_browse_file_menu(self, menu: QMenu, item: QTreeWidgetItem,
                                record) -> dict:
        """Context menu for browse-mode file items (CompressedFileHeader)."""
        actions: dict = {}
        actions[menu.addAction("解压此文件")] = (
            lambda: self.request_browse_decompress.emit(record)
        )
        menu.addSeparator()
        actions[menu.addAction("移除")] = (
            lambda it=item: self._remove_item(it) if it else None
        )
        return actions

    def _remove_item(self, item: QTreeWidgetItem) -> None:
        parent = item.parent()
        if parent is not None:
            idx = parent.indexOfChild(item)
            parent.takeChild(idx)
            self._update_item(parent)
        else:
            idx = self.indexOfTopLevelItem(item)
            self.takeTopLevelItem(idx)
        self._emit_selection_changed()

    # ── File system helpers ───────────────────────────────────────

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
            if clipboard:
                clipboard.setText(str(Path(record.path).resolve()))
            logger.info("已复制路径到剪贴板: %s", record.path)
        except Exception as e:
            logger.error("复制路径失败: %s", e)
            QMessageBox.warning(self, "错误", f"无法复制路径:\n{e}")

    # ── Row count (top-level) ─────────────────────────────────────

    def rowCount(self) -> int:  # type: ignore[override]
        return self.topLevelItemCount()

    # ── Recursive count for progress ──────────────────────────────

    def count_done_items(self) -> int:
        """Count top-level items that are DONE or FAILED (for progress bar)."""
        done = 0
        for i in range(self.topLevelItemCount()):
            item = self.topLevelItem(i)
            if item is None:
                continue
            status = item.text(self.COL_STATUS)
            if status.startswith(CompressionStatus.DONE.value) or status == CompressionStatus.FAILED.value:
                done += 1
        return done
