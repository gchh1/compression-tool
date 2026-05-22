"""Sliding-window canvas for LZ77-family algorithm visualization.

Driven by MatchEvent data from .viz files — shows the search buffer,
lookahead buffer, and current match/literal in a scrollable, zoomable view.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRect, QRectF, QSize, pyqtSignal
from PyQt6.QtGui import (
    QPainter, QColor, QFont, QPen, QBrush, QFontMetrics,
    QWheelEvent, QMouseEvent,
)
from PyQt6.QtWidgets import QWidget, QScrollBar

from gui.config.theme import ThemeManager


# ── constants ──────────────────────────────────────────────────────

CELL_W = 32          # base cell width in pixels (wider for hex+char)
CELL_H = 42          # cell height (taller for 2-line display)
BYTES_PER_CELL = 1   # each cell = 1 byte
MIN_ZOOM = 0.25
MAX_ZOOM = 4.0
ZOOM_STEP = 0.1

# Colors
SEARCH_BG = QColor(30, 41, 59)        # dark blue-grey
LOOKAHEAD_BG = QColor(15, 23, 42)     # darker
MATCH_BG = QColor(34, 197, 94, 120)    # green with alpha
LITERAL_BG = QColor(100, 116, 139, 60) # grey with alpha
CURSOR_LINE = QColor(239, 68, 68)      # red divider
HIGHLIGHT = QColor(250, 204, 21, 100)  # yellow for current match
TEXT_COLOR = QColor(248, 250, 252)
DIM_TEXT = QColor(148, 163, 184)
HEADER_BG = QColor(51, 65, 85)


class WindowCanvas(QWidget):
    """Interactive sliding window visualization for LZ77 compression.

    Parameters
    ----------
    window_size : int
        Maximum search-buffer size (the "dictionary").
    lookahead_size : int
        Maximum lookahead-buffer size.
    """

    # emitted when user clicks a byte cell
    byte_clicked = pyqtSignal(int)   # absolute byte position
    step_changed = pyqtSignal(int)   # current match-event index

    def __init__(
        self,
        window_size: int = 32768,
        lookahead_size: int = 258,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._window_size = window_size
        self._lookahead_size = lookahead_size

        # Data
        self._match_events: list = []          # list of MatchEvent (may be a page subset)
        self._total_bytes: int = 0             # total input size
        self._current_idx: int = -1            # current event index (absolute)
        self._event_offset: int = 0            # absolute index of _match_events[0]
        self._total_event_count: int = 0       # total across all pages
        self._raw_bytes: bytes = b""           # raw input (if loaded)
        self._source_path: str = ""            # source file for lazy byte reads

        # View state
        self._zoom: float = 1.0
        self._scroll_offset: int = 0           # first visible byte position
        self._cell_w: int = CELL_W
        self._cell_h: int = CELL_H
        self._cols: int = 16                   # bytes per row in hex view

        self._hover_pos: int = -1

        self.setMinimumHeight(200)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

        self._scrollbar: QScrollBar | None = None

    # ── public API ──────────────────────────────────────────────────

    def set_data(
        self,
        match_events: list,
        total_bytes: int,
        raw_bytes: bytes = b"",
    ) -> None:
        """Load compression trace data (all events at once)."""
        self._match_events = match_events
        self._total_bytes = total_bytes
        self._raw_bytes = raw_bytes
        self._current_idx = -1
        self._event_offset = 0
        self._total_event_count = len(match_events)
        self._scroll_offset = 0
        self._update_scrollbar()
        self.update()

    def set_events_page(
        self,
        offset: int,
        events: list,
        total_event_count: int,
        total_bytes: int,
        source_path: str = "",
    ) -> None:
        """Load a page of events starting at absolute index *offset*."""
        self._match_events = events
        self._event_offset = offset
        self._total_event_count = total_event_count
        if total_bytes != self._total_bytes and self._scroll_offset > total_bytes:
            self._scroll_offset = max(0, total_bytes - self._visible_bytes())
        self._total_bytes = total_bytes
        if source_path:
            self._source_path = source_path
        self.update()

    def event_count(self) -> int:
        """Total number of match events across all pages."""
        return self._total_event_count

    def set_scrollbar(self, sb: QScrollBar) -> None:
        """Attach an external scrollbar for synchronized scrolling."""
        self._scrollbar = sb
        sb.setRange(0, max(0, self._total_bytes - 1))
        sb.setPageStep(max(1, self._visible_bytes()))
        sb.valueChanged.connect(self._on_scrollbar_changed)

    def set_step(self, idx: int) -> None:
        """Jump to a specific absolute match-event index."""
        if 0 <= idx < self._total_event_count:
            self._current_idx = idx
            local_idx = idx - self._event_offset
            if 0 <= local_idx < len(self._match_events):
                ev = self._match_events[local_idx]
                vis = self._visible_bytes()
                self._scroll_offset = max(0, ev.input_pos - vis // 2)
                self._update_scrollbar()
                self.update()

    # ── internal helpers ────────────────────────────────────────────

    def _visible_bytes(self) -> int:
        """How many bytes fit in the current viewport."""
        w = self.width()
        return max(1, (w // max(1, self._cell_w)) * self._cols)

    def _update_scrollbar(self) -> None:
        if self._scrollbar is not None:
            self._scrollbar.blockSignals(True)
            self._scrollbar.setRange(0, max(0, self._total_bytes - 1))
            self._scrollbar.setPageStep(max(1, self._visible_bytes()))
            self._scrollbar.setValue(self._scroll_offset)
            self._scrollbar.blockSignals(False)

    def _on_scrollbar_changed(self, value: int) -> None:
        self._scroll_offset = value
        self.update()

    def _byte_at(self, pos: int) -> int | None:
        """Get byte at absolute position, using source file or in-memory data."""
        if 0 <= pos < len(self._raw_bytes):
            return self._raw_bytes[pos]
        if self._source_path and 0 <= pos < self._total_bytes:
            try:
                with open(self._source_path, "rb") as fh:
                    fh.seek(pos)
                    return fh.read(1)[0]
            except (OSError, IndexError):
                return None
        return None

    def _read_source_range(self, start: int, count: int) -> bytes:
        """Read a contiguous byte range from the source file (lazy)."""
        if not self._source_path:
            return b""
        if count <= 0:
            return b""
        try:
            with open(self._source_path, "rb") as fh:
                fh.seek(start)
                return fh.read(count)
        except OSError:
            return b""

    # ── events ──────────────────────────────────────────────────────

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        self._update_scrollbar()

    def wheelEvent(self, event: QWheelEvent) -> None:
        if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            # Zoom
            delta = event.angleDelta().y()
            if delta > 0:
                self._zoom = min(MAX_ZOOM, self._zoom + ZOOM_STEP)
            else:
                self._zoom = max(MIN_ZOOM, self._zoom - ZOOM_STEP)
            self._cell_w = max(4, int(CELL_W * self._zoom))
            self._cell_h = max(6, int(CELL_H * self._zoom))
            self._update_scrollbar()
            self.update()
        else:
            # Scroll vertically within the event list
            if self._scrollbar is not None:
                delta = event.angleDelta().y()
                cur = self._scrollbar.value()
                step = self._scrollbar.pageStep() // 4
                self._scrollbar.setValue(cur - delta // 120 * step)
            else:
                delta = event.angleDelta().y()
                self._scroll_offset = max(
                    0, self._scroll_offset - delta // 120 * self._cols
                )
                self.update()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        pos = event.pos()
        bp = self._pixel_to_byte(pos.x(), pos.y())
        if bp != self._hover_pos:
            self._hover_pos = bp
            self.update()

    def leaveEvent(self, event) -> None:
        self._hover_pos = -1
        self.update()

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            bp = self._pixel_to_byte(
                event.pos().x(), event.pos().y()
            )
            if bp >= 0:
                self.byte_clicked.emit(bp)
                # Find nearest match event
                for local_i, ev in enumerate(self._match_events):
                    if ev.input_pos >= bp:
                        abs_i = local_i + self._event_offset
                        self.set_step(abs_i)
                        self.step_changed.emit(abs_i)
                        break

    def _pixel_to_byte(self, px: int, py: int) -> int:
        """Convert pixel coordinates to absolute byte position."""
        margin = 80  # label area
        if px < margin:
            return -1
        col = (px - margin) // self._cell_w
        if col >= self._cols:
            return -1
        row = py // self._cell_h
        return self._scroll_offset + row * self._cols + col

    # ── painting ────────────────────────────────────────────────────

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        painter.fillRect(self.rect(), ThemeManager.color('bg_surface'))

        if not self._match_events:
            painter.setPen(ThemeManager.color('text_muted'))
            painter.drawText(
                self.rect(), Qt.AlignmentFlag.AlignCenter,
                "无匹配数据 — 请先压缩文件以生成 .viz"
            )
            return

        self._draw_window_view(painter)

    def _draw_window_view(self, painter: QPainter) -> None:
        """Draw the sliding window with search/lookahead regions."""
        w = self.width()
        h = self.height()
        margin = 80
        cols = self._cols

        # Current event determines the window position
        curr_ev = None
        local_idx = self._current_idx - self._event_offset
        if 0 <= local_idx < len(self._match_events):
            curr_ev = self._match_events[local_idx]

        # Draw header bar with position info
        self._draw_header(painter, w, curr_ev)

        # Calculate visible range (clamped to total_bytes)
        vis_start = min((self._scroll_offset // cols) * cols, max(0, self._total_bytes - 1))
        vis_end = min(self._total_bytes, vis_start + self._visible_bytes())
        if vis_end <= vis_start:
            vis_end = min(self._total_bytes, vis_start + max(1, self._visible_bytes()))

        # Build a set of match-covered positions from events
        match_positions: set[int] = set()
        match_info: dict[int, tuple[int, int]] = {}  # pos -> (offset, length)
        for ev in self._match_events:
            if ev.offset > 0:
                for i in range(ev.length):
                    p = ev.input_pos + i
                    match_positions.add(p)
                    match_info[p] = (ev.offset, ev.length)

        # Batch-read visible byte range from source file (one seek+read)
        _source_slice = self._read_source_range(vis_start, vis_end - vis_start)

        # Draw cells
        hex_font = QFont("Consolas", max(7, int(8 * self._zoom)))
        char_font = QFont("Consolas", max(8, int(9 * self._zoom)))
        label_font = QFont("Consolas", max(7, int(8 * self._zoom)))

        for i in range(vis_start, vis_end):
            byte_pos = i
            local = byte_pos - vis_start
            row = local // cols
            col = local % cols

            cx = margin + col * self._cell_w
            cy = 28 + row * self._cell_h

            if cx + self._cell_w > w or cy + self._cell_h > h:
                continue

            # Determine cell color
            is_match = byte_pos in match_positions
            is_current = False
            is_search = False
            is_lookahead = False

            if curr_ev:
                cursor = curr_ev.input_pos
                search_start = max(0, cursor - self._window_size)
                is_search = search_start <= byte_pos < cursor
                is_lookahead = cursor <= byte_pos < cursor + self._lookahead_size
                if curr_ev.offset > 0:
                    is_current = (
                        byte_pos >= cursor
                        and byte_pos < cursor + curr_ev.length
                    )
                else:
                    is_current = byte_pos == cursor

            # Draw cell background
            rect = QRect(cx + 1, cy + 1, self._cell_w - 2, self._cell_h - 2)
            if is_current:
                painter.fillRect(rect, HIGHLIGHT)
            elif is_match:
                painter.fillRect(rect, MATCH_BG)
            elif is_lookahead:
                painter.fillRect(rect, LOOKAHEAD_BG)
            elif is_search:
                painter.fillRect(rect, SEARCH_BG)
            else:
                painter.fillRect(rect, SEARCH_BG if byte_pos < vis_start + self._window_size else LOOKAHEAD_BG)

            # Draw cell border
            painter.setPen(QPen(QColor(71, 85, 105), 0.5))
            painter.drawRect(QRect(cx, cy, self._cell_w, self._cell_h))

            # Draw hex (top) + ASCII (bottom)
            b = None
            idx_in_slice = byte_pos - vis_start
            if 0 <= idx_in_slice < len(_source_slice):
                b = _source_slice[idx_in_slice]

            if b is not None:
                if is_current:
                    painter.setPen(QColor(15, 23, 42))
                elif is_match:
                    painter.setPen(QColor(34, 197, 94))
                else:
                    painter.setPen(TEXT_COLOR)

                # Top: hex value
                painter.setFont(hex_font)
                hex_rect = QRect(cx, cy + 2, self._cell_w, self._cell_h // 2)
                painter.drawText(hex_rect, Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignBottom,
                                 f"{b:02X}")

                # Bottom: ASCII char
                painter.setFont(char_font)
                ch = chr(b) if 32 <= b < 127 else "·"
                char_rect = QRect(cx, cy + self._cell_h // 2, self._cell_w, self._cell_h // 2)
                painter.drawText(char_rect, Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop, ch)

            # Hover highlight
            if byte_pos == self._hover_pos:
                painter.setPen(QPen(ThemeManager.color('accent'), 1.5))
                painter.drawRect(QRect(cx, cy, self._cell_w, self._cell_h))

        # Draw row labels (byte offsets)
        painter.setFont(label_font)
        painter.setPen(DIM_TEXT)
        for row in range(0, (vis_end - vis_start + cols - 1) // cols):
            label = f"{vis_start + row * cols:08X}"
            painter.drawText(4, 28 + row * self._cell_h + self._cell_h // 2 + 4, label)

        # Draw cursor line (red vertical divider at search/lookahead boundary)
        if curr_ev:
            cursor = curr_ev.input_pos
            local = cursor - vis_start
            if 0 <= local < vis_end - vis_start:
                row = local // cols
                col = local % cols
                cx = margin + col * self._cell_w
                painter.setPen(QPen(CURSOR_LINE, 2))
                painter.drawLine(cx, 28, cx, 28 + row * self._cell_h + self._cell_h)

    def _draw_header(
        self, painter: QPainter, w: int, curr_ev
    ) -> None:
        """Draw the top information bar."""
        header_h = 26
        painter.fillRect(0, 0, w, header_h, HEADER_BG)

        painter.setFont(QFont("Consolas", 9))
        if curr_ev:
            painter.setPen(TEXT_COLOR)
            if curr_ev.offset > 0:
                text = (
                    f"pos={curr_ev.input_pos:08X}  "
                    f"match: offset={curr_ev.offset}  length={curr_ev.length}"
                )
            else:
                b = curr_ev.literal
                ch = chr(b) if 32 <= b < 127 else f"\\x{b:02X}"
                text = f"pos={curr_ev.input_pos:08X}  literal: {ch}"
            painter.drawText(8, header_h - 6, text)
        else:
            painter.setPen(DIM_TEXT)
            painter.drawText(8, header_h - 6, "滑动窗口 — 使用滚轮/滑块浏览")

        # Step counter on the right
        total = max(self._total_event_count, len(self._match_events))
        if total > 0:
            step_text = (
                f"步骤 {self._current_idx + 1}/{total}"
                if self._current_idx >= 0
                else f"共 {total} 步"
            )
            painter.setPen(DIM_TEXT)
            fm = QFontMetrics(painter.font())
            tw = fm.horizontalAdvance(step_text)
            painter.drawText(w - tw - 12, header_h - 6, step_text)

    def sizeHint(self) -> QSize:
        return QSize(800, 400)
