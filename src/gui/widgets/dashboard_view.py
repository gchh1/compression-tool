"""Dashboard overview view — summary cards, type distribution, size charts."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRectF
from PyQt6.QtGui import QPainter, QColor, QPen, QFont, QFontMetrics, QPainterPath
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QScrollArea, QFrame,
)

from gui.core.models import FileRecord, FolderRecord, formatted_size


class _SummaryCard(QFrame):
    """A single metric card."""

    def __init__(self, title: str, value: str, color: str = "#4a90d9", parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.Shape.StyledPanel)
        self.setStyleSheet(
            f"background: white; border: 1px solid #e0e0e0; border-radius: 8px; "
            f"border-left: 4px solid {color}; padding: 12px;"
        )
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        val_label = QLabel(value)
        val_label.setStyleSheet(f"font-size: 22px; font-weight: bold; color: {color}; border: none;")
        layout.addWidget(val_label)
        title_label = QLabel(title)
        title_label.setStyleSheet("font-size: 11px; color: #888; border: none;")
        layout.addWidget(title_label)


class _PieChart(QWidget):
    """Simple pie chart drawn with QPainter."""

    COLORS = [
        QColor("#4a90d9"), QColor("#f5a623"), QColor("#7ed321"),
        QColor("#d0021b"), QColor("#9013fe"), QColor("#50e3c2"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data: list[tuple[str, float]] = []
        self.setMinimumSize(200, 200)

    def set_data(self, data: list[tuple[str, float]]) -> None:
        self._data = data
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        if not self._data:
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "无数据")
            painter.end()
            return

        w = self.width()
        h = self.height()
        total = sum(v for _, v in self._data)
        if total == 0:
            painter.end()
            return

        margin = 20
        legend_w = 120
        diameter = min(w - legend_w - margin * 2, h - margin * 2)
        cx = margin + diameter / 2
        cy = h / 2
        r = diameter / 2

        # Draw slices
        angle = 90 * 16  # Start from top (Qt uses 1/16 degree)
        legend_font = QFont("Sans", 10)
        painter.setFont(legend_font)
        fm = QFontMetrics(legend_font)

        for i, (label, value) in enumerate(self._data):
            if value <= 0:
                continue
            span = int(value / total * 360 * 16)
            color = self.COLORS[i % len(self.COLORS)]

            painter.setBrush(color)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawPie(QRectF(cx - r, cy - r, r * 2, r * 2), angle, -span)

            # Legend
            lx = int(cx + r + 16)
            ly = int(cy - len(self._data) * 12 + i * 22)
            painter.fillRect(QRectF(lx, ly, 12, 12), color)
            painter.setPen(QColor("#333"))
            pct = value / total * 100
            painter.drawText(int(lx + 18), int(ly + fm.ascent() - 2),
                             f"{label} ({pct:.0f}%)")

            angle -= span

        painter.end()


class _BarChart(QWidget):
    """Simple horizontal bar chart."""

    COLORS = [
        QColor("#4a90d9"), QColor("#f5a623"), QColor("#7ed321"),
        QColor("#d0021b"), QColor("#9013fe"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data: list[tuple[str, float]] = []
        self.setMinimumSize(200, 150)

    def set_data(self, data: list[tuple[str, float]]) -> None:
        self._data = data
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        if not self._data:
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "无数据")
            painter.end()
            return

        w = self.width()
        h = self.height()
        max_val = max(v for _, v in self._data) or 1

        font = QFont("Sans", 10)
        painter.setFont(font)
        fm = QFontMetrics(font)
        bar_h = min(24, (h - 30) // len(self._data) - 4)
        label_w = 100
        bar_area_w = w - label_w - 80

        for i, (label, value) in enumerate(self._data):
            y = 20 + i * (bar_h + 6)
            bar_w = int(value / max_val * bar_area_w) if max_val > 0 else 0

            painter.setPen(QColor("#555"))
            painter.drawText(0, y + fm.ascent(), label)

            color = self.COLORS[i % len(self.COLORS)]
            painter.setBrush(color)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(QRectF(label_w, y, max(bar_w, 2), bar_h), 3, 3)

            painter.setPen(QColor("#333"))
            painter.drawText(int(label_w + bar_w + 6), int(y + fm.ascent()),
                             formatted_size(int(value)))

        painter.end()


class DashboardView(QScrollArea):
    """Overview dashboard with summary cards and charts."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWidgetResizable(True)
        self.setFrameShape(QFrame.Shape.NoFrame)

        container = QWidget()
        self._layout = QVBoxLayout(container)
        self._layout.setContentsMargins(16, 16, 16, 16)
        self._layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        # Title
        title = QLabel("项目概览")
        title.setStyleSheet("font-size: 18px; font-weight: bold; padding: 0 0 8px 0;")
        self._layout.addWidget(title)

        # Cards row
        cards_layout = QHBoxLayout()
        self._file_count_card = _SummaryCard("文件总数", "—", "#4a90d9")
        self._total_size_card = _SummaryCard("原始总大小", "—", "#f5a623")
        self._est_ratio_card = _SummaryCard("预估压缩率", "—", "#7ed321")
        cards_layout.addWidget(self._file_count_card)
        cards_layout.addWidget(self._total_size_card)
        cards_layout.addWidget(self._est_ratio_card)
        self._layout.addLayout(cards_layout)

        # Charts row
        charts_layout = QHBoxLayout()

        # Pie chart
        pie_container = QFrame()
        pie_container.setFrameShape(QFrame.Shape.StyledPanel)
        pie_container.setStyleSheet("background: white; border: 1px solid #e0e0e0; border-radius: 8px;")
        pie_layout = QVBoxLayout(pie_container)
        pie_title = QLabel("资源类型分布")
        pie_title.setStyleSheet("font-weight: bold; padding: 8px; border: none; font-size: 13px;")
        pie_layout.addWidget(pie_title)
        self._pie_chart = _PieChart()
        pie_layout.addWidget(self._pie_chart)
        charts_layout.addWidget(pie_container)

        # Bar chart
        bar_container = QFrame()
        bar_container.setFrameShape(QFrame.Shape.StyledPanel)
        bar_container.setStyleSheet("background: white; border: 1px solid #e0e0e0; border-radius: 8px;")
        bar_layout = QVBoxLayout(bar_container)
        bar_title = QLabel("目录大小分布")
        bar_title.setStyleSheet("font-weight: bold; padding: 8px; border: none; font-size: 13px;")
        bar_layout.addWidget(bar_title)
        self._bar_chart = _BarChart()
        bar_layout.addWidget(self._bar_chart)
        charts_layout.addWidget(bar_container)

        self._layout.addLayout(charts_layout)
        self.setWidget(container)

    def update_from_records(self, records: list) -> None:
        """Update dashboard with current records."""
        all_files: list[FileRecord] = []
        for rec in records:
            if isinstance(rec, FolderRecord):
                all_files.extend(rec.files)
            elif isinstance(rec, FileRecord):
                all_files.append(rec)

        total = len(all_files)
        total_size = sum(f.size for f in all_files)

        # Update cards via stored references
        self._file_count_card.findChildren(QLabel)[0].setText(str(total))
        self._total_size_card.findChildren(QLabel)[0].setText(formatted_size(total_size))
        if total_size > 0:
            text_size = sum(f.size for f in all_files if f.type.value in ('script', 'text'))
            est = 25 + (total_size - text_size) / max(total_size, 1) * 75
            self._est_ratio_card.findChildren(QLabel)[0].setText(f"~{est:.0f}%")
        else:
            self._est_ratio_card.findChildren(QLabel)[0].setText("—")

        # Type distribution
        type_sizes: dict[str, int] = {}
        for f in all_files:
            t = f.type.value
            type_sizes[t] = type_sizes.get(t, 0) + f.size
        self._pie_chart.set_data(sorted(type_sizes.items(), key=lambda x: -x[1]))

        # Directory size distribution
        dir_sizes: dict[str, int] = {}
        for f in all_files:
            parent = str(f.path.parent) if hasattr(f.path, 'parent') else "/"
            dir_sizes[parent] = dir_sizes.get(parent, 0) + f.size
        self._bar_chart.set_data(sorted(dir_sizes.items(), key=lambda x: -x[1])[:8])
