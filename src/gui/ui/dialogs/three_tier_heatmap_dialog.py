"""3-Tier compression heatmap dialog: file bar → chunk entropy strip → content-level viz.

Tier 1 – File-level horizontal bar (scrollable, entropy-colored segments).
Tier 2 – Chunk-level entropy strip (one bar per actual compression chunk, draggable slider).
Tier 3 – Content-level LZ77 sliding window (.viz) or byte-level heatmap (fallback).

Uses HeatmapController (mmap-based .heat reader) — never loads full files into memory.
"""

from __future__ import annotations

import logging
import struct
from pathlib import Path
from typing import TYPE_CHECKING

from PyQt6.QtCore import Qt, pyqtSignal, QRectF
from PyQt6.QtGui import (
    QPainter, QColor, QFont, QFontMetrics, QPen, QBrush,
    QMouseEvent,
)
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QWidget, QScrollArea,
)

from gui.config.theme import ThemeManager

if TYPE_CHECKING:
    from gui.engine.heatmap_controller import HeatmapController

logger = logging.getLogger(__name__)

# ══════════════════════════════════════════════════════════════════════
# §1  Absolute entropy → color  (piecewise HSL, [0.0, 8.0])
#     5.15 → H≈108° (firm green),  7.8 → H≈14° (deep red)
# ══════════════════════════════════════════════════════════════════════

def _entropy_to_color(entropy: float) -> QColor:
    e = max(0.0, min(8.0, entropy))
    if e <= 2.0:
        t = e / 2.0
        return QColor.fromHslF((230.0 - t * 30.0) / 360.0,
                               0.75 + t * 0.15,
                               0.22 + t * 0.28)
    if e <= 4.0:
        t = (e - 2.0) / 2.0
        return QColor.fromHslF((200.0 - t * 80.0) / 360.0,
                               0.72 - t * 0.06,
                               0.48 - t * 0.04)
    if e <= 6.0:
        t = (e - 4.0) / 2.0
        return QColor.fromHslF((120.0 - t * 20.0) / 360.0,
                               0.66 + t * 0.15,
                               0.44 + t * 0.06)
    if e <= 7.5:
        t = (e - 6.0) / 1.5
        return QColor.fromHslF((100.0 - t * 65.0) / 360.0,
                               0.81 + t * 0.14,
                               0.50 - t * 0.02)
    t = (e - 7.5) / 0.5
    return QColor.fromHslF((35.0 - t * 35.0) / 360.0,
                           0.95 - t * 0.05,
                           0.48 - t * 0.12)




# ══════════════════════════════════════════════════════════════════════
# §3  Tier 1 — Global File Bar
# ══════════════════════════════════════════════════════════════════════

