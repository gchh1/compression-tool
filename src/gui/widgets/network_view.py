"""Network transmission simulation view."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRectF
from PyQt6.QtGui import QPainter, QColor, QPen, QFont, QFontMetrics
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox, QTableWidget,
    QTableWidgetItem, QHeaderView, QFrame,
)

from gui.core.models import NETWORK_PROFILES, NetworkProfile, formatted_size, FileRecord, FolderRecord
from gui.core.theme import ThemeManager


class _TransferBarChart(QWidget):
    """Before/after bar chart for transfer time comparison."""

    COLORS_BEFORE = ThemeManager.color('error')
    COLORS_AFTER = ThemeManager.color('success')

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data: list[tuple[str, float, float]] = []  # (name, before, after)
        self.setMinimumHeight(180)

    def set_data(self, data: list[tuple[str, float, float]]) -> None:
        self._data = data
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        if not self._data:
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                             "无传输数据 (请先压缩文件)")
            painter.end()
            return

        w = self.width()
        h = self.height()
        max_val = max(max(b, a) for _, b, a in self._data) or 0.001

        font = QFont("Sans", 10)
        painter.setFont(font)
        fm = QFontMetrics(font)
        n = len(self._data)
        group_w = (w - 60) // n
        bar_w = group_w // 3

        # Legend
        painter.fillRect(QRectF(w - 180, 8, 14, 14), self.COLORS_BEFORE)
        painter.setPen(QColor("#333"))
        painter.drawText(w - 162, 8 + fm.ascent(), "压缩前")
        painter.fillRect(QRectF(w - 90, 8, 14, 14), self.COLORS_AFTER)
        painter.drawText(w - 72, 8 + fm.ascent(), "压缩后")

        for i, (name, before, after) in enumerate(self._data):
            x = 40 + i * group_w
            bh = before / max_val * (h - 55)
            ah = after / max_val * (h - 55)

            # Before bar
            painter.fillRect(QRectF(x + 2, h - 35 - bh, bar_w, bh), self.COLORS_BEFORE)
            # After bar
            painter.fillRect(QRectF(x + bar_w + 4, h - 35 - ah, bar_w, ah), self.COLORS_AFTER)

            # Label
            painter.setPen(QColor("#555"))
            painter.drawText(QRectF(x, h - 35, group_w - 4, 30),
                             Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop,
                             name)

        painter.end()


class NetworkView(QWidget):
    """View 5: Network transfer simulation."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._records: list = []
        self._total_original: int = 0
        self._total_compressed: int = 0
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)

        title = QLabel("网络传输模拟")
        title.setStyleSheet("font-size: 18px; font-weight: bold; padding: 0 0 8px 0;")
        layout.addWidget(title)

        desc = QLabel("模拟不同网络环境下，压缩对传输时间的提升效果")
        desc.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; padding-bottom: 12px;")
        layout.addWidget(desc)

        # Results table
        self._table = QTableWidget()
        self._table.setColumnCount(4)
        self._table.setHorizontalHeaderLabels(["网络环境", "压缩前传输", "压缩后传输", "节省时间"])
        self._table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self._table.setColumnWidth(1, 120)
        self._table.setColumnWidth(2, 120)
        self._table.setColumnWidth(3, 120)
        self._table.setAlternatingRowColors(True)
        layout.addWidget(self._table)

        # Bar chart
        self._bar_chart = _TransferBarChart()
        layout.addWidget(self._bar_chart)

    def update_from_records(self, records: list) -> None:
        """Calculate transfer times after compression."""
        self._records = records

        all_files: list[FileRecord] = []
        for rec in records:
            if isinstance(rec, FolderRecord):
                all_files.extend(rec.files)
            else:
                all_files.append(rec)

        self._total_original = sum(f.size for f in all_files)
        self._total_compressed = sum(
            len(f.compressed_data) if f.compressed_data else f.size
            for f in all_files
        )

        self._table.setRowCount(len(NETWORK_PROFILES))
        chart_data = []
        row = 0
        for name, profile in sorted(NETWORK_PROFILES.items()):
            before = profile.transfer_time(self._total_original)
            after = profile.transfer_time(self._total_compressed)
            saved = before - after

            self._table.setItem(row, 0, QTableWidgetItem(f"{profile.name} ({name})"))
            self._table.setItem(row, 1, QTableWidgetItem(f"{before:.2f}s"))
            self._table.setItem(row, 2, QTableWidgetItem(f"{after:.2f}s"))
            saved_item = QTableWidgetItem(f"{saved:.2f}s ({saved / max(before, 0.001) * 100:.0f}%)")
            saved_item.setForeground(Qt.GlobalColor.darkGreen if saved > 0 else Qt.GlobalColor.red)
            self._table.setItem(row, 3, saved_item)
            chart_data.append((name, before, after))
            row += 1

        self._bar_chart.set_data(chart_data)
