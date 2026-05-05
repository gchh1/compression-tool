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

from gui.core.models import FileRecord, FolderRecord, formatted_size, _filetype


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
        self._count_label.setStyleSheet("color: #888; font-size: 11px; padding: 2px;")
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
        self._count_label.setText(f"{total} 个文件")

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
        compress_action = menu.addAction("🔧 单独压缩此文件")
        menu.addSeparator()
        view_action = menu.addAction("🔬 查看分析")
        action = menu.exec(self._tree.viewport().mapToGlobal(pos))

        if action == compress_action:
            self.compress_requested.emit(rec)
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