class Tier1GlobalBar(QWidget):
    """Horizontal bar showing each file as a proportionally-sized colored segment."""

    file_selected = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._files: list[dict] = []
        self._total_bytes: int = 0
        self._selected_idx: int = -1
        self._hover_idx: int = -1
        self.setMouseTracking(True)
        self.setMinimumHeight(46)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def set_data(self, files: list[dict], total_bytes: int) -> None:
        self._files = files
        self._total_bytes = total_bytes
        if files and self._selected_idx < 0:
            self._selected_idx = 0
        # Compute minimum width so horizontal scrollbar appears when needed
        if files and total_bytes > 0:
            min_seg = 3.0
            base_w = max(300, len(files) * 40)
            self._min_width = int(sum(
                max(min_seg, (f["input_bytes"] / total_bytes) * base_w)
                for f in files
            ))
            self.setMinimumWidth(self._min_width)
        else:
            self.setMinimumWidth(300)
        self.setMinimumHeight(50)
        self.update()

    def select_index(self, idx: int) -> None:
        if 0 <= idx < len(self._files):
            self._selected_idx = idx
            self.update()

    def _segment_at(self, x: int) -> int:
        if not self._files or self._total_bytes <= 0:
            return -1
        frac = x / self.width()
        cursor = int(frac * self._total_bytes)
        pos = 0
        for i, f in enumerate(self._files):
            pos += f["input_bytes"]
            if cursor < pos:
                return i
        return len(self._files) - 1

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        t = ThemeManager.get()

        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(t.bg_surface))
        painter.drawRoundedRect(QRectF(0, 0, w, h), 8, 8)

        if not self._files or self._total_bytes <= 0:
            painter.setPen(QColor(t.text_muted))
            fnt = painter.font()
            fnt.setPointSize(11)
            painter.setFont(fnt)
            painter.drawText(QRectF(0, 0, w, h), Qt.AlignmentFlag.AlignCenter, "(无文件数据)")
            return

        min_seg = 3.0
        x = 0.0
        for i, f in enumerate(self._files):
            seg_w = max(min_seg, (f["input_bytes"] / self._total_bytes) * w)
            arr = f.get("entropy_array", [])
            avg = sum(arr) / max(len(arr), 1) if arr else 0.0
            color = _entropy_to_color(avg)

            if i == self._selected_idx:
                painter.setBrush(QBrush(color.lighter(130)))
                painter.setPen(QPen(QColor(251, 191, 36), 2.5))
                painter.drawRoundedRect(QRectF(x + 1, 2, seg_w - 1, h - 4), 6, 6)
            elif i == self._hover_idx:
                painter.setBrush(QBrush(color.lighter(115)))
                painter.setPen(Qt.PenStyle.NoPen)
                painter.drawRoundedRect(QRectF(x, 1, seg_w, h - 2), 6, 6)
            else:
                painter.setBrush(QBrush(color))
                painter.setPen(Qt.PenStyle.NoPen)
                painter.drawRoundedRect(QRectF(x, 1, seg_w, h - 2), 6, 6)

            if seg_w > 60:
                painter.setPen(QColor(255, 255, 255, 220))
                fnt = QFont(painter.font())
                fnt.setPointSize(9)
                fnt.setBold(i == self._selected_idx)
                painter.setFont(fnt)
                label = f["name"]
                fm = QFontMetrics(fnt)
                if fm.horizontalAdvance(label) > seg_w - 8:
                    label = fm.elidedText(label, Qt.TextElideMode.ElideRight, int(seg_w) - 8)
                painter.drawText(QRectF(x + 4, 0, seg_w - 8, h), Qt.AlignmentFlag.AlignVCenter, label)

            x += seg_w

    def mouseMoveEvent(self, event: QMouseEvent):
        idx = self._segment_at(int(event.position().x()))
        if idx != self._hover_idx:
            self._hover_idx = idx
            self.update()

    def mousePressEvent(self, event: QMouseEvent):
        idx = self._segment_at(int(event.position().x()))
        if idx >= 0:
            self._selected_idx = idx
            self.file_selected.emit(idx)
            self.update()

    def leaveEvent(self, event):
        self._hover_idx = -1
        self.update()


# ══════════════════════════════════════════════════════════════════════
# §4  Tier 2 — Per-File Entropy Strip (with adaptive chunking)
# ══════════════════════════════════════════════════════════════════════

