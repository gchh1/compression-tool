"""Block-level compression profiling bar chart."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRectF, pyqtSignal
from PyQt6.QtGui import QColor, QPainter, QPen, QFont, QFontMetrics
from PyQt6.QtWidgets import QWidget, QScrollArea, QVBoxLayout, QLabel


class BlockProfilerWidget(QWidget):
    """Horizontal bar chart showing per-block compression ratio.

    Each block is one bar:
    - Height proportional to compressed_bits / (output_bytes * 8)
    - Green when ratio < 1.0 (compression), red when > 1.0 (expansion)
    - Hover shows tooltip with block details.
    - Click emits block_selected(index).
    """

    block_selected = pyqtSignal(int)

    BAR_WIDTH = 12
    BAR_GAP = 2
    TOP_MARGIN = 30
    BOTTOM_MARGIN = 20
    LEFT_MARGIN = 60
    RIGHT_MARGIN = 20

    def __init__(self, parent=None):
        super().__init__(parent)
        self._blocks: list[dict] = []
        self._hovered_idx: int = -1
        self._max_value: float = 1.0
        self.setMouseTracking(True)
        self.setMinimumHeight(200)

    def set_blocks(self, blocks: list[dict]) -> None:
        self._blocks = blocks
        if not blocks:
            self._max_value = 1.0
        else:
            values = []
            for b in blocks:
                raw_bits = b.get('output_bytes', 1) * 8
                compressed_bits = b.get('ll_tree_bits', 0) + b.get('dist_tree_bits', 0)
                # Add encoded token bits estimate
                for t in range(b.get('literal_count', 0)):
                    compressed_bits += 8
                for _ in range(b.get('match_count', 0)):
                    compressed_bits += 16
                values.append(compressed_bits / max(raw_bits, 1))
            self._max_value = max(max(values), 1.0) * 1.1
        self._hovered_idx = -1
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        w = self.width()
        h = self.height()
        chart_h = h - self.TOP_MARGIN - self.BOTTOM_MARGIN

        if not self._blocks:
            painter.drawText(QRectF(0, 0, w, h), Qt.AlignmentFlag.AlignCenter,
                             "No block profile data")
            painter.end()
            return

        # Background
        painter.fillRect(self.rect(), QColor(245, 245, 245))

        # Axes
        axis_pen = QPen(QColor(180, 180, 180), 1)
        painter.setPen(axis_pen)
        baseline = h - self.BOTTOM_MARGIN
        painter.drawLine(int(self.LEFT_MARGIN), int(baseline),
                         int(w - self.RIGHT_MARGIN), int(baseline))

        # Y-axis labels (ratio)
        font = QFont("Monospace", 8)
        painter.setFont(font)
        fm = QFontMetrics(font)

        # 1.0 line
        y1 = baseline - (1.0 / self._max_value) * chart_h
        dash_pen = QPen(QColor(200, 200, 200), 1, Qt.PenStyle.DashLine)
        painter.setPen(dash_pen)
        painter.drawLine(int(self.LEFT_MARGIN), int(y1),
                         int(w - self.RIGHT_MARGIN), int(y1))
        painter.setPen(QColor(120, 120, 120))
        painter.drawText(int(self.LEFT_MARGIN - 4), int(y1 + fm.ascent() / 2),
                         "1.0")

        # 0.5 line if visible
        y05 = baseline - (0.5 / self._max_value) * chart_h
        if y05 > self.TOP_MARGIN:
            painter.setPen(dash_pen)
            painter.drawLine(int(self.LEFT_MARGIN), int(y05),
                             int(w - self.RIGHT_MARGIN), int(y05))
            painter.setPen(QColor(120, 120, 120))
            painter.drawText(int(self.LEFT_MARGIN - 4), int(y05 + fm.ascent() / 2),
                             "0.5")

        # Bars
        bar_area_w = w - self.LEFT_MARGIN - self.RIGHT_MARGIN
        total_bar_w = len(self._blocks) * (self.BAR_WIDTH + self.BAR_GAP) - self.BAR_GAP
        start_x = self.LEFT_MARGIN + max(0, (bar_area_w - total_bar_w) / 2)

        for i, b in enumerate(self._blocks):
            x = start_x + i * (self.BAR_WIDTH + self.BAR_GAP)
            if x + self.BAR_WIDTH > w - self.RIGHT_MARGIN:
                break

            raw_bits = b.get('output_bytes', 1) * 8
            compressed_bits = b.get('ll_tree_bits', 0) + b.get('dist_tree_bits', 0)
            for _ in range(b.get('literal_count', 0)):
                compressed_bits += 8
            for _ in range(b.get('match_count', 0)):
                compressed_bits += 16
            ratio = compressed_bits / max(raw_bits, 1)
            bar_h = max(2, (ratio / self._max_value) * chart_h)
            bar_y = baseline - bar_h

            # Color: green if ratio < 1.0, red if > 1.0
            if ratio <= 1.0:
                g = int(180 + 75 * (1 - ratio))
                color = QColor(50, g, 50)
            else:
                r = int(min(255, 180 + 75 * (ratio - 1)))
                color = QColor(r, 50, 50)

            if i == self._hovered_idx:
                color = color.lighter(130)

            painter.fillRect(QRectF(x, bar_y, self.BAR_WIDTH, bar_h), color)

            # If few blocks, draw index labels
            if len(self._blocks) <= 50:
                painter.setPen(QColor(100, 100, 100))
                painter.drawText(
                    QRectF(x - 4, baseline + 2, self.BAR_WIDTH + 8, 14),
                    Qt.AlignmentFlag.AlignCenter,
                    str(i))

        painter.end()

    def _bar_at(self, x: float) -> int:
        if not self._blocks:
            return -1
        bar_area_w = self.width() - self.LEFT_MARGIN - self.RIGHT_MARGIN
        total_bar_w = len(self._blocks) * (self.BAR_WIDTH + self.BAR_GAP) - self.BAR_GAP
        start_x = self.LEFT_MARGIN + max(0, (bar_area_w - total_bar_w) / 2)
        idx = int((x - start_x) / (self.BAR_WIDTH + self.BAR_GAP))
        bar_x = start_x + idx * (self.BAR_WIDTH + self.BAR_GAP)
        if bar_x <= x < bar_x + self.BAR_WIDTH and 0 <= idx < len(self._blocks):
            return idx
        return -1

    def mouseMoveEvent(self, event) -> None:
        idx = self._bar_at(event.position().x())
        if idx != self._hovered_idx:
            self._hovered_idx = idx
            self.update()
        if idx >= 0:
            b = self._blocks[idx]
            raw_bits = b.get('output_bytes', 1) * 8
            compressed_bits = b.get('ll_tree_bits', 0) + b.get('dist_tree_bits', 0)
            for _ in range(b.get('literal_count', 0)):
                compressed_bits += 8
            for _ in range(b.get('match_count', 0)):
                compressed_bits += 16
            ratio = compressed_bits / max(raw_bits, 1)
            self.setToolTip(
                f"Block #{b['block_index']}\n"
                f"Literals: {b['literal_count']}  Matches: {b['match_count']}\n"
                f"LL tree: {b['ll_tree_bits']} bits  "
                f"Dist tree: {b['dist_tree_bits']} bits\n"
                f"Output bytes: {b['output_bytes']}  "
                f"Ratio: {ratio:.3f}"
            )
        else:
            self.setToolTip("")

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            idx = self._bar_at(event.position().x())
            if idx >= 0:
                self.block_selected.emit(idx)

    def leaveEvent(self, event) -> None:
        self._hovered_idx = -1
        self.update()


class BlockProfilerPanel(QWidget):
    """Panel wrapping the BlockProfilerWidget with a header."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._label = QLabel("块级压缩剖析")
        self._label.setStyleSheet("font-weight: bold; padding: 4px;")
        layout.addWidget(self._label)

        self._chart = BlockProfilerWidget()
        layout.addWidget(self._chart)

    def set_blocks(self, blocks: list[dict]) -> None:
        self._chart.set_blocks(blocks)
        self._label.setText(f"块级压缩剖析 ({len(blocks)} blocks)")

    @property
    def block_selected(self):
        return self._chart.block_selected
