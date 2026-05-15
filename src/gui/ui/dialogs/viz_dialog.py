"""Dialog for viewing LZ77 compression visualization from .viz files.

v2 files use lazy page-based loading via mmap — only the visible window
of match events is held in memory.  v1 files fall back to full load.

Includes sliding-window canvas + Huffman code-length chart + block stats.
"""

from __future__ import annotations

import logging

from PyQt6.QtCore import Qt, QRect
from PyQt6.QtGui import QPainter, QColor, QFont
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QSlider, QPushButton,
    QSplitter, QTextEdit, QGroupBox, QScrollBar, QWidget,
)

from gui.engine.viz_loader import VizLoader, HuffmanTreeBuilt, MatchEvent, BlockBoundary, DPStateEvent
from gui.ui.widgets.window_canvas import WindowCanvas

logger = logging.getLogger(__name__)

PAGE_SIZE = 100_000  # match events per page (~900 KB)

# ── Huffman code-length bar-chart widget ──────────────────────────

HUFF_COLORS = [
    QColor(59, 130, 246),   # blue   — type 0 (lit/len)
    QColor(34, 197, 94),    # green  — type 1 (dist)
    QColor(250, 204, 21),   # yellow — type 2 (Brotli lit0/lit1)
    QColor(239, 68, 68),    # red    — type 3 (Brotli len/dist)
]
HUFF_TYPE_LABELS = {0: "Lit/Len", 1: "Dist", 2: "Lit", 3: "Len/Dist"}