class Tier2EntropyStrip(QWidget):
    """Entropy strip: one colored bar per actual compression chunk + draggable slider."""

    slider_offset_changed = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._display_chunks: list[float] = []
        self._chunk_bytes: int = 0
        self._global_start: int = 0
        self._input_bytes: int = 0
        self._file_name: str = ""
        self._slider_frac: float = 0.0
        self._dragging: bool = False
        self._hover_chunk: int = -1
        self._min_bar_w: int = 3  # px per chunk minimum
        self.setMouseTracking(True)
        self.setMinimumHeight(105)

    def set_file(self, file_entry: dict, chunk_bytes: int) -> None:
        raw = file_entry.get("entropy_array", [])
        self._display_chunks = list(raw)
        self._chunk_bytes = chunk_bytes
        self._global_start = file_entry.get("global_start", 0)
        self._input_bytes = file_entry.get("input_bytes", 0)
        self._file_name = file_entry.get("name", "")
        self._slider_frac = 0.0
        self._hover_chunk = -1
        # Size hint: at least min_bar_w px per chunk
        n = len(self._display_chunks)
        self.setMinimumWidth(max(300, n * self._min_bar_w))
        self.update()

    def set_slider_to_offset(self, global_offset: int) -> None:
        if self._input_bytes > 0:
            local = max(0, min(global_offset - self._global_start, self._input_bytes))
            self._slider_frac = local / self._input_bytes
            self.update()

    @property
    def current_global_offset(self) -> int:
        return self._global_start + int(self._slider_frac * self._input_bytes)

    def _mouse_to_frac(self, x: float) -> float:
        return max(0.0, min(1.0, x / max(1, self.width())))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        t = ThemeManager.get()

        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(t.bg_surface))
        painter.drawRoundedRect(QRectF(0, 0, w, h), 8, 8)

        strip_top = 30
        strip_h = h - strip_top - 30
        chunks = self._display_chunks

        # Info line
        painter.setPen(QColor(t.text_secondary))
        fnt = QFont(painter.font())
        fnt.setPointSize(9)
        painter.setFont(fnt)
        avg_e = sum(chunks) / max(len(chunks), 1) if chunks else 0.0
        painter.drawText(QRectF(8, 4, w - 16, 20), Qt.AlignmentFlag.AlignLeft,
                         f"{self._file_name}  |  μ entropy: {avg_e:.2f}  |  {len(chunks)} chunks  |  {self._chunk_bytes:,} B/chunk")

        if not chunks:
            painter.setPen(QColor(t.text_muted))
            painter.drawText(QRectF(0, strip_top, w, strip_h), Qt.AlignmentFlag.AlignCenter, "(无熵数据)")
            return

        # Chunk bars — each chunk gets at least _min_bar_w px
        bar_w = max(self._min_bar_w, w / max(len(chunks), 1))
        for i, entropy in enumerate(chunks):
            x = i * bar_w
            color = _entropy_to_color(entropy)
            if i == self._hover_chunk:
                color = color.lighter(120)
            painter.setBrush(color)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(QRectF(x, strip_top, bar_w + 0.5, strip_h), 1, 1)

        # Slider overlay
        slider_x = self._slider_frac * w
        slider_w = max(40.0, w * 0.03)
        s_left = max(0.0, slider_x - slider_w / 2)
        s_right = min(w, slider_x + slider_w / 2)

        painter.setBrush(QColor(251, 191, 36, 40))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawRoundedRect(QRectF(s_left, strip_top, s_right - s_left, strip_h), 4, 4)

        painter.setPen(QPen(QColor(251, 191, 36, 180), 2.0))
        painter.drawLine(int(slider_x), strip_top, int(slider_x), strip_top + strip_h)

        pin_r = 6.0
        painter.setBrush(QColor(251, 191, 36))
        painter.setPen(QPen(QColor(0, 0, 0, 60), 1))
        painter.drawEllipse(QRectF(slider_x - pin_r, strip_top - pin_r, pin_r * 2, pin_r * 2))

        # Readout
        chunk_idx = min(int(self._slider_frac * len(chunks)), max(0, len(chunks) - 1))
        cur_e = chunks[chunk_idx] if 0 <= chunk_idx < len(chunks) else 0.0
        offset = self.current_global_offset

        painter.setPen(QColor(t.accent))
        fnt2 = QFont(painter.font())
        fnt2.setPointSize(9)
        painter.setFont(fnt2)
        rtext = (
            f"0x{self._global_start:08X}"
            f"  ──  offset: {offset:,}  ({self._slider_frac * 100:.1f}%)"
            f"  ──  chunk {chunk_idx}/{len(chunks) - 1}"
            f"  ──  e = {cur_e:.3f}"
            f"  ──  0x{(self._global_start + self._input_bytes):08X}"
        )
        painter.drawText(QRectF(8, strip_top + strip_h + 4, w - 16, 20),
                         Qt.AlignmentFlag.AlignCenter, rtext)

    def mousePressEvent(self, event: QMouseEvent):
        self._dragging = True
        self._slider_frac = self._mouse_to_frac(event.position().x())
        self.slider_offset_changed.emit(self.current_global_offset)
        self.update()

    def mouseMoveEvent(self, event: QMouseEvent):
        if self._dragging:
            self._slider_frac = self._mouse_to_frac(event.position().x())
            self.slider_offset_changed.emit(self.current_global_offset)
        else:
            frac = self._mouse_to_frac(event.position().x())
            chunks = self._display_chunks
            if chunks:
                self._hover_chunk = min(int(frac * len(chunks)), len(chunks) - 1)
            else:
                self._hover_chunk = -1
        self.update()

    def mouseReleaseEvent(self, event: QMouseEvent):
        self._dragging = False
        self._slider_frac = self._mouse_to_frac(event.position().x())
        self.slider_offset_changed.emit(self.current_global_offset)
        self.update()

    def leaveEvent(self, event):
        self._dragging = False
        self._hover_chunk = -1
        self.update()


