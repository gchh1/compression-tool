"""Project resource tree (left sidebar)."""

from __future__ import annotations

import os
from pathlib import Path

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QIcon, QAction
from PyQt6.QtWidgets import (
    QTreeWidget, QTreeWidgetItem, QMenu, QVBoxLayout, QWidget, QLabel,
    QHeaderView,
)

from gui.models import ArchiveEntry, FileRecord, FolderRecord, formatted_size, _filetype
from gui.config.theme import ThemeManager


class ResourceTree(QWidget):
    """Left-side panel: project file tree + type filter."""

    file_selected = pyqtSignal(object)  # Record
    file_double_clicked = pyqtSignal(object)  # Record
    compress_requested = pyqtSignal(object)  # Record

    def __init__(self, parent=None):
        super().__init__(parent)
        self._records: dict[str, object] = {}  # path → Record
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        header = QLabel("资源列表")
        header.setStyleSheet("font-weight: bold; padding: 4px; font-size: 13px;")
        layout.addWidget(header)

        self._tree = QTreeWidget()
        self._tree.setHeaderLabels(["名称", "大小"])
        self._tree.setColumnWidth(0, 160)
        self._tree.setColumnWidth(1, 70)
        self._tree.setRootIsDecorated(True)
        self._tree.setAlternatingRowColors(True)
        self._tree.setIndentation(16)
        self._tree.header().setStretchLastSection(False)
        self._tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self._tree.itemClicked.connect(self._on_item_clicked)
        self._tree.itemDoubleClicked.connect(self._on_double_clicked)
        self._tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._tree.customContextMenuRequested.connect(self._on_context_menu)
        layout.addWidget(self._tree)

        self._count_label = QLabel("")
        self._count_label.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; font-size: 11px; padding: 2px;")
        layout.addWidget(self._count_label)

    def load_records(self, records: list) -> None:
        """Populate tree from FileRecord/FolderRecord list."""
        self._tree.clear()
        self._records.clear()

        # Group by directory
        dirs: dict[str, list] = {}
        files: list = []
        for rec in records:
            if isinstance(rec, FolderRecord):
                dirs[rec.path] = list(rec.files)
            else:
                files.append(rec)

        # Add standalone files
        for rec in files:
            self._add_file_item(self._tree, rec)

        # Add folders
        for dir_path, dir_files in dirs.items():
            folder_item = QTreeWidgetItem(self._tree)
            name = Path(dir_path).name
            total_size = sum(f.size for f in dir_files)
            folder_item.setText(0, f"📁 {name}")
            folder_item.setText(1, formatted_size(total_size))
            folder_item.setData(0, Qt.ItemDataRole.UserRole, None)  # non-selectable
            folder_item.setExpanded(True)
            for f in dir_files:
                self._add_file_item(folder_item, f)

        total = len(self._records)
        self._count_label.setText(f"{total} 个项目")

    def load_archive_entries(self, entries: list[ArchiveEntry]) -> None:
        """Populate tree from archive entries, reconstructing folder structure."""
        self._tree.clear()
        self._records.clear()

        # Reconstruct folder hierarchy from flat paths
        dirs: dict[str, QTreeWidgetItem] = {}
        files: list[tuple[str, ArchiveEntry]] = []

        for entry in entries:
            name = entry.name.replace('\\', '/')
            if '/' in name:
                parent_dir = name.rsplit('/', 1)[0]
                files.append((parent_dir, entry))
            else:
                files.append(('', entry))

        # Ensure all intermediate directories exist
        all_dirs: set[str] = set()
        for parent_dir, _ in files:
            if parent_dir:
                parts = parent_dir.split('/')
                for i in range(1, len(parts) + 1):
                    all_dirs.add('/'.join(parts[:i]))

        # Create directory items
        for d in sorted(all_dirs):
            item = QTreeWidgetItem()
            item.setText(0, f"📁 {d.split('/')[-1]}")
            item.setText(1, "")
            item.setData(0, Qt.ItemDataRole.UserRole, None)
            dirs[d] = item

        # Place directory items in their parents
        for d in sorted(all_dirs):
            if '/' in d:
                parent = d.rsplit('/', 1)[0]
                if parent in dirs:
                    dirs[parent].addChild(dirs[d])
                else:
                    self._tree.addTopLevelItem(dirs[d])
            else:
                self._tree.addTopLevelItem(dirs[d])

        # Add file items
        for parent_dir, entry in files:
            item = QTreeWidgetItem()
            item.setText(0, f"📄 {entry.name.split('/')[-1]}")
            item.setText(1, formatted_size(entry.size))
            item.setData(0, Qt.ItemDataRole.UserRole, entry)
            item.setToolTip(0, entry.name)
            self._records[entry.name] = entry

            if parent_dir and parent_dir in dirs:
                dirs[parent_dir].addChild(item)
            else:
                self._tree.addTopLevelItem(item)

        # Expand first level
        for i in range(self._tree.topLevelItemCount()):
            self._tree.topLevelItem(i).setExpanded(True)

        total = len(entries)
        self._count_label.setText(f"压缩包内 {total} 个文件")

    def remove_selected(self) -> object | None:
        """Remove the currently selected item. Returns the removed data object or None."""
        items = self._tree.selectedItems()
        if not items:
            return None
        item = items[0]
        rec = item.data(0, Qt.ItemDataRole.UserRole)
        parent = item.parent()
        if parent:
            parent.removeChild(item)
        else:
            idx = self._tree.indexOfTopLevelItem(item)
            if idx >= 0:
                self._tree.takeTopLevelItem(idx)
        return rec

    def get_selected_records(self) -> list:
        """Return data objects for all selected items."""
        result = []
        for item in self._tree.selectedItems():
            rec = item.data(0, Qt.ItemDataRole.UserRole)
            if rec is not None:
                result.append(rec)
        return result

    def get_all_records(self) -> list:
        """Return all data objects currently in the tree."""
        return list(self._records.values())

    def _add_file_item(self, parent: QTreeWidgetItem | QTreeWidget, rec: FileRecord) -> None:
        item = QTreeWidgetItem()
        icon = self._type_icon(rec)
        item.setText(0, f"{icon} {rec.name}")
        item.setText(1, formatted_size(rec.size))
        item.setData(0, Qt.ItemDataRole.UserRole, rec)
        item.setToolTip(0, rec.path)

        if isinstance(parent, QTreeWidget):
            parent.addTopLevelItem(item)
        else:
            parent.addChild(item)

        self._records[rec.path] = rec

    @staticmethod
    def _type_icon(rec: FileRecord) -> str:
        t = rec.type.value
        if t == "script":
            return "📜"
        elif t == "image":
            return "🖼"
        elif t == "text":
            return "📄"
        elif t == "compressed":
            return "📦"
        else:
            return "❓"

    def _on_item_clicked(self, item: QTreeWidgetItem, col: int) -> None:
        rec = item.data(0, Qt.ItemDataRole.UserRole)
        if rec:
            self.file_selected.emit(rec)

    def _on_double_clicked(self, item: QTreeWidgetItem, col: int) -> None:
        rec = item.data(0, Qt.ItemDataRole.UserRole)
        if rec:
            self.file_double_clicked.emit(rec)

    def _on_context_menu(self, pos) -> None:
        item = self._tree.itemAt(pos)
        if not item:
            return
        rec = item.data(0, Qt.ItemDataRole.UserRole)
        if rec is None:
            return

        menu = QMenu(self)

        if isinstance(rec, ArchiveEntry):
            # Archive browsing mode
            remove_action = menu.addAction("🗑 从列表移除")
            action = menu.exec(self._tree.viewport().mapToGlobal(pos))
            if action == remove_action:
                self.remove_selected()
        else:
            # Compression mode
            compress_action = menu.addAction("🔧 压缩此项")
            menu.addSeparator()
            remove_action = menu.addAction("🗑 从列表移除")
            view_action = menu.addAction("🔬 查看分析")
            action = menu.exec(self._tree.viewport().mapToGlobal(pos))

            if action == compress_action:
                self.compress_requested.emit(rec)
            elif action == remove_action:
                self.remove_selected()
            elif action == view_action:
                self.file_double_clicked.emit(rec)

    def update_compression_result(self, rec: FileRecord) -> None:
        """Update tree item after compression completes."""
        # Find and update the item
        for i in range(self._tree.topLevelItemCount()):
            self._update_item_recursive(self._tree.topLevelItem(i), rec)

    def _update_item_recursive(self, item: QTreeWidgetItem, rec: FileRecord) -> None:
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if data is rec:
            if rec.status.value == "done":
                item.setText(1, f"{formatted_size(rec.size)} → {formatted_size(len(rec.compressed_data or []))}")
                item.setForeground(1, Qt.GlobalColor.darkGreen)
        for i in range(item.childCount()):
            self._update_item_recursive(item.child(i), rec)
