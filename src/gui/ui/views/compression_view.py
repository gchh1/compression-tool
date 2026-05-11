"""Compress page — file list, add/remove/compress actions, drop zone."""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QFrame,
)

from gui.models import FileRecord, FolderRecord, formatted_size
from gui.config.theme import ThemeManager
from gui.ui.panels.resource_tree import ResourceTree


class CompressPage(QWidget):
    """Main compress page: file/folder list with action buttons."""

    add_files_requested = pyqtSignal()
    add_folder_requested = pyqtSignal()
    compress_requested = pyqtSignal()
    clear_requested = pyqtSignal()
    file_selected = pyqtSignal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)

        # Header
        title = QLabel("文件压缩")
        title.setStyleSheet("font-size: 18px; font-weight: bold; padding: 0 0 8px 0;")
        layout.addWidget(title)

        # Drop zone hint (visible when tree is empty)
        self._drop_hint = QFrame()
        self._drop_hint.setFrameShape(QFrame.Shape.StyledPanel)
        self._drop_hint.setStyleSheet(
            f"QFrame {{ background: {ThemeManager.hex('bg_surface')}; border: 2px dashed {ThemeManager.hex('border')}; "
            "border-radius: 12px; padding: 40px; }"
        )
        hint_layout = QVBoxLayout(self._drop_hint)
        hint_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon_label = QLabel("📂")
        icon_label.setStyleSheet("font-size: 48px; border: none;")
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hint_layout.addWidget(icon_label)
        text_label = QLabel("拖放文件/文件夹到此处，或点击下方按钮添加")
        text_label.setStyleSheet(f"font-size: 14px; color: {ThemeManager.hex('text_muted')}; border: none;")
        text_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hint_layout.addWidget(text_label)
        layout.addWidget(self._drop_hint)

        # Resource tree (file/folder list)
        self.resource_tree = ResourceTree()
        self.resource_tree.file_selected.connect(self.file_selected.emit)
        layout.addWidget(self.resource_tree)

        # Summary label
        self._summary = QLabel("")
        self._summary.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; font-size: 11px; padding: 2px 0;")
        layout.addWidget(self._summary)

        # Action buttons
        btn_layout = QHBoxLayout()

        add_file_btn = QPushButton("+ 添加文件")
        add_file_btn.setStyleSheet(self._btn_style(ThemeManager.hex('accent')))
        add_file_btn.clicked.connect(self.add_files_requested.emit)
        btn_layout.addWidget(add_file_btn)

        add_folder_btn = QPushButton("+ 添加文件夹")
        add_folder_btn.setStyleSheet(self._btn_style(ThemeManager.hex('accent')))
        add_folder_btn.clicked.connect(self.add_folder_requested.emit)
        btn_layout.addWidget(add_folder_btn)

        btn_layout.addSpacing(16)

        self._compress_btn = QPushButton("▶ 压缩")
        self._compress_btn.setStyleSheet(self._btn_style(ThemeManager.hex('success')))
        self._compress_btn.clicked.connect(self.compress_requested.emit)
        btn_layout.addWidget(self._compress_btn)

        clear_btn = QPushButton("清空列表")
        clear_btn.setStyleSheet(self._btn_style(ThemeManager.hex('error')))
        clear_btn.clicked.connect(self.clear_requested.emit)
        btn_layout.addWidget(clear_btn)

        btn_layout.addStretch()
        layout.addLayout(btn_layout)

    @staticmethod
    def _btn_style(color: str) -> str:
        return (
            f"QPushButton {{ background: {color}; color: white; font-weight: bold; "
            "padding: 8px 20px; border-radius: 6px; font-size: 13px; }"
            "QPushButton:hover { opacity: 0.9; }"
        )

    def load_records(self, records: list) -> None:
        self.resource_tree.load_records(records)
        self._update_state(records)

    def _update_state(self, records: list) -> None:
        has_items = bool(records)
        self._drop_hint.setVisible(not has_items)
        self.resource_tree.setVisible(has_items)

        if not has_items:
            self._summary.setText("")
            return

        file_count = 0
        folder_count = 0
        total_size = 0
        for rec in records:
            if isinstance(rec, FolderRecord):
                folder_count += 1
                total_size += rec.size
            elif isinstance(rec, FileRecord):
                file_count += 1
                total_size += rec.size

        parts = []
        if folder_count:
            parts.append(f"{folder_count} 个文件夹")
        if file_count:
            parts.append(f"{file_count} 个文件")
        parts.append(f"总计 {formatted_size(total_size)}")
        self._summary.setText("，".join(parts))

    def get_records(self) -> list:
        return self.resource_tree.get_all_records()

    def add_record(self, rec) ->None:
        records = self.get_records()
        records.append(rec)
        self.load_records(records)