class HuffmanBarWidget(QWidget):
    """Vertical bar chart of Huffman code lengths per symbol."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._trees: list[HuffmanTreeBuilt] = []
        self.setMinimumHeight(150)
        self.setMinimumWidth(300)

    def set_trees(self, trees: list[HuffmanTreeBuilt]) -> None:
        self._trees = trees
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(30, 41, 59))
        if not self._trees:
            painter.setPen(QColor(148, 163, 184))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "(无 Huffman 树数据)")
            return

        w = self.width()
        h = self.height()
        margin = 40
        bottom = h - 8

        # Group trees by tree_type
        by_type: dict[int, list[HuffmanTreeBuilt]] = {}
        for t in self._trees:
            by_type.setdefault(t.tree_type, []).append(t)

        # Use the first tree of each type for display
        for tree_type, trees in by_type.items():
            t = trees[0]
            if t.alphabet_size == 0:
                continue

            max_len = max(t.code_lengths[:t.alphabet_size]) if t.alphabet_size > 0 else 1
            if max_len == 0:
                continue

            bar_w = max(1, (w - margin * 2) // t.alphabet_size)
            color = HUFF_COLORS[tree_type % len(HUFF_COLORS)]

            for sym in range(t.alphabet_size):
                cl = t.code_lengths[sym]
                if cl == 0:
                    continue
                x = margin + sym * bar_w
                bar_h = int((cl / max_len) * (bottom - margin))
                painter.fillRect(QRect(x, bottom - bar_h, max(1, bar_w - 1), bar_h), color)

        # Legend
        painter.setFont(QFont("Consolas", 8))
        ly = 8
        for tree_type, trees in sorted(by_type.items()):
            label = HUFF_TYPE_LABELS.get(tree_type, f"Type {tree_type}")
            color = HUFF_COLORS[tree_type % len(HUFF_COLORS)]
            painter.setPen(color)
            painter.drawText(margin, ly, f"■ {label} ({len(trees)}块)")
            ly += 14


# ── VizDialog ─────────────────────────────────────────────────────

class VizDialog(QDialog):
    """Visualization dialog for LZ77 compression trace data."""

    def __init__(self, viz_path: str, source_path: str = "", parent=None) -> None:
        super().__init__(parent)
        self._viz_path = viz_path
        self._source_path = source_path
        self._loader: VizLoader | None = None
        self._match_events: list[MatchEvent] = []   # current page
        self._page_start: int = 0
        self._blocks: list[BlockBoundary] = []
        self._dp_states: list[DPStateEvent] = []
        self._huffman_trees: list[HuffmanTreeBuilt] = []

        self.setWindowTitle(f"压缩可视化 — {viz_path}")
        self.resize(1200, 750)
        self.setMinimumSize(900, 550)

        self._setup_ui()
        self._load_data()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        # Top bar: info + controls
        top = QHBoxLayout()
        self._info_label = QLabel("加载中...")
        top.addWidget(self._info_label)
        top.addStretch()

        prev_btn = QPushButton("◀ 上一步")
        prev_btn.clicked.connect(self._prev_step)
        top.addWidget(prev_btn)

        self._step_slider = QSlider(Qt.Orientation.Horizontal)
        self._step_slider.setMinimum(0)
        self._step_slider.setMaximum(0)
        self._step_slider.valueChanged.connect(self._on_slider_changed)
        top.addWidget(self._step_slider)

        next_btn = QPushButton("下一步 ▶")
        next_btn.clicked.connect(self._next_step)
        top.addWidget(next_btn)

        self._step_label = QLabel("0/0")
        top.addWidget(self._step_label)
        layout.addLayout(top)

        # Main area: canvas + sidebar
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # Left: WindowCanvas
        self._canvas = WindowCanvas(window_size=32768, lookahead_size=258)
        self._scrollbar = QScrollBar(Qt.Orientation.Vertical)
        self._canvas.set_scrollbar(self._scrollbar)
        canvas_layout = QHBoxLayout()
        canvas_layout.setContentsMargins(0, 0, 0, 0)
        canvas_layout.addWidget(self._canvas)
        canvas_layout.addWidget(self._scrollbar)
        canvas_wrapper = QGroupBox("滑动窗口")
        canvas_wrapper.setLayout(canvas_layout)
        splitter.addWidget(canvas_wrapper)

        # Right: detail + Huffman + blocks
        right = QVBoxLayout()

        self._detail_text = QTextEdit()
        self._detail_text.setReadOnly(True)
        self._detail_text.setMaximumWidth(400)
        right.addWidget(QLabel("事件详情"))
        right.addWidget(self._detail_text)

        huff_group = QGroupBox("Huffman 编码长度分布")
        self._huffman_widget = HuffmanBarWidget()
        huff_layout = QVBoxLayout(huff_group)
        huff_layout.addWidget(self._huffman_widget)
        right.addWidget(huff_group)

        block_group = QGroupBox("块统计")
        self._block_text = QTextEdit()
        self._block_text.setReadOnly(True)
        self._block_text.setMaximumWidth(400)
        self._block_text.setMaximumHeight(180)
        block_layout = QVBoxLayout(block_group)
        block_layout.addWidget(self._block_text)
        right.addWidget(block_group)

        right_widget = QGroupBox()
        right_widget.setLayout(right)
        splitter.addWidget(right_widget)
        splitter.setSizes([700, 500])

        layout.addWidget(splitter)

    # ── loading ────────────────────────────────────────────────────

    def _load_data(self) -> None:
        try:
            self._loader = VizLoader(self._viz_path)
            self._blocks = self._loader.load_block_boundaries()
            self._huffman_trees = self._loader.load_huffman_trees()

            try:
                self._dp_states = self._loader.load_dp_states()
            except Exception:
                self._dp_states = []

            match_total = self._loader.match_count
            self._step_slider.setMaximum(max(0, match_total - 1))
            self._step_label.setText(f"0/{match_total}")

            info = (
                f"事件: {self._loader.total_events}  "
                f"匹配: {match_total}  "
                f"块: {len(self._blocks)}  "
                f"v{self._loader.version}"
            )
            if self._dp_states:
                info += f"  DP状态: {len(self._dp_states)}"
            self._info_label.setText(info)

            self._update_block_summary()
            self._update_huffman_chart()

            if match_total > 0:
                self._load_page(0)
                self._canvas.set_step(0)
                self._show_event_detail(0)

        except Exception as e:
            logger.exception("Failed to load viz file")
            self._info_label.setText(f"加载失败: {e}")

    def _load_page(self, start_idx: int) -> None:
        if self._loader is None:
            return

        if self._loader.version >= 2:
            self._match_events = self._loader.get_match_page(start_idx, PAGE_SIZE * 9)
        else:
            if not self._match_events:
                self._match_events = self._loader.load_match_events()
            return

        self._page_start = start_idx

        total_bytes = max(
            (e.input_pos + max(e.length, 1) for e in self._match_events),
            default=0,
        )
        # Use source_path if available so canvas can show real byte values
        source = self._source_path
        self._canvas.set_events_page(
            start_idx, self._match_events,
            self._loader.match_count, total_bytes,
            source_path=source,
        )

    def _ensure_page(self, idx: int) -> None:
        if self._loader is None or self._loader.version < 2:
            return
        if not self._match_events:
            self._load_page(idx)
            return
        if self._page_start <= idx < self._page_start + len(self._match_events):
            return
        self._load_page(idx)

    # ── Huffman chart ──────────────────────────────────────────────

    def _update_huffman_chart(self) -> None:
        self._huffman_widget.set_trees(self._huffman_trees)

    # ── navigation ──────────────────────────────────────────────────

    def _prev_step(self) -> None:
        total = self._loader.match_count if self._loader else 0
        if total == 0:
            return
        cur = self._step_slider.value()
        if cur > 0:
            self._step_slider.setValue(cur - 1)

    def _next_step(self) -> None:
        total = self._loader.match_count if self._loader else 0
        if total == 0:
            return
        cur = self._step_slider.value()
        if cur < total - 1:
            self._step_slider.setValue(cur + 1)

    def _on_slider_changed(self, value: int) -> None:
        total = self._loader.match_count if self._loader else 0
        self._step_label.setText(f"{value}/{total}")
        self._ensure_page(value)
        self._canvas.set_step(value)
        self._show_event_detail(value)

    def _show_event_detail(self, idx: int) -> None:
        local_idx = idx - self._page_start
        if not (0 <= local_idx < len(self._match_events)):
            return
        ev = self._match_events[local_idx]
        if ev.offset > 0:
            text = (
                f"类型: 匹配\n"
                f"位置: 0x{ev.input_pos:08X} ({ev.input_pos})\n"
                f"距离: {ev.offset} bytes back\n"
                f"长度: {ev.length} bytes\n"
                f"匹配区间: [{ev.input_pos}, {ev.input_pos + ev.length})\n"
                f"引用区间: [{ev.input_pos - ev.offset}, {ev.input_pos - ev.offset + ev.length})"
            )
        else:
            b = ev.literal
            ch = chr(b) if 32 <= b < 127 else f"\\x{b:02X}"
            text = (
                f"类型: 字面量\n"
                f"位置: 0x{ev.input_pos:08X} ({ev.input_pos})\n"
                f"字节: 0x{b:02X} ({ch})"
            )
        self._detail_text.setPlainText(text)

    def _update_block_summary(self) -> None:
        if not self._blocks:
            self._block_text.setPlainText("(无块信息)")
            return
        lines = []
        for b in self._blocks:
            ratio = (
                f"{b.output_bytes / max(b.input_bytes, 1) * 100:.1f}%"
                if b.input_bytes > 0
                else "—"
            )
            lines.append(
                f"块 {b.block_index}: "
                f"输入 {b.input_bytes}B → 输出 {b.output_bytes}B "
                f"({ratio})\n"
                f"  字面量 {b.literal_count} / 匹配 {b.match_count}"
            )
        self._block_text.setPlainText("\n".join(lines))
