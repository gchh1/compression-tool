"""Token-level compression heatmap (text highlights + info panel)."""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QColor, QFont, QTextCharFormat, QTextCursor, QBrush
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QLabel, QPlainTextEdit

from gui.config.theme import ThemeManager
from gui.engine.token_parser import Token, TokenType


def ratio_to_qcolor(ratio: float) -> QColor:
    """压缩率 → 色相：好(低)偏绿，差(高)偏红。使用主题 heatmap_low→mid→high 插值。"""
    t = max(0.0, min(1.0, ratio / 1.5))
    low = ThemeManager.resolve_color("heatmap_low")
    mid = ThemeManager.resolve_color("heatmap_mid")
    high = ThemeManager.resolve_color("heatmap_high")
    if t <= 0.5:
        f = t / 0.5
        r = int(low.red() + (mid.red() - low.red()) * f)
        g = int(low.green() + (mid.green() - low.green()) * f)
        b = int(low.blue() + (mid.blue() - low.blue()) * f)
    else:
        f = (t - 0.5) / 0.5
        r = int(mid.red() + (high.red() - mid.red()) * f)
        g = int(mid.green() + (high.green() - mid.green()) * f)
        b = int(mid.blue() + (high.blue() - mid.blue()) * f)
    return QColor(r, g, b)


def ratio_to_gray(ratio: float) -> QColor:
    """压缩率 → 灰度：使用主题色插值（色盲友好模式）。"""
    t = max(0.0, min(1.0, ratio / 1.5))
    light = ThemeManager.resolve_color("text_primary")
    dark = ThemeManager.resolve_color("bg_primary")
    return QColor(
        int(light.red() + (dark.red() - light.red()) * t),
        int(light.green() + (dark.green() - light.green()) * t),
        int(light.blue() + (dark.blue() - light.blue()) * t),
    )


class TokenHeatmapWidget(QPlainTextEdit):
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


class TokenInfoPanel(QWidget):
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
