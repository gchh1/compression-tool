"""File property panel (right sidebar)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QLabel, QScrollArea, QFrame,
)

from gui.core.models import FileRecord, formatted_size


class PropertyPanel(QWidget):
    """Right-side panel: selected file properties and compression results."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._record: FileRecord | None = None
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        header = QLabel("文件属性")
        header.setStyleSheet("font-weight: bold; padding: 4px; font-size: 13px;")
        layout.addWidget(header)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)

        container = QWidget()
        self._form_layout = QVBoxLayout(container)
        self._form_layout.setContentsMargins(0, 0, 0, 0)
        self._form_layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        # Sections
        self._name_label = QLabel("")
        self._name_label.setWordWrap(True)
        self._name_label.setStyleSheet("font-size: 14px; font-weight: bold; padding: 4px;")
        self._form_layout.addWidget(self._name_label)

        self._basic_section = self._add_section("基本信息")
        self._type_label = QLabel("")
        self._size_label = QLabel("")
        self._path_label = QLabel("")
        self._path_label.setWordWrap(True)
        self._basic_section.addWidget(self._type_label)
        self._basic_section.addWidget(self._size_label)
        self._basic_section.addWidget(self._path_label)

        self._compression_section = self._add_section("压缩结果")
        self._status_label = QLabel("未压缩")
        self._algo_label = QLabel("")
        self._ratio_label = QLabel("")
        self._time_label = QLabel("")
        self._blocks_label = QLabel("")
        self._compression_section.addWidget(self._status_label)
        self._compression_section.addWidget(self._algo_label)
        self._compression_section.addWidget(self._ratio_label)
        self._compression_section.addWidget(self._time_label)
        self._compression_section.addWidget(self._blocks_label)

        scroll.setWidget(container)
        layout.addWidget(scroll)

    def _add_section(self, title: str) -> QVBoxLayout:
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet("color: #ddd;")
        self._form_layout.addWidget(sep)

        label = QLabel(title)
        label.setStyleSheet("font-weight: bold; color: #555; padding: 4px 0; font-size: 11px;")
        self._form_layout.addWidget(label)

        section = QVBoxLayout()
        self._form_layout.addLayout(section)
        return section

    def set_record(self, rec: FileRecord | None) -> None:
        self._record = rec
        if rec is None:
            self._name_label.setText("(未选择)")
            self._type_label.setText("")
            self._size_label.setText("")
            self._path_label.setText("")
            self._status_label.setText("")
            self._algo_label.setText("")
            self._ratio_label.setText("")
            self._time_label.setText("")
            self._blocks_label.setText("")
            return

        self._name_label.setText(rec.name)
        self._type_label.setText(f"类型: {rec.type.value}")
        self._size_label.setText(f"大小: {formatted_size(rec.size)}")
        self._path_label.setText(f"路径: {rec.path}")

        # Compression results
        from gui.core.models import CompressionStatus
        if rec.status == CompressionStatus.PENDING:
            self._status_label.setText("状态: ⏳ 等待压缩")
            self._algo_label.setText(f"策略: {rec.algorithm.value}")
            self._ratio_label.setText("")
            self._time_label.setText("")
            self._blocks_label.setText("")
        elif rec.status == CompressionStatus.DONE:
            self._status_label.setText("状态: ✅ 压缩完成")
            self._algo_label.setText(f"策略: {rec.algorithm.value}")
            self._ratio_label.setText(
                f"压缩率: {rec.compression_ratio * 100:.1f}%  "
                f"({formatted_size(rec.size)} → {formatted_size(len(rec.compressed_data or []))})")
            self._time_label.setText(f"耗时: {rec.compression_time_ms:.1f} ms")
            bp = rec.block_profile
            if bp and bp.get('blocks'):
                self._blocks_label.setText(f"Blocks: {len(bp['blocks'])}")
            else:
                self._blocks_label.setText("")
        elif rec.status == CompressionStatus.FAILED:
            self._status_label.setText("状态: ❌ 压缩失败")
            self._algo_label.setText("")
            self._ratio_label.setText(f"错误: {rec.error_message}")
            self._time_label.setText("")
            self._blocks_label.setText("")
