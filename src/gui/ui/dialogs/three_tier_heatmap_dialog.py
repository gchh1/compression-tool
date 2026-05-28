"""3-Tier compression heatmap dialog: file bar → block strip → character-level viz.

Tier 1 – File-level horizontal bar (compression-ratio coloured segments).
Tier 2 – Block-level compression-ratio strip (Deflate blocks, click to select).
Tier 3 – Character-level minimap + hex text view (bidirectional sync).

Data sources (priority):
  - .viz v2: BlockBoundary → Tier 1/2 compression ratios
              MatchEvent + HuffmanTreeBuilt → Tier 3 per-char encoding ratios
  - .heat v2: fallback entropy data when .viz is unavailable
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
    from gui.engine.viz_loader import VizLoader

logger = logging.getLogger(__name__)

# ══════════════════════════════════════════════════════════════════════
# §1  Compression ratio → colour  (green→yellow→red, per heatmap.html)
# ══════════════════════════════════════════════════════════════════════

def _compression_ratio_to_color(ratio: float) -> QColor:
    """Map compression ratio [0, 1] to green→yellow→red.

    0.0 = highly compressible (green) — lots of matches.
    1.0 = incompressible    (red)   — mostly literals.

    Three-segment linear interpolation, matching heatmap.html's
    ``getTrafficColor()``.
    """
    t = max(0.0, min(1.0, ratio))
    if t < 0.5:
        # green → yellow
        r = int(255 * (t * 2))
        g = 255
        b = 48
    else:
        # yellow → red
        r = 255
        g = int(255 * (2 - t * 2))
        b = 48
    return QColor(r, g, b)


def _ratio_to_contrast_color(ratio: float) -> QColor:
    """Return white for dark backgrounds, dark for light backgrounds."""
    return QColor(255, 255, 255) if ratio > 0.6 else QColor(15, 23, 42)


# ══════════════════════════════════════════════════════════════════════
# §2  Tier 1 — Global File Bar (compression ratio)
# ══════════════════════════════════════════════════════════════════════

class Tier1GlobalBar(QWidget):
    """Horizontal bar — each file segment coloured by overall compression ratio.

    Block height matches Tier2BlockStrip; labels are painted inside blocks
    when width permits, and a hover tooltip shows full details.
    """

    file_selected = pyqtSignal(int)

    _BAR_TOP = 6
    _BAR_BOT = 8
    _LABEL_H = 16

    def __init__(self, parent=None):
        super().__init__(parent)
        self._files: list[dict] = []
        self._total_bytes: int = 0
        self._selected_idx: int = -1
        self._hover_idx: int = -1
        self.setMouseTracking(True)
        self.setMinimumHeight(68)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

        self._sel_pen = QPen(QColor(0xFB, 0xBF, 0x24), 2.0)

    def set_data(self, files: list[dict], total_bytes: int) -> None:
        self._files = files
        self._total_bytes = total_bytes
        if files and self._selected_idx < 0:
            self._selected_idx = 0
        if files and total_bytes > 0:
            min_seg = 60.0
            base_w = float(max(300, len(files) * 120))
            self._min_width = int(sum(
                max(min_seg, (f["input_bytes"] / total_bytes) * base_w)
                for f in files
            ))
            self.setMinimumWidth(max(300, self._min_width))
        else:
            self.setMinimumWidth(300)
        self.update()

    def select_index(self, idx: int) -> None:
        if 0 <= idx < len(self._files):
            self._selected_idx = idx
            self.update()

    def _segment_at(self, x: int) -> int:
        if not self._files or self._total_bytes <= 0:
            return -1
        frac = x / max(1, self.width())
        cursor = int(frac * self._total_bytes)
        pos = 0
        for i, f in enumerate(self._files):
            pos += f["input_bytes"]
            if cursor < pos:
                return i
        return len(self._files) - 1

    def _item_start(self, idx: int) -> int:
        acc = 0
        for i in range(idx):
            acc += self._files[i]["input_bytes"]
        return acc

    def _item_end(self, idx: int) -> int:
        return self._item_start(idx) + self._files[idx]["input_bytes"]

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        t = ThemeManager.get()

        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(t.bg_surface))
        painter.drawRoundedRect(QRectF(0, 0, w, h), 6, 6)

        if not self._files or self._total_bytes <= 0:
            painter.setPen(QColor(t.text_muted))
            fnt = painter.font()
            fnt.setPointSize(11)
            painter.setFont(fnt)
            painter.drawText(QRectF(0, 0, w, h), Qt.AlignmentFlag.AlignCenter, "(无文件数据)")
            return

        min_seg = 60.0
        bar_h = max(12, h - self._BAR_TOP - self._BAR_BOT - self._LABEL_H)
        bar_top = self._BAR_TOP + self._LABEL_H

        fnt_label = QFont(painter.font())
        fnt_label.setPointSize(9)
        fnt_small = QFont(painter.font())
        fnt_small.setPointSize(8)

        x = 0.0
        for i, f in enumerate(self._files):
            seg_w = max(min_seg, (f["input_bytes"] / self._total_bytes) * w)
            ratio = f.get("compression_ratio", 1.0)
            color = _compression_ratio_to_color(ratio)
            hovered = i == self._hover_idx
            selected = i == self._selected_idx

            # Hover: scale block vertically
            scale = 1.10 if hovered else 1.0
            cy = bar_top + (bar_h - bar_h * scale) / 2
            ch = bar_h * scale

            # ── Coloured block ──
            painter.setBrush(QBrush(color))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(QRectF(x + 1, cy, seg_w - 2, ch), 3, 3)

            # ── Label above block ──
            pct = (1.0 - ratio) * 100
            label = f"{f['name']}  {pct:.0f}%"
            painter.setPen(_ratio_to_contrast_color(ratio) if seg_w > 80 else QColor(t.text_primary))
            painter.setFont(fnt_label)
            fm = QFontMetrics(fnt_label)
            if fm.horizontalAdvance(label) > seg_w - 4:
                if seg_w > 50:
                    label = f"{f['name']}" if len(f['name']) < 12 else f"{f['name'][:10]}…"
                    if fm.horizontalAdvance(label) > seg_w - 4:
                        label = fm.elidedText(label, Qt.TextElideMode.ElideRight, int(seg_w) - 4)
                else:
                    label = fm.elidedText(label, Qt.TextElideMode.ElideRight, int(seg_w) - 4)
            painter.drawText(QRectF(x + 2, self._BAR_TOP, seg_w - 4, self._LABEL_H),
                           Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, label)

            # ── Ratio inside block (if wide enough) ──
            if seg_w > 70:
                inner_label = f"{ratio:.2f}"
                painter.setPen(_ratio_to_contrast_color(ratio))
                painter.setFont(fnt_small)
                painter.drawText(QRectF(x + 2, cy, seg_w - 4, ch),
                               Qt.AlignmentFlag.AlignCenter, inner_label)

            # ── Selection outline ──
            if selected:
                painter.setBrush(Qt.BrushStyle.NoBrush)
                painter.setPen(self._sel_pen)
                painter.drawRoundedRect(QRectF(x + 1, self._BAR_TOP, seg_w - 2, h - self._BAR_TOP - self._BAR_BOT), 4, 4)

            x += seg_w

    def mouseMoveEvent(self, event: QMouseEvent):
        idx = self._segment_at(int(event.position().x()))
        if idx != self._hover_idx:
            self._hover_idx = idx
            if 0 <= idx < len(self._files):
                f = self._files[idx]
                ratio = f.get("compression_ratio", 1.0)
                pct = (1.0 - ratio) * 100
                tip = (f"{f['name']}\n"
                       f"压缩率: {ratio:.4f}  (节省 {pct:.1f}%)\n"
                       f"大小: {f['input_bytes']:,} B")
                algo = f.get("_algorithm")
                if algo:
                    tip += f"\n算法: {algo}"
                tcnt = f.get("_token_count")
                if tcnt is not None:
                    tip += f"\nToken 数: {tcnt}"
                self.setToolTip(tip)
            else:
                self.setToolTip("")
            self.update()

    def mousePressEvent(self, event: QMouseEvent):
        idx = self._segment_at(int(event.position().x()))
        if idx >= 0:
            self._selected_idx = idx
            self.file_selected.emit(idx)
            self.update()

    def leaveEvent(self, event):
        self._hover_idx = -1
        self.setToolTip("")
        self.update()


# ══════════════════════════════════════════════════════════════════════
# §3  Tier 2 — Block-level Compression-Ratio Strip
# ══════════════════════════════════════════════════════════════════════

class Tier2BlockStrip(QWidget):
    """Discrete coloured blocks — same visual language as Tier 1.

    - Viz mode: one rectangle per Deflate block, width ∝ input_bytes.
    - Entropy fallback: equal-width rectangles per .heat chunk.
    - Click selects a block; selected block gets amber outline.
    - Hover scales the block vertically + shows tooltip.
    """

    block_selected = pyqtSignal(int)  # global_offset

    _BAR_TOP = 6
    _BAR_BOT = 8
    _LABEL_H = 16

    def __init__(self, parent=None):
        super().__init__(parent)
        self._blocks: list[dict] = []          # {input_start, input_bytes, output_bytes, ratio}
        self._ratio_chunks: list[float] = []  # fallback ratios (from .heat v2/v3)
        self._chunk_bytes: int = 0
        self._global_start: int = 0
        self._total_input: int = 0
        self._file_name: str = ""
        self._selected_idx: int = -1
        self._hover_idx: int = -1
        self._use_viz: bool = False
        self.setMouseTracking(True)
        self.setMinimumHeight(68)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

        self._sel_pen = QPen(QColor(0xFB, 0xBF, 0x24), 2.0)

    def set_blocks(self, blocks: list[dict], global_start: int,
                   file_name: str = "", total_input: int = 0) -> None:
        """Set data from .viz BlockBoundary (compression ratio mode)."""
        self._blocks = blocks
        self._global_start = global_start
        self._file_name = file_name
        self._total_input = total_input
        self._selected_idx = -1
        self._hover_idx = -1
        self._use_viz = True
        self._ratio_chunks = []
        self.setMinimumWidth(max(300, len(blocks) * 16))
        self.update()

    def set_ratios(self, ratios: list[float], chunk_bytes: int,
                   global_start: int, file_name: str = "",
                   input_bytes: int = 0) -> None:
        """Set data from .heat v2/v3 compression ratios (fallback mode)."""
        self._ratio_chunks = list(ratios)
        self._chunk_bytes = chunk_bytes
        self._global_start = global_start
        self._file_name = file_name
        self._total_input = input_bytes
        self._selected_idx = -1
        self._hover_idx = -1
        self._use_viz = False
        self._blocks = []
        self.setMinimumWidth(max(300, len(ratios) * 16))
        self.update()

    def set_selected_offset(self, global_offset: int) -> None:
        """Highlight the block / chunk containing *global_offset*."""
        idx = self._index_at_offset(global_offset)
        if idx != self._selected_idx:
            self._selected_idx = idx
            self.update()

    @property
    def current_global_offset(self) -> int:
        if 0 <= self._selected_idx < self._item_count:
            return self._item_start(self._selected_idx)
        return self._global_start

    # ── item abstraction (works for both viz blocks & entropy chunks) ──

    @property
    def _item_count(self) -> int:
        return len(self._blocks) if self._use_viz else len(self._ratio_chunks)

    def _item_start(self, idx: int) -> int:
        if self._use_viz and 0 <= idx < len(self._blocks):
            return self._global_start + self._blocks[idx].get("input_start", 0)
        if not self._use_viz and 0 <= idx < len(self._ratio_chunks):
            return self._global_start + idx * self._chunk_bytes
        return self._global_start

    def _item_end(self, idx: int) -> int:
        if self._use_viz and 0 <= idx < len(self._blocks):
            b = self._blocks[idx]
            return self._global_start + b["input_start"] + b["input_bytes"]
        if not self._use_viz and 0 <= idx < len(self._ratio_chunks):
            return self._global_start + (idx + 1) * self._chunk_bytes
        return self._global_start

    def _item_size(self, idx: int) -> int:
        if self._use_viz and 0 <= idx < len(self._blocks):
            return self._blocks[idx].get("input_bytes", 0)
        if not self._use_viz and 0 <= idx < len(self._ratio_chunks):
            return self._chunk_bytes
        return 0

    def _item_ratio(self, idx: int) -> float:
        if self._use_viz and 0 <= idx < len(self._blocks):
            return self._blocks[idx].get("ratio", 1.0)
        if not self._use_viz and 0 <= idx < len(self._ratio_chunks):
            return self._ratio_chunks[idx]
        return 1.0

    def _item_color(self, idx: int) -> QColor:
        return _compression_ratio_to_color(self._item_ratio(idx))

    def _item_width(self, idx: int, total_w: float) -> float:
        """Return raw proportional width — no minimum clamp (applied later)."""
        if self._use_viz and self._total_input > 0:
            b = self._blocks[idx]
            return (b["input_bytes"] / self._total_input) * total_w
        count = max(1, len(self._ratio_chunks))
        return total_w / count

    def _find_item_at(self, x: float) -> int:
        if self._item_count == 0:
            return -1
        w = max(1, self.width())
        frac = max(0.0, min(1.0, x / w))
        if self._use_viz and self._total_input > 0:
            cursor = frac * self._total_input
            acc = 0
            for i, b in enumerate(self._blocks):
                acc += b["input_bytes"]
                if cursor < acc:
                    return i
            return len(self._blocks) - 1
        else:
            return min(int(frac * self._item_count), self._item_count - 1)

    def _index_at_offset(self, global_offset: int) -> int:
        """Return the item index covering *global_offset*, or -1."""
        if self._item_count == 0:
            return -1
        for i in range(self._item_count):
            if self._item_start(i) <= global_offset < self._item_end(i):
                return i
        return -1

    # ── paint ──────────────────────────────────────────────────────

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        t = ThemeManager.get()

        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(t.bg_surface))
        painter.drawRoundedRect(QRectF(0, 0, w, h), 6, 6)

        count = self._item_count
        bar_h = max(12, h - self._BAR_TOP - self._BAR_BOT - self._LABEL_H)
        bar_top = self._BAR_TOP + self._LABEL_H

        if count == 0:
            painter.setPen(QColor(t.text_muted))
            painter.drawText(QRectF(0, bar_top, w, bar_h),
                             Qt.AlignmentFlag.AlignCenter, "(无数据)")
            return

        # ── Bin if too many items ──
        if count > 200:
            items, item_w_fracs = self._bin_items(w)
        else:
            items = list(range(count))
            items_w = [self._item_width(i, float(w)) for i in range(count)]
            # Scale widths proportionally so they sum to w
            total_items_w = sum(items_w)
            if total_items_w > 0:
                item_w_fracs = [iw * w / total_items_w for iw in items_w]
            else:
                item_w_fracs = [w / count] * count

        fnt_label = QFont(painter.font())
        fnt_label.setPointSize(9)
        fnt_small = QFont(painter.font())
        fnt_small.setPointSize(8)

        x = 0.0
        for i, item_idx in enumerate(items):
            bw = max(4.0, item_w_fracs[i])  # min 4 px for visibility / hit-test
            color = self._item_color(item_idx)
            ratio = self._item_ratio(item_idx)
            hovered = item_idx == self._hover_idx
            selected = item_idx == self._selected_idx

            scale = 1.10 if hovered else 1.0
            cy = bar_top + (bar_h - bar_h * scale) / 2
            ch = bar_h * scale

            # ── Coloured block ──
            painter.setBrush(QBrush(color))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(QRectF(x + 1, cy, bw - 2, ch), 3, 3)

            # ── Label above block ──
            if self._use_viz:
                label = f"B{item_idx}  {ratio:.2f}"
            else:
                r_val = self._ratio_chunks[item_idx] if item_idx < len(self._ratio_chunks) else 1.0
                label = f"C{item_idx}  {r_val:.2f}"
            painter.setPen(_ratio_to_contrast_color(ratio) if bw > 80 else QColor(t.text_primary))
            painter.setFont(fnt_label)
            fm = QFontMetrics(fnt_label)
            if fm.horizontalAdvance(label) > bw - 4:
                label = fm.elidedText(label, Qt.TextElideMode.ElideRight, int(bw) - 4)
            painter.drawText(QRectF(x + 2, self._BAR_TOP, bw - 4, self._LABEL_H),
                           Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, label)

            # ── Ratio value inside block ──
            if bw > 70:
                inner = f"{ratio:.2f}"
                painter.setPen(_ratio_to_contrast_color(ratio))
                painter.setFont(fnt_small)
                painter.drawText(QRectF(x + 2, cy, bw - 4, ch),
                               Qt.AlignmentFlag.AlignCenter, inner)

            # ── Selection outline ──
            if selected:
                painter.setBrush(Qt.BrushStyle.NoBrush)
                painter.setPen(self._sel_pen)
                painter.drawRoundedRect(QRectF(x + 1, self._BAR_TOP, bw - 2, h - self._BAR_TOP - self._BAR_BOT), 4, 4)

            x += bw

        # ── Readout below blocks ──
        self._paint_readout(painter, w, h, t)

    def _bin_items(self, total_w: float) -> tuple[list[int], list[float]]:
        """Merge items into ≤200 bins, returning (representative_index, width)."""
        count = self._item_count
        max_bins = 200
        bin_size = max(1, count / max_bins)

        indices: list[int] = []
        widths: list[float] = []
        for bi in range(max_bins):
            lo = int(bi * bin_size)
            hi = min(count, int((bi + 1) * bin_size))
            if hi <= lo:
                break
            indices.append((lo + hi) // 2)
            if self._use_viz and self._total_input > 0:
                w_sum = sum(self._blocks[j]["input_bytes"] for j in range(lo, hi))
                widths.append(max(60.0, (w_sum / self._total_input) * total_w))
            else:
                widths.append(max(60.0, total_w / max_bins))
        return indices, widths

    def _paint_readout(self, painter: QPainter, w: float, h: float, t) -> None:
        """Status line below the blocks."""
        y = h - self._BAR_BOT
        painter.setPen(QColor(t.text_muted))
        fnt = QFont(painter.font())
        fnt.setPointSize(8)
        painter.setFont(fnt)

        if self._use_viz:
            avg_r = (sum(b["ratio"] for b in self._blocks) / max(len(self._blocks), 1)) \
                    if self._blocks else 1.0
            text = (f"{self._file_name}  |  avg ratio: {avg_r:.3f}  |  "
                    f"{self._item_count} blocks  |  {self._total_input:,} B")
        else:
            chunks = self._ratio_chunks
            avg_r = sum(chunks) / max(len(chunks), 1) if chunks else 1.0
            text = (f"{self._file_name}  |  avg ratio: {avg_r:.3f}  |  "
                    f"{self._item_count} chunks  |  {self._chunk_bytes:,} B/chunk")
        painter.drawText(QRectF(8, y, w - 16, 14), Qt.AlignmentFlag.AlignLeft, text)

    # ── mouse ───────────────────────────────────────────────────────

    def mousePressEvent(self, event: QMouseEvent):
        idx = self._find_item_at(event.position().x())
        if idx >= 0 and idx != self._selected_idx:
            self._selected_idx = idx
            offset = self._item_start(idx)
            self.block_selected.emit(offset)
            self.update()

    def mouseMoveEvent(self, event: QMouseEvent):
        idx = self._find_item_at(event.position().x())
        if idx != self._hover_idx:
            self._hover_idx = idx
            if 0 <= idx < self._item_count:
                start = self._item_start(idx)
                end = self._item_end(idx)
                ratio = self._item_ratio(idx)
                size = self._item_size(idx)
                if self._use_viz:
                    b = self._blocks[idx]
                    tip = (f"block {idx}\n"
                           f"offset: 0x{start:06X} – 0x{end:06X}\n"
                           f"压缩率: {ratio:.4f}\n"
                           f"大小: {size:,} B → {b.get('output_bytes', 0):,} B")
                else:
                    r_val = self._ratio_chunks[idx] if idx < len(self._ratio_chunks) else 1.0
                    tip = (f"chunk {idx}\n"
                           f"offset: 0x{start:06X} – 0x{end:06X}\n"
                           f"压缩率: {r_val:.4f}\n"
                           f"大小: {size:,} B")
                self.setToolTip(tip)
            else:
                self.setToolTip("")
            self.update()

    def leaveEvent(self, event):
        self._hover_idx = -1
        self.setToolTip("")
        self.update()


# ══════════════════════════════════════════════════════════════════════
# §4  Tier 3a — Minimap (block panorama + viewport indicator)
# ══════════════════════════════════════════════════════════════════════

class Tier3Minimap(QWidget):
    """Miniature heatmap of a block's byte-level compression ratios.

    Shows a down-sampled vertical-bar chart with a draggable viewport
    indicator that controls the hex text view.
    """

    viewport_changed = pyqtSignal(int)  # byte offset to center text view on

    def __init__(self, parent=None):
        super().__init__(parent)
        self._ratios: list[float] = []
        self._block_start: int = 0
        self._block_end: int = 0
        self._viewport_start: int = 0
        self._viewport_end: int = 0
        self._dragging: bool = False
        self.setMouseTracking(True)
        self.setMinimumHeight(50)
        self.setMaximumHeight(80)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def set_data(self, ratios: list[float], block_start: int, block_end: int) -> None:
        self._ratios = ratios
        self._block_start = block_start
        self._block_end = block_end
        self._viewport_start = block_start
        self._viewport_end = min(block_start + 256, block_end)
        self.update()

    def set_viewport(self, start: int, end: int) -> None:
        """Update viewport indicator position (called from hex view scroll)."""
        if start != self._viewport_start or end != self._viewport_end:
            self._viewport_start = start
            self._viewport_end = end
            self.update()

    def _offset_for_x(self, x: float) -> int:
        if self._block_end <= self._block_start:
            return self._block_start
        frac = max(0.0, min(1.0, x / self.width()))
        return self._block_start + int(frac * (self._block_end - self._block_start))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        t = ThemeManager.get()

        painter.fillRect(0, 0, w, h, QColor(t.bg_surface))

        ratios = self._ratios
        n = len(ratios)
        if n == 0:
            painter.setPen(QColor(t.text_muted))
            fnt = painter.font()
            fnt.setPointSize(9)
            painter.setFont(fnt)
            painter.drawText(QRectF(0, 0, w, h), Qt.AlignmentFlag.AlignCenter,
                           "(无局部数据)")
            return

        # ── Down-sample to pixel width ──
        bins = min(n, max(1, int(w / 3.0)))
        bin_w = w / bins
        bar_top = 4
        bar_h = h - 8

        for j in range(bins):
            lo = int(j * n / bins)
            hi = max(lo + 1, int((j + 1) * n / bins))
            avg = sum(ratios[lo:hi]) / (hi - lo)
            color = _compression_ratio_to_color(avg)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QBrush(color))
            bx = j * bin_w
            painter.drawRoundedRect(QRectF(bx, bar_top, bin_w, bar_h), 1, 1)

        # ── Viewport indicator ──
        total_span = self._block_end - self._block_start
        if total_span > 0:
            vp_left = (self._viewport_start - self._block_start) / total_span * w
            vp_right = (self._viewport_end - self._block_start) / total_span * w
            vp_w = max(4.0, vp_right - vp_left)

            painter.setBrush(QBrush(QColor(255, 255, 255, 35)))
            painter.setPen(QPen(QColor(255, 255, 255, 100), 1))
            painter.drawRoundedRect(QRectF(vp_left, 0, vp_w, h), 3, 3)

    def mousePressEvent(self, event: QMouseEvent):
        self._dragging = True
        offset = self._offset_for_x(event.position().x())
        self.viewport_changed.emit(offset)

    def mouseMoveEvent(self, event: QMouseEvent):
        if self._dragging:
            offset = self._offset_for_x(event.position().x())
            self.viewport_changed.emit(offset)

    def mouseReleaseEvent(self, event: QMouseEvent):
        self._dragging = False
        offset = self._offset_for_x(event.position().x())
        self.viewport_changed.emit(offset)

    def leaveEvent(self, event):
        self._dragging = False


# ══════════════════════════════════════════════════════════════════════
# §5  Tier 3b — Hex Text View (per heatmap.html Layer 2)
# ══════════════════════════════════════════════════════════════════════

class _HexGridWidget(QWidget):
    """Inner widget that renders the 16-column hex grid."""

    _COLS = 16
    _CELL_W = 58
    _CELL_H = 42
    _ADDR_W = 80
    _LINE_H = 44
    _PAD = 4

    def __init__(self, parent=None):
        super().__init__(parent)
        self._source_data: bytes = b""
        self._ratios: list[float] = []
        self._file_start: int = 0
        self._file_size: int = 0
        self._total_rows: int = 0
        self.setMouseTracking(True)
        self._setMinimums()

    def _setMinimums(self):
        self._total_rows = max(1, (len(self._source_data) + self._COLS - 1) // self._COLS)
        total_w = self._ADDR_W + self._COLS * (self._CELL_W + 1) + self._PAD * 2
        total_h = self._total_rows * self._LINE_H + self._PAD * 2
        self.setMinimumSize(total_w, total_h)
        self.resize(total_w, total_h)

    def set_data(self, source_data: bytes, ratios: list[float],
                 file_start: int = 0) -> None:
        self._source_data = source_data
        self._ratios = ratios
        self._file_start = file_start
        self._file_size = len(source_data)
        self._setMinimums()
        self.update()

    def offset_at_row(self, row: int) -> int:
        return self._file_start + row * self._COLS

    def mouseMoveEvent(self, event):
        """Show per-cell tooltip with offset, hex, ASCII, and compression ratio."""
        x = event.position().x()
        y = event.position().y()

        col = int((x - self._ADDR_W) // (self._CELL_W + 1))
        row = int((y - self._PAD) // self._LINE_H)

        if 0 <= col < self._COLS and 0 <= row < self._total_rows:
            byte_idx = row * self._COLS + col
            if byte_idx < len(self._source_data):
                byte_val = self._source_data[byte_idx]
                ratio = self._ratios[byte_idx] if byte_idx < len(self._ratios) else 1.0
                off = self._file_start + byte_idx
                ascii_ch = chr(byte_val) if 32 <= byte_val < 127 else "·"
                tip = (f"Offset: 0x{off:06X}  ({off:,})\n"
                       f"Hex: 0x{byte_val:02X}\n"
                       f"ASCII: {ascii_ch}\n"
                       f"压缩率: {ratio:.4f}")
                self.setToolTip(tip)
                return
        self.setToolTip("")

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        t = ThemeManager.get()
        painter.fillRect(self.rect(), QColor(t.bg_surface))

        data = self._source_data
        ratios = self._ratios
        n = len(data)
        if n == 0:
            return

        total_rows = self._total_rows

        # Determine which rows are visible
        clip_rect = event.rect()
        first_row = max(0, int((clip_rect.top() - self._PAD) / self._LINE_H))
        last_row = min(total_rows, int((clip_rect.bottom() - self._PAD) / self._LINE_H) + 1)

        fnt_hex = QFont("Menlo, Monaco, monospace")
        fnt_hex.setPointSize(11)
        fnt_hex.setBold(True)
        fnt_ascii = QFont("Menlo, Monaco, monospace")
        fnt_ascii.setPointSize(8)

        addr_fnt = QFont("Menlo, Monaco, monospace")
        addr_fnt.setPointSize(9)

        for row in range(first_row, last_row):
            y = self._PAD + row * self._LINE_H
            addr = self._file_start + row * self._COLS

            # ── Address label ──
            painter.setPen(QColor(t.text_muted))
            painter.setFont(addr_fnt)
            painter.drawText(QRectF(2, y, self._ADDR_W - 8, self._CELL_H),
                           Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                           f"+0x{addr:06X}")

            # ── Byte cells ──
            for col in range(self._COLS):
                byte_idx = row * self._COLS + col
                if byte_idx >= n:
                    break

                bx = self._ADDR_W + col * (self._CELL_W + 1)
                by = y

                byte_val = data[byte_idx]
                ratio = ratios[byte_idx] if byte_idx < len(ratios) else 1.0

                # Cell background
                bg = _compression_ratio_to_color(ratio)
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(QBrush(bg))
                painter.drawRoundedRect(QRectF(bx, by, self._CELL_W, self._CELL_H), 2, 2)

                # Hex value (top half)
                painter.setPen(_ratio_to_contrast_color(ratio))
                painter.setFont(fnt_hex)
                hex_str = f"{byte_val:02X}"
                painter.drawText(QRectF(bx, by, self._CELL_W, self._CELL_H * 0.55),
                               Qt.AlignmentFlag.AlignCenter, hex_str)

                # ASCII (bottom half)
                ascii_ch = chr(byte_val) if 32 <= byte_val < 127 else "·"
                fg = _ratio_to_contrast_color(ratio)
                fg.setAlpha(150)
                painter.setPen(fg)
                painter.setFont(fnt_ascii)
                painter.drawText(QRectF(bx, by + self._CELL_H * 0.4,
                                        self._CELL_W, self._CELL_H * 0.55),
                               Qt.AlignmentFlag.AlignCenter, ascii_ch)

    def visible_range(self, scroll_value: int, viewport_height: int) -> tuple[int, int]:
        """Return (start_offset, end_offset) visible in the scroll area."""
        first_row = max(0, int((scroll_value - self._PAD) / self._LINE_H))
        last_row = min(self._total_rows,
                       int((scroll_value + viewport_height - self._PAD) / self._LINE_H) + 1)
        return (self.offset_at_row(first_row),
                min(self._file_start + self._file_size,
                    self.offset_at_row(last_row)))


class Tier3HexView(QScrollArea):
    """Scrollable hex-grid text view (per heatmap.html Layer 2).

    Emits visible_range_changed when the user scrolls, so the minimap
    can update its viewport indicator.
    """

    visible_range_changed = pyqtSignal(int, int)  # start_offset, end_offset

    def __init__(self, parent=None):
        super().__init__(parent)
        self._grid = _HexGridWidget()
        self.setWidget(self._grid)
        self.setWidgetResizable(False)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self._blocking: bool = False

        self.verticalScrollBar().valueChanged.connect(self._on_scroll)

    def set_data(self, source_data: bytes, ratios: list[float],
                 file_start: int = 0) -> None:
        self._grid.set_data(source_data, ratios, file_start)

    def scroll_to_offset(self, byte_offset: int) -> None:
        """Scroll to show *byte_offset* near the top of the viewport."""
        if not self._grid._source_data:
            return
        row = max(0, (byte_offset - self._grid._file_start) // self._grid._COLS)
        y = self._grid._PAD + row * self._grid._LINE_H
        self._blocking = True
        # Force geometry update so scrollbar range is valid
        self._grid.updateGeometry()
        self.verticalScrollBar().setMaximum(max(0, self._grid.height() - self.viewport().height()))
        self.verticalScrollBar().setValue(max(0, y))
        self._blocking = False

    def _on_scroll(self, value: int) -> None:
        if self._blocking:
            return
        start, end = self._grid.visible_range(value, self.viewport().height())
        self.visible_range_changed.emit(start, end)


# ══════════════════════════════════════════════════════════════════════
# §6  Tier 3 — Container widget (Minimap + HexView, or .viz WindowCanvas)
# ══════════════════════════════════════════════════════════════════════

class Tier3VizWidget(QWidget):
    """Content-level view driven by Tier 2 block selection.

    Priority:
      1. Source file → minimap + hex text view (compression-ratio heatmap, uses .viz if available)
      2. .viz data only → WindowCanvas (LZ77 sliding window)
      3. Neither → placeholder
    """

    def __init__(self, viz_loader: VizLoader | None = None,
                 source_path: str = "", parent=None,
                 fallback_ratios: list[float] | None = None,
                 fallback_block_size: int = 256):
        super().__init__(parent)
        self._viz_loader = viz_loader
        self._source_path = source_path
        self._canvas: object = None
        self._use_viz: bool = False
        self._current_offset: int = 0
        self._file_size: int = 0

        # Fallback: CharEncodingRatioBuilder
        self._ratio_builder = None
        self._minimap: Tier3Minimap | None = None
        self._hex_view: Tier3HexView | None = None
        self._fallback_layout: QVBoxLayout | None = None

        # Fallback block-level compression ratios (when no .viz file)
        self._fallback_ratios = fallback_ratios
        self._fallback_block_size = fallback_block_size

        self.setMinimumHeight(220)

        if source_path:
            sp = Path(source_path)
            if sp.is_file():
                self._file_size = sp.stat().st_size

        # Prefer minimap+hexview when source file is available
        if source_path and Path(source_path).is_file():
            self._init_fallback(viz_loader)
        elif viz_loader and viz_loader.match_count > 0:
            self._init_viz()

        if not self._use_viz and self._fallback_layout is None:
            self._init_fallback(viz_loader)

    # ── .viz WindowCanvas path ─────────────────────────────────────

    def _init_viz(self) -> None:
        try:
            self._match_total = self._viz_loader.match_count
            if self._match_total > 0:
                from gui.ui.widgets.window_canvas import WindowCanvas
                self._canvas = WindowCanvas(parent=self)
                layout = QVBoxLayout(self)
                layout.setContentsMargins(0, 0, 0, 0)
                layout.addWidget(self._canvas)
                self._use_viz = True
                return
        except Exception:
            pass
        self._use_viz = False

    # ── Fallback: minimap + hex view ───────────────────────────────

    def _init_fallback(self, viz_loader) -> None:
        if viz_loader is not None:
            try:
                from gui.engine.char_encoding_ratio import CharEncodingRatioBuilder
                self._ratio_builder = CharEncodingRatioBuilder(viz_loader)
            except Exception:
                self._ratio_builder = None
        else:
            self._ratio_builder = None

        self._fallback_layout = QVBoxLayout(self)
        self._fallback_layout.setContentsMargins(0, 0, 0, 0)
        self._fallback_layout.setSpacing(4)

        # Minimap
        self._minimap = Tier3Minimap()
        self._fallback_layout.addWidget(self._minimap)

        # Hex view
        self._hex_view = Tier3HexView()
        self._fallback_layout.addWidget(self._hex_view, stretch=1)

        # ── Bidirectional sync ──────────────────────────────────
        self._minimap.viewport_changed.connect(self._on_minimap_drag)
        self._hex_view.visible_range_changed.connect(self._on_hex_scroll)
        self._sync_blocked: bool = False

    def _on_minimap_drag(self, offset: int) -> None:
        if self._sync_blocked:
            return
        if self._hex_view:
            self._sync_blocked = True
            self._hex_view.scroll_to_offset(offset)
            self._sync_blocked = False

    def _on_hex_scroll(self, start: int, end: int) -> None:
        if self._sync_blocked:
            return
        if self._minimap:
            self._sync_blocked = True
            self._minimap.set_viewport(start, end)
            self._sync_blocked = False

    # ── Navigation ─────────────────────────────────────────────────

    def navigate_to(self, byte_offset: int) -> None:
        self._current_offset = byte_offset

        if self._use_viz and self._canvas:
            self._navigate_viz(byte_offset)
        elif self._fallback_layout is not None and self._source_path:
            self._navigate_fallback(byte_offset)

    def _navigate_viz(self, byte_offset: int) -> None:
        idx = self._viz_loader.find_event_index_at_offset(byte_offset)
        if idx >= 0:
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

    def _navigate_fallback(self, byte_offset: int) -> None:
        """Load the entire block containing *byte_offset* and display in hex view.

        The hex view shows the full block from its start, with the minimap
        covering the same block range.  Scrolling is possible within the
        block; clicking a different Tier 2 block reloads with fresh data.
        """
        try:
            path = Path(self._source_path)
            if not path.is_file():
                return
            file_size = path.stat().st_size
            if file_size == 0:
                return

            # ── Clamp byte_offset to valid range ──
            byte_offset = max(0, min(byte_offset, file_size - 1))

            # ── Find the block containing byte_offset ──
            block_start = byte_offset
            block_end = min(byte_offset + 65536, file_size)  # fallback: 64 KiB window

            if self._ratio_builder is not None and self._ratio_builder.block_count > 0:
                for b in self._ratio_builder._blocks:
                    bs = b.input_start
                    be = b.input_start + b.input_bytes
                    if bs <= byte_offset < be:
                        block_start = bs
                        block_end = min(be, file_size)
                        break

            # ── Read entire block from source file ──
            with open(self._source_path, "rb") as f:
                f.seek(block_start)
                raw = f.read(block_end - block_start)

            # ── Compute per-char encoding ratios ──
            if self._ratio_builder is not None and self._ratio_builder.has_viz:
                ratios = self._ratio_builder.build_range(block_start, block_end)
            elif self._fallback_ratios and self._fallback_block_size > 0:
                # Use block-level compression ratios for per-byte coloring
                ratios = []
                for i in range(len(raw)):
                    block_idx = (block_start + i) // self._fallback_block_size
                    if block_idx < len(self._fallback_ratios):
                        ratios.append(self._fallback_ratios[block_idx])
                    else:
                        ratios.append(1.0)
            else:
                # No per-char data — use neutral mid-range (non-deflate, etc.)
                ratios = [0.5] * len(raw)

            # ── Minimap: down-sampled block panorama ──
            if self._ratio_builder is not None and self._ratio_builder.has_viz:
                minimap_ratios = self._ratio_builder.build_range_downsampled(
                    block_start, block_end, 2000)
            elif self._fallback_ratios and self._fallback_block_size > 0:
                # Downsample from block-level ratios
                total_bytes = block_end - block_start
                n_samples = min(2000, max(1, total_bytes))
                minimap_ratios = []
                for j in range(n_samples):
                    pos = block_start + j * total_bytes // n_samples
                    blk_idx = pos // self._fallback_block_size
                    if blk_idx < len(self._fallback_ratios):
                        minimap_ratios.append(self._fallback_ratios[blk_idx])
                    else:
                        minimap_ratios.append(1.0)
            else:
                minimap_ratios = [1.0] * min(2000, max(1, block_end - block_start))

            if self._minimap:
                self._minimap.set_data(minimap_ratios, block_start, block_end)

            # ── Hex view: full block data ──
            if self._hex_view:
                self._sync_blocked = True
                self._hex_view.set_data(raw, ratios, block_start)
                self._hex_view.scroll_to_offset(byte_offset)
                # Sync minimap viewport from actual visible range after scroll
                sb = self._hex_view.verticalScrollBar()
                vis_start, vis_end = self._hex_view._grid.visible_range(
                    sb.value(), self._hex_view.viewport().height())
                if self._minimap:
                    self._minimap.set_viewport(vis_start, vis_end)
                self._sync_blocked = False

        except Exception as e:
            logger.error("Tier3 fallback navigate: %s", e, exc_info=True)

    # ── paintEvent (placeholder for non-fallback) ──────────────────

    def paintEvent(self, event) -> None:
        if self._use_viz or self._fallback_layout is not None:
            return
        painter = QPainter(self)
        t = ThemeManager.get()
        painter.fillRect(self.rect(), QColor(t.bg_surface))
        painter.setPen(QColor(t.text_muted))
        fnt = painter.font()
        fnt.setPointSize(10)
        painter.setFont(fnt)
        painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                         "← 选择 chunk 查看局部热力图 (需要源文件)")


# ══════════════════════════════════════════════════════════════════════
# §7  ThreeTierHeatmapDialog
# ══════════════════════════════════════════════════════════════════════

class ThreeTierHeatmapDialog(QDialog):
    """3-tier compression heatmap dialog.

    Parameters:
        heat_path: Path to the .heat file (entropy data, mmap-read) — fallback.
        source_path: Original source file path (for Tier 3 text view).
        viz_path: Path to the .viz file (compression event data).
        compressed_size / original_size: For status display.
        folder_mode: True if aggregating multiple files.
    """

    def __init__(self, heat_path: str = "", source_path: str = "",
                 compressed_size: int = 0, original_size: int = 0,
                 viz_path: str = "",
                 parent=None, folder_mode: bool = False,
                 raw_data: bytes | None = None,
                 compressed_data: bytes | None = None,
                 algorithm: str = ""):
        super().__init__(parent)
        self._heat_path = heat_path
        self._source_path = source_path
        self._compressed_size = compressed_size
        self._original_size = original_size
        self._viz_path = viz_path
        self._folder_mode = folder_mode
        self._raw_data = raw_data
        self._compressed_data = compressed_data
        self._algorithm = algorithm
        self._huffman_trees: list = []
        self._controller: HeatmapController | None = None
        self._viz_loader: VizLoader | None = None
        self._macro: dict = {}
        self._files: list[dict] = []
        self._selected_idx: int = -1

        title = source_path or heat_path or "文件夹"
        self.setWindowTitle("压缩热力图 (3-Tier) — " + title)
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

        # ── Tier 1 ──
        t1_label = QLabel("Tier 1 — 全局文件条 (压缩率，点击选择，滚轮横向浏览)")
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

        # ── Tier 2 ──
        t2_label = QLabel("Tier 2 — Block 级压缩率条带 (点击色块浏览)")
        t2_label.setStyleSheet(self._tier_label_style(t))
        layout.addWidget(t2_label)

        self._tier2 = Tier2BlockStrip()
        self._tier2.block_selected.connect(self._on_block_selected)
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

        # ── Tier 3 ──
        t3_label = QLabel("Tier 3 — 字符级热力图 (Minimap + 文本视图 / LZ77 窗口)")
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

    # ── Data loading ──

    def _load_data(self) -> None:
        from gui.engine.heatmap_controller import HeatmapController

        has_heat = self._heat_path and Path(self._heat_path).is_file()
        has_viz = self._viz_path and Path(self._viz_path).is_file()

        if not has_heat and not has_viz and self._raw_data is not None:
            self._load_fallback_data()
            return

        if not has_heat:
            if self._raw_data is not None:
                self._load_fallback_data()
                return
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.warning(
                self, "热力图错误",
                "没有可用的 .heat / .viz 文件，无法显示热力图。"
            )
            self.reject()
            return

        # Try loading .viz first (primary data source for compression ratio)
        if self._viz_path and Path(self._viz_path).is_file():
            try:
                from gui.engine.viz_loader import VizLoader
                self._viz_loader = VizLoader(self._viz_path)
            except Exception as e:
                logger.warning("[3tier] viz load failed: %s", e)

        # Load .heat
        self._controller = HeatmapController(self._heat_path)
        self._macro = self._controller.macro_data
        self._files = self._macro.get("files", [])

        # ── Augment files with compression ratio from .viz ──
        if self._viz_loader is not None:
            try:
                from gui.engine.char_encoding_ratio import CharEncodingRatioBuilder
                ratio_builder = CharEncodingRatioBuilder(self._viz_loader)
                for f in self._files:
                    # For single-file mode, all blocks belong to this file
                    f["compression_ratio"] = ratio_builder.overall_compression_ratio()
                    f["_ratio_builder"] = ratio_builder
            except Exception as e:
                logger.warning("[3tier] ratio builder failed: %s", e)

        # Override file names with original source filename (not hash-prefixed .heat name)
        if self._files and self._source_path:
            orig_name = Path(self._source_path).stem
            for f in self._files:
                f["name"] = orig_name

        # If no .viz, use fallback compression ratio from .heat or dialog params
        for f in self._files:
            if "compression_ratio" not in f:
                ratios = f.get("ratio_array", [])
                if ratios:
                    f["compression_ratio"] = sum(ratios) / len(ratios)
                elif self._original_size > 0:
                    f["compression_ratio"] = self._compressed_size / self._original_size
                else:
                    f["compression_ratio"] = 1.0

        total = self._macro.get("total_input_bytes", 0)
        self._tier1.set_data(self._files, total)
        if self._files:
            self._select_file(0)

    def _load_fallback_data(self) -> None:
        """Fallback mode: compute block compression ratios in real-time,
        and for Deflate/DPFlate, extract Huffman trees from compressed data."""
        raw = self._raw_data
        if not raw:
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.warning(self, "热力图错误", "没有原始数据")
            self.reject()
            return

        from gui.engine.compressor import CompressionEngine
        from gui.ade.explorer import SilentExplorer
        from gui.models import AlgorithmType

        algo_map = {e.value: e for e in AlgorithmType}
        algo = algo_map.get(self._algorithm, AlgorithmType.DEFLATE)

        engine = CompressionEngine()
        block_size = 256
        n_blocks = max(1, (len(raw) + block_size - 1) // block_size)
        blocks = []
        for i in range(n_blocks):
            start = i * block_size
            end = min(start + block_size, len(raw))
            block_raw = raw[start:end]
            with SilentExplorer.user_compression_priority():
                r = engine.compress(block_raw, algo)
            ratio = r.compressed_size / len(block_raw) if len(block_raw) > 0 else 1.0
            blocks.append({"start": start, "size": end - start, "ratio": round(ratio, 4)})

        ratios = [b["ratio"] for b in blocks]
        avg_ratio = sum(ratios) / len(ratios) if ratios else 1.0

        self._macro = {
            "total_input_bytes": len(raw),
            "chunk_bytes": block_size,
            "total_output_bytes": self._compressed_size if self._compressed_size > 0 else int(len(raw) * avg_ratio),
        }

        file_name = Path(self._source_path).stem if self._source_path else "文件"

        actual_ratio = (self._compressed_size / len(raw)
                         if self._compressed_size > 0 and len(raw) > 0
                         else avg_ratio)
        self._files = [{
            "name": file_name,
            "ratio_array": ratios,
            "compression_ratio": actual_ratio,
            "input_bytes": len(raw),
            "global_start": 0,
        }]

        # ── Parse tokens and Huffman trees ──
        from gui.engine.token_parser import parse_for_demo, can_parse
        if can_parse(algo):
            try:
                result = parse_for_demo(algo, raw)
                token_count = len(result.tokens)
                logger.info(
                    "[3tier-fallback] parsed %d tokens for %s",
                    token_count, algo.value,
                )
                self._files[0]["_token_count"] = token_count
                self._files[0]["_algorithm"] = algo.value
                self._files[0]["_parsed_tokens"] = result.tokens
                if result.huffman_trees:
                    self._huffman_trees = result.huffman_trees
                    logger.info(
                        "[3tier-fallback] extracted %d Huffman trees",
                        len(self._huffman_trees),
                    )
            except Exception as e:
                logger.warning("[3tier-fallback] token parse failed: %s", e)

        self._tier1.set_data(self._files, len(raw))
        if self._files:
            self._select_file(0)

    def _select_file(self, idx: int) -> None:
        if idx < 0 or idx >= len(self._files):
            return
        self._selected_idx = idx
        f = self._files[idx]

        # ── Lazy-load .heat for folder mode (skip if ratios already loaded) ──
        heat_path = f.get("heat_path")
        if heat_path and len(f.get("ratio_array", [])) <= 1 and len(f.get("entropy_array", [])) <= 1:
            self._lazy_load_file_entropy(idx, heat_path)

        self._tier1.select_index(idx)

        # ── Tier 2: use blocks from .viz, or fallback to .heat ratios ──
        ratio_builder = f.get("_ratio_builder")
        if ratio_builder is not None and ratio_builder.has_viz:
            blocks = ratio_builder.block_compression_ratios()
            self._tier2.set_blocks(
                blocks,
                global_start=f.get("global_start", 0),
                file_name=f.get("name", ""),
                total_input=ratio_builder.total_input_bytes,
            )
        else:
            chunk_bytes = self._macro.get("chunk_bytes", 1048576)
            ratios = f.get("ratio_array", [])
            if not ratios:
                # v2 fallback: approximate ratios from entropy
                entropy = f.get("entropy_array", [])
                ratios = [min(1.0, max(0.01, e / 8.0)) for e in entropy]
            self._tier2.set_ratios(
                ratios,
                chunk_bytes,
                global_start=f.get("global_start", 0),
                file_name=f.get("name", ""),
                input_bytes=f.get("input_bytes", 0),
            )

        # ── Tier 3: replace with viz widget ──
        while self._tier3_layout.count():
            item = self._tier3_layout.takeAt(0)
            if item and item.widget():
                item.widget().deleteLater()

        file_viz = f.get("viz_path", self._viz_path)
        file_source = f.get("source_path", self._source_path)

        # Load viz loader for this file (reuse or reload)
        file_viz_loader = None
        if ratio_builder is not None:
            file_viz_loader = ratio_builder._viz
        elif file_viz and Path(file_viz).is_file():
            try:
                from gui.engine.viz_loader import VizLoader
                file_viz_loader = VizLoader(file_viz)
            except Exception:
                pass

        self._tier3 = Tier3VizWidget(
            viz_loader=file_viz_loader,
            source_path=file_source or "",
            parent=self,
            fallback_ratios=f.get("ratio_array", None),
            fallback_block_size=self._macro.get("chunk_bytes", 256),
        )
        self._tier3_layout.addWidget(self._tier3)

        # ── Huffman tree visualization (for Deflate/DPFlate) ──
        if self._huffman_trees:
            try:
                from gui.ui.views.visualizers.huffman import HuffmanTreeCanvas
                from gui.config.theme import ThemeManager
                th = ThemeManager.get()
                huff_label = QLabel("Huffman 编码树")
                huff_label.setStyleSheet(
                    f"font-weight: bold; padding: 8px 4px 4px 4px; "
                    f"color: {th.text_primary}; background: transparent;"
                )
                self._tier3_layout.addWidget(huff_label)
                for ht in self._huffman_trees:
                    tree_label = QLabel(f"  {ht.tree_type}")
                    tree_label.setStyleSheet(
                        f"color: {th.text_secondary}; background: transparent; "
                        "font-size: 11px; padding: 0 4px;"
                    )
                    self._tier3_layout.addWidget(tree_label)
                    canvas = HuffmanTreeCanvas(ht)
                    canvas.setMinimumHeight(200)
                    self._tier3_layout.addWidget(canvas)
            except Exception as e:
                logger.warning("[3tier] Huffman display failed: %s", e)

        # Navigate Tier 3 to slider position (convert global → file-local)
        global_offset = self._tier2.current_global_offset
        local_offset = global_offset - f.get("global_start", 0)
        self._tier3.navigate_to(local_offset)

        # Status
        if file_viz and Path(file_viz).is_file():
            self._status_lbl.setText(
                f"文件: {f.get('name', '')}  |  "
                f"blocks: {ratio_builder.block_count if ratio_builder else 'N/A'}  |  "
                f"点击 Tier 2 色块浏览内容"
            )
        else:
            is_v3 = f.get("is_v3", False)
            source = "压缩率 (v3)" if is_v3 else "压缩率 (熵近似)"
            self._status_lbl.setText(
                f"文件: {f.get('name', '')}  |  "
                f"chunks: {f.get('chunk_count', 0)}  |  "
                f"{source}"
            )

    def _lazy_load_file_entropy(self, idx: int, heat_path: str) -> None:
        from gui.engine.heatmap_controller import HeatmapController

        if self._controller is not None:
            self._controller.close()
            self._controller = None

        try:
            self._controller = HeatmapController(heat_path)
            f = self._files[idx]
            f["entropy_array"] = self._controller.entropy_array
            f["ratio_array"] = self._controller.ratio_array
            f["input_bytes"] = self._controller.total_input  # v3: actual; v2: estimated
            f["chunk_count"] = self._controller.total_chunks
            f["avg_entropy"] = self._controller.avg_entropy
            f["is_v3"] = self._controller.is_v3
            if self._macro.get("chunk_bytes", 0) == 0:
                self._macro["chunk_bytes"] = self._controller.chunk_bytes
        except Exception as e:
            logger.error("[3tier] lazy-load heat failed: %s — %s", heat_path, e)

    # ── Signals ──

    def _on_file_selected(self, idx: int) -> None:
        self._select_file(idx)

    def _on_block_selected(self, global_offset: int) -> None:
        if hasattr(self, '_tier3') and self._tier3 is not None:
            # Convert folder-global offset to file-local offset
            local_offset = global_offset
            if 0 <= self._selected_idx < len(self._files):
                local_offset = global_offset - self._files[self._selected_idx].get("global_start", 0)
            self._tier3.navigate_to(local_offset)

    # ── Lifecycle ──

    def closeEvent(self, event):
        if self._controller is not None:
            self._controller.close()
            self._controller = None
        if self._viz_loader is not None:
            self._viz_loader.close()
            self._viz_loader = None
        super().closeEvent(event)

    # ── Factory ──────────────────────────────────────────────────

    @classmethod
    def from_folder(cls, folder_record, parent=None) -> "ThreeTierHeatmapDialog":
        from gui.models import FolderRecord as FR
        if not isinstance(folder_record, FR):
            raise TypeError("from_folder requires a FolderRecord")

        folder_record.ensure_files_loaded()

        _V2_HEADER_FMT = struct.Struct("<I I I I f")
        _V3_HEADER_FMT = struct.Struct("<I I I I Q Q")
        _CHUNK_FMT = struct.Struct("<f")
        files: list[dict] = []
        total_input = 0
        chunk_bytes = 0

        for fr in folder_record.files:
            heat_path = getattr(fr, "heat_path", None)
            if not heat_path or not Path(heat_path).is_file():
                continue

            try:
                with open(heat_path, "rb") as hf:
                    raw = hf.read()
                if len(raw) < 20:
                    continue
                magic, version, total_chunks, cb = struct.unpack("<I I I I", raw[:16])
                if magic != 0x54414548:
                    continue

                if version == 3 and len(raw) >= 28:
                    _, _, _, _, tot_in, tot_out = _V3_HEADER_FMT.unpack(raw[:28])
                    input_bytes = tot_in
                    data_off = 28
                    # Read per-chunk ratios from mmap-like data
                    ratios: list[float] = []
                    for ci in range(total_chunks):
                        off = data_off + ci * 4
                        if off + 4 <= len(raw):
                            ratios.append(_CHUNK_FMT.unpack(raw[off:off + 4])[0])
                    comp_ratio = tot_out / max(tot_in, 1)
                    avg_e = 0.0
                    has_v3 = True
                else:
                    # v2
                    avg_e = struct.unpack("<f", raw[16:20])[0]
                    input_bytes = cb * total_chunks
                    ratios = []
                    # Approximate ratios from entropy
                    for ci in range(total_chunks):
                        off = 20 + ci * 4
                        if off + 4 <= len(raw):
                            e = _CHUNK_FMT.unpack(raw[off:off + 4])[0]
                            ratios.append(min(1.0, max(0.01, e / 8.0)))
                    comp_ratio = getattr(fr, "compression_ratio", 1.0)
                    if comp_ratio <= 0 or comp_ratio > 1.0:
                        comp_ratio = 1.0
                    has_v3 = False

                # Try to load viz for this file
                viz_path = getattr(fr, "viz_path", "")
                ratio_builder = None
                if viz_path and Path(viz_path).is_file():
                    try:
                        from gui.engine.viz_loader import VizLoader
                        from gui.engine.char_encoding_ratio import CharEncodingRatioBuilder
                        vl = VizLoader(viz_path)
                        ratio_builder = CharEncodingRatioBuilder(vl)
                        comp_ratio = ratio_builder.overall_compression_ratio()
                    except Exception:
                        pass

                files.append({
                    "name": fr.name,
                    "heat_path": heat_path,
                    "source_path": getattr(fr, "path", ""),
                    "viz_path": viz_path,
                    "global_start": total_input,
                    "global_end": total_input + input_bytes,
                    "input_bytes": input_bytes,
                    "chunk_count": total_chunks,
                    "entropy_array": [],
                    "ratio_array": ratios,
                    "avg_entropy": avg_e,
                    "compression_ratio": comp_ratio,
                    "_ratio_builder": ratio_builder,
                    "is_v3": has_v3,
                })
                total_input += input_bytes
                if chunk_bytes == 0:
                    chunk_bytes = cb
            except Exception as e:
                logger.warning("[3tier] skip heat header: %s — %s", heat_path, e)
                continue

        if not files:
            files = [{
                "name": folder_record.name,
                "global_start": 0,
                "global_end": folder_record.size,
                "input_bytes": folder_record.size,
                "chunk_count": 1,
                "entropy_array": [],
                "ratio_array": [folder_record.compression_ratio if folder_record.total_original > 0 else 1.0],
                "compression_ratio": folder_record.compression_ratio if folder_record.total_original > 0 else 1.0,
            }]

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
