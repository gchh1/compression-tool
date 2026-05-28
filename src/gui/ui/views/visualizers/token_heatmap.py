"""Token-level compression heatmap (text highlights + info panel)."""

from __future__ import annotations

import colorsys

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QColor, QFont, QTextCharFormat, QTextCursor, QBrush
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QLabel, QPlainTextEdit

from gui.config.theme import ThemeManager
from gui.engine.token_parser import Token, TokenType


def ratio_to_qcolor(ratio: float) -> QColor:
    """压缩率(压缩字节/原始字节) → 色相：好(低)偏绿，差(高)偏红。LZ 演示/热力图/算法对比条共用。"""
    r = max(0.0, min(ratio, 1.5))
    hue = max(0.0, 120.0 * (1.0 - r / 1.5))
    sat = 0.85 - 0.15 * (r / 1.5)
    val = 0.95 - 0.25 * (r / 1.5)
    rgb = colorsys.hsv_to_rgb(hue / 360.0, sat, val)
    return QColor(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255))


def ratio_to_gray(ratio: float) -> QColor:
    r = max(0.0, min(ratio, 1.5))
    v = int(240 - 180 * (r / 1.5))
    return QColor(v, v, v)


class TokenHeatmapWidget(QPlainTextEdit):# 负责文本显示和高亮
    token_hovered = pyqtSignal(int)

    def __init__(self, parent=None, grayscale: bool = False):
        super().__init__(parent)
        self._grayscale = grayscale
        self.setReadOnly(True)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.WidgetWidth)
        font = QFont("Consolas", 10)
        font.setStyleHint(QFont.StyleHint.Monospace)
        self.setFont(font)
        self._tokens: list[Token] = []
        self._char_to_token: list[int] = []
        self._byte_to_char: list[int] = []
        self._text: str = ""
        self._hovered_idx: int = -1
        self.setMouseTracking(True)
        self.viewport().setMouseTracking(True)

    def set_data(self, text: str, tokens: list[Token], byte_to_char: list[int]) -> None:
        self._text = text
        self._tokens = tokens
        self._byte_to_char = byte_to_char
        self._char_to_token = [-1] * len(text)
        for i, t in enumerate(tokens):
            for c in range(t.original_start, min(t.original_start + t.original_length, len(byte_to_char))):
                char_idx = byte_to_char[c] if c < len(byte_to_char) else c
                if 0 <= char_idx < len(self._char_to_token):
                    self._char_to_token[char_idx] = i
        self.setPlainText(text)
        self._apply_highlights()

    def _apply_highlights(self) -> None:
        cursor = self.textCursor()
        cursor.select(QTextCursor.SelectionType.Document)
        fmt_default = QTextCharFormat()
        cursor.setCharFormat(fmt_default)

        for i, t in enumerate(self._tokens):
            bs = t.original_start
            be = min(bs + t.original_length, len(self._byte_to_char))
            cs = self._byte_to_char[bs] if bs < len(self._byte_to_char) else 0
            ce = self._byte_to_char[be] if be < len(self._byte_to_char) else len(self._text)
            if ce <= cs:
                continue

            color = ratio_to_gray(t.compression_ratio) if self._grayscale else ratio_to_qcolor(t.compression_ratio)
            bg = QColor(color)
            bg.setAlpha(100)

            fmt = QTextCharFormat()
            fmt.setBackground(QBrush(bg))

            cur = QTextCursor(self.document())
            cur.setPosition(cs)
            cur.setPosition(ce, QTextCursor.MoveMode.KeepAnchor)
            cur.setCharFormat(fmt)

    def mouseMoveEvent(self, event) -> None:
        super().mouseMoveEvent(event)
        pos = event.position().toPoint() if hasattr(event, 'position') else event.pos()
        cursor = self.cursorForPosition(pos)
        char_pos = cursor.position()
        if 0 <= char_pos < len(self._char_to_token):
            idx = self._char_to_token[char_pos]
            if idx != self._hovered_idx:
                self._hovered_idx = idx
                self.token_hovered.emit(idx)
        else:
            self._hovered_idx = -1
            self.token_hovered.emit(-1)