# ══════════════════════════════════════════════════════════════════════
# §5  Tier 3 — Content-level viz (LZ77 sliding window or byte heatmap)
# ══════════════════════════════════════════════════════════════════════

class Tier3VizWidget(QWidget):
    """Content-level view driven by Tier 2 slider position.

    - If .viz data is available → embedded WindowCanvas showing
      LZ77 match/literal events at the selected byte offset.
    - Fallback → byte-level entropy grid via HeatmapController.slice_byte_heatmap.
    """

    def __init__(self, viz_path: str = "", source_path: str = "",
                 parent=None):
        super().__init__(parent)
        self._viz_path = viz_path
        self._source_path = source_path
        self._viz_loader = None      # VizLoader (lazy)
        self._match_total: int = 0
        self._canvas: object = None  # WindowCanvas (when viz available)
        self._byte_grid: list[int] = []   # fallback byte values
        self._grid_offset: int = 0
        self._grid_entropy: float = 0.0
        self._use_viz: bool = False
        self._current_offset: int = 0
        self._file_size: int = 0

        self.setMinimumHeight(250)

        # Determine source file size for proper total_bytes
        if source_path:
            sp = Path(source_path)
            if sp.is_file():
                self._file_size = sp.stat().st_size

        if viz_path and Path(viz_path).is_file():
            self._init_viz()
        elif source_path:
            self._use_viz = False

    def _init_viz(self) -> None:
        try:
            from gui.engine.viz_loader import VizLoader
            self._viz_loader = VizLoader(self._viz_path)
            self._match_total = self._viz_loader.match_count
            if self._match_total > 0:
                self._use_viz = True
                # Embed WindowCanvas
                from gui.ui.widgets.window_canvas import WindowCanvas
                self._canvas = WindowCanvas(parent=self)
                layout = QVBoxLayout(self)
                layout.setContentsMargins(0, 0, 0, 0)
                layout.addWidget(self._canvas)
                return
        except Exception:
            pass
        self._use_viz = False

    def navigate_to(self, byte_offset: int) -> None:
        self._current_offset = byte_offset
        if self._use_viz and self._viz_loader and self._canvas:
            idx = self._viz_loader.find_event_index_at_offset(byte_offset)
            if idx >= 0:
                # Load a page of MatchEvents around idx for the sliding window
                half_page = 500
                start = max(0, idx - half_page)
                page = self._viz_loader.get_match_page(start, (half_page * 2 + 1) * 9)
                if page:
                    total_bytes = self._file_size or max(
                        (e.input_pos + max(e.length, 1) for e in page),
                        default=0,
                    )
                    self._canvas.set_events_page(
                        start, page, self._match_total, total_bytes,
                        source_path=self._source_path,
                    )
                self._canvas.set_step(idx)
        elif self._source_path:
            self._load_fallback_heat(byte_offset)
            self.update()

    def _load_fallback_heat(self, byte_offset: int) -> None:
        try:
            from gui.engine.heatmap_controller import HeatmapController
            half = 2048
            result = HeatmapController.slice_byte_heatmap(
                self._source_path, byte_offset - half, half * 2
            )
            self._byte_grid = result.get("bytes", [])
            self._grid_offset = result.get("offset", 0)
            self._grid_entropy = result.get("entropy_64k", 0.0)
        except Exception:
            self._byte_grid = []
            self._grid_offset = 0
            self._grid_entropy = 0.0

    def paintEvent(self, event) -> None:
        if self._use_viz:
            # WindowCanvas handles its own painting
            return
        painter = QPainter(self)
        t = ThemeManager.get()
        painter.fillRect(self.rect(), QColor(t.bg_surface))

        if not self._byte_grid:
            painter.setPen(QColor(t.text_muted))
            fnt = painter.font()
            fnt.setPointSize(10)
            painter.setFont(fnt)
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                             "← 选择 chunk 查看内容级细节 (需要 .viz 或源文件)")
            return

        w, h = self.width(), self.height()
        cells = self._byte_grid[:256]  # show up to 256 bytes
        cols = 16
        rows = (len(cells) + cols - 1) // cols
        cell_w = max(8, (w - 60) // cols)
        cell_h = max(8, min(cell_w, (h - 60) // max(rows, 1)))
        start_x = (w - cell_w * cols) // 2
        start_y = 40

        # Header
        painter.setPen(QColor(t.text_secondary))
        fnt = QFont("Menlo, Monaco, monospace")
        fnt.setPointSize(9)
        painter.setFont(fnt)
        painter.drawText(QRectF(8, 4, w - 16, 20), Qt.AlignmentFlag.AlignLeft,
                         f"Byte-level view  |  offset: 0x{self._grid_offset:08X}  |  "
                         f"entropy: {self._grid_entropy:.4f}  |  showing {len(cells)} bytes")

        for i, b in enumerate(cells):
            row, col = divmod(i, cols)
            x = start_x + col * cell_w
            y = start_y + row * cell_h
            # Color: map byte value to grayscale (0=black, 255=white)
            v = int(255 - b) if b <= 127 else b
            c = QColor(v, v, v)
            painter.setBrush(c)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(QRectF(x + 1, y + 1, cell_w - 2, cell_h - 2), 2, 2)

        # Row labels (offset)
        painter.setPen(QColor(t.text_muted))
        tiny_fnt = QFont("Menlo, Monaco, monospace")
        tiny_fnt.setPointSize(7)
        painter.setFont(tiny_fnt)
        for row in range(rows):
            off = self._grid_offset + row * cols
            painter.drawText(QRectF(2, start_y + row * cell_h, 54, cell_h),
                             Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                             f"0x{off:06X}")


# ══════════════════════════════════════════════════════════════════════
# §6  ThreeTierHeatmapDialog
# ══════════════════════════════════════════════════════════════════════

class ThreeTierHeatmapDialog(QDialog):
    """3-tier entropy heatmap dialog.

    Parameters:
        heat_path: Path to the .heat file (entropy data, mmap-read).
        source_path: Original source file path (for reference display).
        compressed_size: Bytes of the compressed artifact (for Tier 3 Deflate ratio).
        original_size: Bytes of the original file.
    """

    def __init__(self, heat_path: str = "", source_path: str = "",
                 compressed_size: int = 0, original_size: int = 0,
                 viz_path: str = "",
                 parent=None, folder_mode: bool = False):
        super().__init__(parent)
        self._heat_path = heat_path
        self._source_path = source_path
        self._compressed_size = compressed_size
        self._original_size = original_size
        self._viz_path = viz_path
        self._folder_mode = folder_mode
        self._controller: HeatmapController | None = None
        self._macro: dict = {}
        self._files: list[dict] = []
        self._selected_idx: int = -1

        title = source_path or heat_path or "文件夹"
        self.setWindowTitle("熵热力图 (3-Tier) — " + title)
        self.resize(1100, 750)
        self._setup_ui()
        if not folder_mode:
            self._load_data()

    # ── UI setup ──

    def _setup_ui(self) -> None:
        t = ThemeManager.get()
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 12, 16, 12)
        layout.setSpacing(6)

        # ── Tier 1 label ──
        t1_label = QLabel("Tier 1 — 全局文件条 (点击选择，滚轮横向浏览)")
        t1_label.setStyleSheet(self._tier_label_style(t))
        layout.addWidget(t1_label)

        self._tier1 = Tier1GlobalBar()
        self._tier1.file_selected.connect(self._on_file_selected)
        self._tier1_scroll = QScrollArea()
        self._tier1_scroll.setWidget(self._tier1)
        self._tier1_scroll.setWidgetResizable(False)
        self._tier1_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self._tier1_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._tier1_scroll.setFixedHeight(62)
        self._tier1_scroll.setStyleSheet(
            f"QScrollArea {{ border: 1px solid {t.border}; border-radius: 4px; background: transparent; }}"
        )
        layout.addWidget(self._tier1_scroll)

        # ── Tier 2 label ──
        t2_label = QLabel("Tier 2 — 单文件 chunk 级熵条带 (拖拽滑块浏览)")
        t2_label.setStyleSheet(self._tier_label_style(t))
        layout.addWidget(t2_label)

        self._tier2 = Tier2EntropyStrip()
        self._tier2.slider_offset_changed.connect(self._on_slider_moved)
        self._tier2_scroll = QScrollArea()
        self._tier2_scroll.setWidget(self._tier2)
        self._tier2_scroll.setWidgetResizable(False)
        self._tier2_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self._tier2_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._tier2_scroll.setFixedHeight(120)
        self._tier2_scroll.setStyleSheet(
            f"QScrollArea {{ border: 1px solid {t.border}; border-radius: 4px; background: transparent; }}"
        )
        layout.addWidget(self._tier2_scroll)

        # ── Tier 3 label ──
        t3_label = QLabel("Tier 3 — 内容级 LZ77 滑动窗口 / 逐字节热力")
        t3_label.setStyleSheet(self._tier_label_style(t))
        layout.addWidget(t3_label)

        self._tier3_container = QWidget()
        self._tier3_layout = QVBoxLayout(self._tier3_container)
        self._tier3_layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._tier3_container, stretch=1)

        # ── Footer ──
        footer = QHBoxLayout()
        footer.setSpacing(16)

        self._status_lbl = QLabel("")
        self._status_lbl.setStyleSheet(
            f"color: {t.text_muted}; font-size: 10px; background: transparent;")
        footer.addWidget(self._status_lbl, stretch=1)

        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.close)
        footer.addWidget(close_btn)
        layout.addLayout(footer)

    @staticmethod
    def _tier_label_style(t) -> str:
        return (
            f"font-size: 10px; color: {t.text_muted}; font-weight: 600; "
            f"text-transform: uppercase; letter-spacing: 1px; background: transparent;"
        )

    # ── Data ──

    def _load_data(self) -> None:
        from gui.engine.heatmap_controller import HeatmapController
        try:
            self._controller = HeatmapController(self._heat_path)
            self._macro = self._controller.macro_data
            self._files = self._macro.get("files", [])
            total = self._macro.get("total_input_bytes", 0)

            self._tier1.set_data(self._files, total)
            if self._files:
                self._select_file(0)
        except Exception as e:
            logger.error("[3tier] load failed: %s", e, exc_info=True)
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.warning(self, "热力图错误", f"加载 .heat 文件失败:\n{e}")


    def _select_file(self, idx: int) -> None:
        if idx < 0 or idx >= len(self._files):
            return
        self._selected_idx = idx
        f = self._files[idx]

        # ── Lazy-load .heat for folder mode ──
        heat_path = f.get("heat_path")
        if heat_path and len(f.get("entropy_array", [])) <= 1:
            self._lazy_load_file_entropy(idx, heat_path)

        self._tier1.select_index(idx)
        chunk_bytes = self._macro.get("chunk_bytes", 1048576)
        self._tier2.set_file(f, chunk_bytes)

        # ── Tier 3: replace with viz widget ──
        # Remove old tier3 widget
        while self._tier3_layout.count():
            item = self._tier3_layout.takeAt(0)
            if item and item.widget():
                item.widget().deleteLater()

        # Determine viz_path for this file
        file_viz = f.get("viz_path", self._viz_path)
        file_source = f.get("source_path", self._source_path)
        file_heat = heat_path or self._heat_path

        self._tier3 = Tier3VizWidget(
            viz_path=file_viz or "",
            source_path=file_source or "",
            parent=self,
        )
        self._tier3_layout.addWidget(self._tier3)

        # Navigate Tier 3 to slider position
        offset = self._tier2.current_global_offset
        self._tier3.navigate_to(offset)

        # Status
        if file_viz and Path(file_viz).is_file():
            self._status_lbl.setText(
                f"文件: {f.get('name', '')}  |  "
                f"chunks: {f.get('chunk_count', 0)}  |  "
                f"拖动 Tier 2 滑块浏览内容"
            )
        else:
            self._status_lbl.setText(
                f"文件: {f.get('name', '')}  |  "
                f"chunks: {f.get('chunk_count', 0)}  |  "
                f"无 .viz 数据，显示逐字节热力 (回退)"
            )

    def _lazy_load_file_entropy(self, idx: int, heat_path: str) -> None:
        """Swap controller to a new .heat file and update the file dict's entropy_array."""
        from gui.engine.heatmap_controller import HeatmapController

        # Close old controller
        if self._controller is not None:
            self._controller.close()
            self._controller = None

        try:
            self._controller = HeatmapController(heat_path)
            f = self._files[idx]
            f["entropy_array"] = self._controller.entropy_array
            f["input_bytes"] = self._controller.estimated_input_bytes
            f["chunk_count"] = self._controller.total_chunks
            f["avg_entropy"] = self._controller.avg_entropy
            if self._macro.get("chunk_bytes", 0) == 0:
                self._macro["chunk_bytes"] = self._controller.chunk_bytes
        except Exception as e:
            logger.error("[3tier] lazy-load heat failed: %s — %s", heat_path, e)

    # ── Signals ──

    def _on_file_selected(self, idx: int) -> None:
        self._select_file(idx)

    def _on_slider_moved(self, global_offset: int) -> None:
        if hasattr(self, '_tier3') and self._tier3 is not None:
            self._tier3.navigate_to(global_offset)

    # ── Lifecycle ──

    def closeEvent(self, event):
        if self._controller is not None:
            self._controller.close()
            self._controller = None
        super().closeEvent(event)

    # ── Factory ──────────────────────────────────────────────────

    @classmethod
    def from_folder(cls, folder_record, parent=None) -> "ThreeTierHeatmapDialog":
        """Build a folder-level heatmap dialog.

        Reads only the 20-byte header from each child file's .heat to build Tier 1.
        Tier 2/3 lazy-load the full .heat via controller swap on file selection.
        """
        from gui.models import FolderRecord as FR
        if not isinstance(folder_record, FR):
            raise TypeError("from_folder requires a FolderRecord")

        folder_record.ensure_files_loaded()

        # ── Read 20B headers from all .heat files ──
        _HEADER_FMT = struct.Struct("<I I I I f")  # magic, version, chunks, chunk_bytes, avg_entropy
        files: list[dict] = []
        total_input = 0
        chunk_bytes = 0

        for fr in folder_record.files:
            heat_path = getattr(fr, "heat_path", None)
            if not heat_path or not Path(heat_path).is_file():
                continue

            try:
                with open(heat_path, "rb") as hf:
                    header = hf.read(20)
                if len(header) < 20:
                    continue
                magic, version, total_chunks, cb, avg_e = _HEADER_FMT.unpack(header)
                if magic != 0x54414548:  # HEAT_MAGIC
                    continue
                input_bytes = cb * total_chunks
                files.append({
                    "name": fr.name,
                    "heat_path": heat_path,
                    "source_path": getattr(fr, "path", ""),
                    "viz_path": getattr(fr, "viz_path", ""),
                    "global_start": total_input,
                    "global_end": total_input + input_bytes,
                    "input_bytes": input_bytes,
                    "chunk_count": total_chunks,
                    "entropy_array": [avg_e],  # stub: single-item for Tier 1 coloring
                    "avg_entropy": avg_e,
                })
                total_input += input_bytes
                if chunk_bytes == 0:
                    chunk_bytes = cb
            except Exception as e:
                logger.warning("[3tier] skip heat header read: %s — %s", heat_path, e)
                continue

        if not files:
            # Fallback: single placeholder so dialog still renders
            files = [{
                "name": folder_record.name,
                "global_start": 0,
                "global_end": folder_record.size,
                "input_bytes": folder_record.size,
                "chunk_count": 1,
                "entropy_array": [4.0],
            }]

        # ── Build dialog ──
        dlg = cls(
            heat_path="",
            source_path=folder_record.path,
            compressed_size=folder_record.total_compressed,
            original_size=folder_record.total_original,
            viz_path="",
            parent=parent,
            folder_mode=True,
        )
        dlg._macro = {
            "total_input_bytes": total_input or folder_record.size,
            "chunk_bytes": chunk_bytes or 1048576,
            "total_chunks": sum(f.get("chunk_count", 0) for f in files),
            "files": files,
        }
        dlg._files = files
        dlg._tier1.set_data(files, total_input or folder_record.size)
        if files:
            dlg._select_file(0)

        if folder_record.total_compressed > 0:
            comp_ratio = folder_record.compression_ratio if folder_record.total_original > 0 else 1.0
            dlg._status_lbl.setText(
                f"整体压缩率: {(comp_ratio * 100):.1f}%  |  "
                f"原始: {folder_record.total_original:,} B  →  "
                f"压缩后: {folder_record.total_compressed:,} B  |  "
                f"点击 Tier 1 文件切换 Tier 2/3"
            )

        return dlg