class TokenInfoPanel(QWidget): # 负责显示 Token 详情文本
    def __init__(self, parent=None, grayscale: bool = False):
        super().__init__(parent)
        self._grayscale = grayscale
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        self._label = QLabel("悬停文本查看 Token 详情")
        self._label.setWordWrap(True)
        self._label.setStyleSheet(
            f"color: {ThemeManager.hex('text_primary')}; font-size: 12px; font-family: Consolas, monospace;"
        )
        self._label.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(self._label)
        layout.addStretch()

    def show_token(self, token: Token | None, idx: int = -1) -> None:
        if token is None:
            self._label.setText("悬停文本查看 Token 详情")
            return
        type_label = {
            TokenType.LITERAL: "LITERAL",
            TokenType.LITERAL_RUN: "LITERAL_RUN",
            TokenType.MATCH: "MATCH",
        }.get(token.type, token.type.value)
        color = ratio_to_gray(token.compression_ratio) if self._grayscale else ratio_to_qcolor(token.compression_ratio)
        lines = [
            f"Token #{idx}   {type_label}",
            f"字符位置: {token.original_start} ~ {token.original_start + token.original_length - 1}",
            f"长度: {token.original_length} 字节",
            f"编码大小: {token.compressed_size:.2f} B",
            f"压缩率: {token.compression_ratio * 100:.1f}%",
        ]
        if token.type == TokenType.MATCH:
            lines.append(f"回退偏移: {token.match_offset} 字符")
        if token.huffman_bits > 0:
            lines.append(f"Huffman: {token.huffman_bits:.1f} bit | {token.huffman_detail}")
        self._label.setText("\n".join(lines))


class BitstreamWidget(QWidget): # 负责比特流热力图显示（每个比特一个小格子，颜色表示压缩率）
    TOOLTIP_REQUESTED = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._bits: list[int] = []
        self._colors: list[QColor] = []
        self._bit_count: int = 0
        self._hovered_bit: int = -1
        self._cell_w: int = 10
        self._cell_h: int = 20
        self._cols: int = 64
        self._gap_px: int = 4
        self.setMouseTracking(True)
        self.setMinimumHeight(60)

    def set_data(self, bitstream_bytes: bytes, tokens: list[Token]) -> None:
        total_bits = len(bitstream_bytes) * 8
        self._bits = []
        self._colors = []
        self._bit_count = total_bits
        for b in bitstream_bytes:
            for bit_i in range(7, -1, -1):
                self._bits.append((b >> bit_i) & 1)

        self._colors = [QColor(220, 220, 220) for _ in range(total_bits)]
        for t in tokens:
            if t.bit_length <= 0:
                continue
            color = ratio_to_qcolor(t.compression_ratio)
            color.setAlpha(180)
            for bi in range(t.bit_offset, min(t.bit_offset + t.bit_length, total_bits)):
                self._colors[bi] = color

        self._update_size()

    def _update_size(self) -> None:
        rows = max(1, (self._bit_count + self._cols - 1) // self._cols)
        byte_gaps = (self._cols // 8) * self._gap_px
        w = self._cols * self._cell_w + byte_gaps + 2
        h = rows * (self._cell_h + 1) + 2
        self.setMinimumSize(w, h)
        self.setFixedSize(w, h)
        self.update()

    def paintEvent(self, event) -> None:
        from PyQt6.QtGui import QPainter

        if self._bit_count == 0:
            return
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        rows = (self._bit_count + self._cols - 1) // self._cols
        for row in range(rows):
            y = row * (self._cell_h + 1) + 1
            for col in range(self._cols):
                bi = row * self._cols + col
                if bi >= self._bit_count:
                    continue
                gap_count = col // 8
                x = col * self._cell_w + gap_count * self._gap_px + 1
                p.fillRect(int(x), int(y), int(self._cell_w), int(self._cell_h), self._colors[bi])
                val = self._bits[bi]
                p.setPen(QColor(40, 40, 40))
                from PyQt6.QtGui import QFont
                font = QFont("Consolas", 7)
                p.setFont(font)
                p.drawText(int(x), int(y), int(self._cell_w), int(self._cell_h),
                           Qt.AlignmentFlag.AlignCenter, str(val))
        p.end()

    def _bit_at(self, pos) -> int:
        px = pos.x()
        py = pos.y()
        for row in range((self._bit_count + self._cols - 1) // self._cols):
            y = row * (self._cell_h + 1) + 1
            if py < y or py > y + self._cell_h:
                continue
            for col in range(self._cols):
                bi = row * self._cols + col
                if bi >= self._bit_count:
                    return -1
                gap_count = col // 8
                x = col * self._cell_w + gap_count * self._gap_px + 1
                if x <= px <= x + self._cell_w:
                    return bi
        return -1

    def mouseMoveEvent(self, event) -> None:
        bi = self._bit_at(event.pos())
        if bi != self._hovered_bit:
            self._hovered_bit = bi
            self.TOOLTIP_REQUESTED.emit(bi)
            self.setToolTip(self._tooltip_for(bi))

    def _tooltip_for(self, bi: int) -> str:
        if bi < 0:
            return ""
        byte_idx = bi // 8
        bit_in_byte = 7 - (bi % 8)
        byte_val = 0
        if byte_idx < len(self._bits) // 8:
            for i in range(8):
                bit_pos = byte_idx * 8 + i
                if bit_pos < len(self._bits):
                    byte_val = (byte_val << 1) | self._bits[bit_pos]
        return f"Bit {bi}  Byte {byte_idx}  bit#{bit_in_byte}  hex=0x{byte_val:02X}"
