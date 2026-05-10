from __future__ import annotations

import logging
from dataclasses import dataclass

from PyQt6.QtCore import Qt, QRect, QRectF, QSize, pyqtSignal
from PyQt6.QtGui import (
    QPainter, QColor, QFont, QPen, QTextCharFormat, QTextCursor,
    QSyntaxHighlighter, QTextDocument, QBrush, QPixmap, QIcon,
)
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QPlainTextEdit, QTextEdit, QSlider, QScrollArea, QDialog,
    QSplitter, QFrame, QSizePolicy, QToolBar, QGroupBox,
    QTableWidget, QTableWidgetItem, QAbstractItemView, QProgressBar,
    QComboBox, QTabWidget,
)

logger = logging.getLogger(__name__)

from gui.core.token_parser import Token, TokenType, ParseResult, get_parser, can_parse
from gui.core.models import AlgorithmType
from gui.core.theme import ThemeManager
from gui.widgets.label_utils import create_styled_label


def _ratio_to_qcolor(ratio: float) -> QColor:
    import colorsys
    r = max(0.0, min(ratio, 1.5))
    hue = max(0.0, 120.0 * (1.0 - r / 1.5))
    sat = 0.85 - 0.15 * (r / 1.5)
    val = 0.95 - 0.25 * (r / 1.5)
    rgb = colorsys.hsv_to_rgb(hue / 360.0, sat, val)
    return QColor(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255))


def _ratio_to_gray(ratio: float) -> QColor:
    r = max(0.0, min(ratio, 1.5))
    v = int(240 - 180 * (r / 1.5))
    return QColor(v, v, v)


def _code_length_to_color(code_length: int, max_cl: int = 15) -> QColor:
    import colorsys
    t = min(code_length / max(max_cl, 10), 1.0)
    hue = max(0.0, 120.0 * (1.0 - t))
    sat = 0.80 - 0.15 * t
    val = 0.90 - 0.20 * t
    rgb = colorsys.hsv_to_rgb(hue / 360.0, sat, val)
    return QColor(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255))


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

            color = _ratio_to_qcolor(t.compression_ratio)
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
        self._label.setStyleSheet(f"color: {ThemeManager.hex('text_primary')}; font-size: 12px; font-family: Consolas, monospace;")
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
        color = _ratio_to_qcolor(token.compression_ratio)
        color_name = color.name()
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


class HeatmapDialog(QDialog):
    def __init__(self, text: str, tokens: list[Token], byte_to_char: list[int],
                 filename: str, algorithm: str, stats: dict,
                 huffman_trees=None, parent=None):
        super().__init__(parent)
        self._huffman_trees = huffman_trees
        self.setWindowTitle(f"压缩热力图 - {filename}")
        self.resize(1200, 800)
        self._setup_ui(text, tokens, byte_to_char, filename, algorithm, stats)

    def _setup_ui(self, text, tokens, byte_to_char, filename, algorithm, stats) -> None:
        try:
            self.setStyleSheet(ThemeManager.full_dialog_sheet())

            layout = QVBoxLayout(self)

            header = QLabel(f"📊 压缩热力图文件: {filename} | 算法: {algorithm}")
            header.setTextFormat(Qt.TextFormat.RichText)
            layout.addWidget(header)

            stats_layout = QHBoxLayout()
            stat_items = [
                ("原始大小", f"{stats.get('original_size', 0):,} B"),
                ("压缩后", f"{stats.get('compressed_size', 0):,} B"),
                ("压缩率", f"{stats.get('ratio', 0) * 100:.1f}%"),
                ("Token数", f"{stats.get('token_count', 0)}"),
                ("匹配数", f"{stats.get('match_count', 0)}"),
                ("耗时", f"{stats.get('time_ms', 0):.1f}ms"),
            ]
            for label, value in stat_items:
                w = QWidget()
                vl = QVBoxLayout(w)
                vl.setContentsMargins(8, 4, 8, 4)
                v = create_styled_label(value, bold=True)
                v.setTextFormat(Qt.TextFormat.RichText)
                v.setAlignment(Qt.AlignmentFlag.AlignCenter)
                l = create_styled_label(label, 'text_secondary', 11)
                l.setTextFormat(Qt.TextFormat.RichText)
                l.setAlignment(Qt.AlignmentFlag.AlignCenter)
                vl.addWidget(v)
                vl.addWidget(l)
                w.setStyleSheet(f"background: {ThemeManager.hex('bg_surface')}; border-radius: 8px;")
                stats_layout.addWidget(w)
            layout.addLayout(stats_layout)

            legend = QLabel(
                f""
                "压缩率: "
                f"● 低  → "
                f"● 中  → "
                f"● 高   "
                f"| 悬停查看详情"
            )
            legend.setTextFormat(Qt.TextFormat.RichText)
            layout.addWidget(legend)

            splitter = QSplitter(Qt.Orientation.Horizontal)
            self._heatmap = TokenHeatmapWidget()
            self._heatmap.set_data(text, tokens, byte_to_char)
            splitter.addWidget(self._heatmap)
            self._info = TokenInfoPanel()
            self._info.setMinimumWidth(200)
            self._info.setMaximumWidth(260)
            splitter.addWidget(self._info)
            splitter.setSizes([900, 260])
            self._heatmap.token_hovered.connect(self._on_token_hovered)
            layout.addWidget(splitter, stretch=1)

            btn_layout = QHBoxLayout()
            btn_layout.addStretch()
            close_btn = QPushButton("关闭")
            close_btn.clicked.connect(self.accept)
            btn_layout.addWidget(close_btn)
            layout.addLayout(btn_layout)

        except Exception as e:
            logger.error("[HeatmapDialog] _setup_ui failed: %s", e, exc_info=True)
            error_layout = QVBoxLayout(self)
            error_label = QLabel(f"初始化热力图对话框失败:\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            error_layout.addWidget(error_label)

    def _on_token_hovered(self, idx: int) -> None:
        if 0 <= idx < len(self._heatmap._tokens):
            self._info.show_token(self._heatmap._tokens[idx], idx)
        else:
            self._info.show_token(None)


class HuffmanTreeCanvas(QWidget):
    def __init__(self, tree_data, parent=None):
        super().__init__(parent)
        self._tree = tree_data.tree
        self._codes = tree_data.codes
        self._min_size = 600
        self._node_positions: dict[int, tuple[float, float]] = {}
        self._compute_layout()

    def _compute_layout(self):
        self._node_positions.clear()
        if self._tree is None:
            return
        self._layout_node(self._tree, 0.5, 0.05, 0.25)

    def _layout_node(self, node, x, y, x_spread):
        if node is None:
            return
        nid = id(node)
        self._node_positions[nid] = (x, y)
        if not node.is_leaf():
            child_y = y + 0.08
            if node.left:
                self._layout_node(node.left, x - x_spread, child_y, x_spread * 0.5)
            if node.right:
                self._layout_node(node.right, x + x_spread, child_y, x_spread * 0.5)

    def minimumSize(self):
        return QSize(self._min_size, self._min_size)

    def sizeHint(self):
        return QSize(self._min_size, self._min_size)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w = self.width()
        h = self.height()

        painter.fillRect(0, 0, w, h, ThemeManager.color('bg_primary'))

        if not self._node_positions:
            painter.setPen(ThemeManager.color('text_secondary'))
            painter.drawText(QRect(0, 0, w, h), Qt.AlignmentFlag.AlignCenter, "无 Huffman 树数据")
            painter.end()
            return

        self._draw_edges(painter, self._tree, w, h)
        self._draw_nodes(painter, self._tree, w, h)
        painter.end()

    def _draw_edges(self, painter, node, w, h):
        if node is None or node.is_leaf():
            return
        nid = id(node)
        px, py = self._node_positions[nid]
        edge_pen = QPen(ThemeManager.color('border_dark'), 1.5)
        painter.setPen(edge_pen)

        if node.left:
            lid = id(node.left)
            lx, ly = self._node_positions[lid]
            painter.drawLine(int(px * w), int(py * h), int(lx * w), int(ly * h))
            self._draw_edges(painter, node.left, w, h)
        if node.right:
            rid = id(node.right)
            rx, ry = self._node_positions[rid]
            painter.drawLine(int(px * w), int(py * h), int(rx * w), int(ry * h))
            self._draw_edges(painter, node.right, w, h)

    def _draw_nodes(self, painter, node, w, h):
        if node is None:
            return
        nid = id(node)
        px, py = self._node_positions[nid]
        cx, cy = int(px * w), int(py * h)

        if node.is_leaf():
            r = 16
            painter.setBrush(ThemeManager.color('success'))
            painter.setPen(QPen(_darken_color(ThemeManager.color('success'), 40), 1.5))
            painter.drawEllipse(cx - r, cy - r, r * 2, r * 2)

            painter.setPen(ThemeManager.color('bg_primary'))
            font = QFont("Consolas", 7, QFont.Weight.Bold)
            painter.setFont(font)
            label = self._symbol_label(node.symbol)
            painter.drawText(QRect(cx - r, cy - r, r * 2, r * 2),
                             Qt.AlignmentFlag.AlignCenter, label)
        else:
            r = 10
            painter.setBrush(ThemeManager.color('accent'))
            painter.setPen(QPen(_darken_color(ThemeManager.color('accent'), 30), 1.5))
            painter.drawEllipse(cx - r, cy - r, r * 2, r * 2)

        self._draw_nodes(painter, node.left, w, h)
        self._draw_nodes(painter, node.right, w, h)

    def _symbol_label(self, symbol: int) -> str:
        if symbol < 256:
            if 32 <= symbol < 127:
                return chr(symbol)
            return str(symbol)
        if symbol == 256:
            return "EOF"
        if 257 <= symbol <= 285:
            return f"L{symbol - 257}"
        return str(symbol)


class HuffmanFreqChart(QWidget):
    def __init__(self, codes, parent=None, grayscale: bool = False):
        super().__init__(parent)
        self._grayscale = grayscale
        self._entries = sorted(codes, key=lambda e: e.code_length)
        self._min_height = 300

    def minimumSize(self):
        return QSize(400, self._min_height)

    def sizeHint(self):
        return QSize(400, self._min_height)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w = self.width()
        h = self.height()

        painter.fillRect(0, 0, w, h, ThemeManager.color('bg_primary'))

        if not self._entries:
            painter.setPen(ThemeManager.color('text_secondary'))
            painter.drawText(QRect(0, 0, w, h), Qt.AlignmentFlag.AlignCenter, "无编码数据")
            painter.end()
            return

        margin_left = 50
        margin_right = 20
        margin_top = 30
        margin_bottom = 50
        chart_w = w - margin_left - margin_right
        chart_h = h - margin_top - margin_bottom

        max_freq = max(e.frequency for e in self._entries) if self._entries else 1
        if max_freq == 0:
            max_freq = 1

        n = len(self._entries)
        bar_w = max(chart_w / n - 2, 3)

        painter.setPen(ThemeManager.color('text_secondary'))
        font = QFont("Microsoft YaHei", 8)
        painter.setFont(font)
        painter.drawText(QRect(0, 0, w, margin_top), Qt.AlignmentFlag.AlignCenter, "符号频率分布")

        max_cl = max((e.code_length for e in self._entries), default=1)
        import colorsys
        for i, entry in enumerate(self._entries):
            x = margin_left + i * (chart_w / n) + 1
            bar_h = (entry.frequency / max_freq) * chart_h
            y = margin_top + chart_h - bar_h

            t = min(entry.code_length / max(max_cl, 10), 1.0)
            hue = max(0.0, 120.0 * (1.0 - t))
            sat = 0.80 - 0.15 * t
            val = 0.90 - 0.20 * t
            rgb = colorsys.hsv_to_rgb(hue / 360.0, sat, val)
            color = QColor(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255))

            painter.setBrush(color)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRect(int(x), int(y), int(bar_w), int(bar_h))

        painter.setPen(ThemeManager.color('border_dark'))
        painter.drawLine(margin_left, margin_top + chart_h,
                         margin_left + chart_w, margin_top + chart_h)
        painter.drawLine(margin_left, margin_top,
                         margin_left, margin_top + chart_h)

        painter.end()


class HuffmanTreeDialog(QDialog):
    def __init__(self, huffman_trees, filename: str = "", parent=None):
        super().__init__(parent)
        self._trees = huffman_trees
        self._current_idx = 0
        self.setWindowTitle(f"Huffman 可视化 - {filename}")
        self.resize(1200, 800)
        self._setup_ui()

    def _setup_ui(self):
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)

        header = QLabel("🌳 Huffman 编码可视化")
        header.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(header)

        if len(self._trees) > 1:
            tree_select_layout = QHBoxLayout()
            tree_select_layout.addWidget(QLabel("选择 Huffman 树:"))

            self._tree_combo = QComboBox()
            for i, td in enumerate(self._trees):
                self._tree_combo.addItem(f"#{i + 1} {td.tree_type}")
            self._tree_combo.currentIndexChanged.connect(self._on_tree_changed)
            tree_select_layout.addWidget(self._tree_combo)
            tree_select_layout.addStretch()
            layout.addLayout(tree_select_layout)

        splitter = QSplitter(Qt.Orientation.Horizontal)

        tree_group = QGroupBox("Huffman 树")
        tree_layout = QVBoxLayout(tree_group)
        self._tree_scroll = QScrollArea()
        self._tree_scroll.setWidgetResizable(True)
        self._tree_canvas = HuffmanTreeCanvas(self._trees[0])
        self._tree_scroll.setWidget(self._tree_canvas)
        tree_layout.addWidget(self._tree_scroll)
        splitter.addWidget(tree_group)

        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        right_layout.setContentsMargins(0, 0, 0, 0)

        freq_group = QGroupBox("频率分布")
        freq_layout = QVBoxLayout(freq_group)
        self._freq_chart = HuffmanFreqChart(self._trees[0].codes)
        freq_layout.addWidget(self._freq_chart)
        right_layout.addWidget(freq_group, stretch=1)

        code_group = QGroupBox("编码表")
        code_layout = QVBoxLayout(code_group)
        self._code_table = QTableWidget()
        self._code_table.setColumnCount(4)
        self._code_table.setHorizontalHeaderLabels(["符号", "频率", "编码", "码长"])
        self._code_table.horizontalHeader().setStretchLastSection(True)
        self._code_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._code_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._code_table.setAlternatingRowColors(True)
        self._code_table.setStyleSheet(
            self._code_table.styleSheet() + f"QTableWidget {{ alternate-background-color: {ThemeManager.hex('bg_hover')}; }}")
        self._populate_code_table(self._trees[0].codes)
        code_layout.addWidget(self._code_table)
        right_layout.addWidget(code_group, stretch=1)

        splitter.addWidget(right_widget)
        splitter.setSizes([600, 600])
        layout.addWidget(splitter, stretch=1)

        stats_layout = QHBoxLayout()
        self._stats_label = QLabel()
        self._update_stats(self._trees[0])
        stats_layout.addWidget(self._stats_label)
        stats_layout.addStretch()

        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        stats_layout.addWidget(close_btn)
        layout.addLayout(stats_layout)

    def _on_tree_changed(self, idx):
        if 0 <= idx < len(self._trees):
            self._current_idx = idx
            td = self._trees[idx]
            self._tree_canvas = HuffmanTreeCanvas(td)
            self._tree_scroll.setWidget(self._tree_canvas)
            self._freq_chart = HuffmanFreqChart(td.codes)
            new_freq_group = self._freq_chart.parent()
            self._populate_code_table(td.codes)
            self._update_stats(td)

    def _populate_code_table(self, codes):
        self._code_table.setRowCount(len(codes))
        for i, entry in enumerate(codes):
            sym_text = self._format_symbol(entry.symbol)
            sym_item = QTableWidgetItem(sym_text)
            sym_item.setForeground(ThemeManager.color('text_primary'))
            self._code_table.setItem(i, 0, sym_item)

            freq_item = QTableWidgetItem(str(entry.frequency))
            freq_item.setForeground(ThemeManager.color('text_primary'))
            self._code_table.setItem(i, 1, freq_item)

            code_item = QTableWidgetItem(entry.code)
            code_item.setForeground(ThemeManager.color('text_secondary'))
            code_item.setFont(QFont("Consolas", 9))
            self._code_table.setItem(i, 2, code_item)

            len_item = QTableWidgetItem(str(entry.code_length))
            len_item.setForeground(_code_length_to_color(entry.code_length))
            self._code_table.setItem(i, 3, len_item)

        self._code_table.resizeColumnsToContents()

    def _format_symbol(self, symbol: int) -> str:
        if symbol < 256:
            if 32 <= symbol < 127:
                return f"'{chr(symbol)}' ({symbol})"
            return str(symbol)
        if symbol == 256:
            return "EOF (256)"
        if 257 <= symbol <= 285:
            return f"LEN_{symbol - 257} ({symbol})"
        return str(symbol)

    def _update_stats(self, td):
        n_symbols = len(td.codes)
        avg_len = sum(e.code_length for e in td.codes) / n_symbols if n_symbols else 0
        total_freq = sum(e.frequency for e in td.codes)
        self._stats_label.setText(
            f"符号数: {n_symbols}  |  "
            f"平均码长: {avg_len:.2f} bit  |  "
            f"总频率: {total_freq}  |  "
            f"树类型: {td.tree_type}  |  "
            f"树序列化: {td.total_bits} bit"
        )
        self._stats_label.setTextFormat(Qt.TextFormat.RichText)

    def _on_token_hovered(self, idx: int) -> None:
        if 0 <= idx < len(self._heatmap._tokens):
            self._info.show_token(self._heatmap._tokens[idx], idx)
        else:
            self._info.show_token(None)


class DPArrayBar(QWidget):
    WINDOW_SIZE = 40

    def __init__(self, parent=None, grayscale: bool = False):
        super().__init__(parent)
        self._grayscale = grayscale
        self._dp_array: list = []
        self._optimal_positions: set[int] = set()
        self._current_pos: int = -1
        self._hover_pos: int = -1
        self._cell_width = 44
        self._cell_height = 56
        self._tc_min: int = 0
        self._tc_max: int = 1
        self.setMouseTracking(True)
        self.setMinimumHeight(80)
        self.setMaximumHeight(130)

    def set_data(self, dp_array: list, optimal_positions: set[int], current_pos: int = -1) -> None:
        self._dp_array = dp_array
        self._optimal_positions = optimal_positions
        self._current_pos = current_pos
        reachable_tc = [s.token_count for s in dp_array if getattr(s, "reachable", False)]
        if reachable_tc:
            self._tc_max = max(reachable_tc)
            self._tc_min = min(reachable_tc)
        else:
            self._tc_max = 1
            self._tc_min = 0
        self.update()

    def sizeHint(self):
        return QSize(self.WINDOW_SIZE * self._cell_width + 80, self._cell_height + 40)

    def _visible_range(self) -> tuple[int, int]:
        n = len(self._dp_array)
        if n == 0:
            return 0, 0
        if self._current_pos < 0:
            return 0, min(self.WINDOW_SIZE, n)
        half = self.WINDOW_SIZE // 2
        start = max(0, self._current_pos - half)
        end = min(n, start + self.WINDOW_SIZE)
        if end - start < self.WINDOW_SIZE:
            start = max(0, end - self.WINDOW_SIZE)
        return start, end

    def mouseMoveEvent(self, event) -> None:
        pos = event.position().toPoint() if hasattr(event, 'position') else event.pos()
        start, end = self._visible_range()
        x = pos.x() - 40
        if x >= 0 and start < end:
            idx = int(x / self._cell_width) + start
            if idx != self._hover_pos and idx < end:
                self._hover_pos = idx
                self.update()
        else:
            if self._hover_pos != -1:
                self._hover_pos = -1
                self.update()

    def leaveEvent(self, event) -> None:
        self._hover_pos = -1
        self.update()

    def paintEvent(self, event) -> None:
        from PyQt6.QtGui import QPainter, QColor, QFont, QPen, QBrush, QLinearGradient
        from PyQt6.QtCore import Qt, QRect
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        n = len(self._dp_array)
        if n == 0:
            painter.setPen(ThemeManager.color('text_secondary'))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "无 DP 数据")
            return

        start, end = self._visible_range()
        if start >= end:
            return

        max_tc = self._tc_max
        min_tc = self._tc_min

        start_x = 40
        bar_y = 8
        cw = self._cell_width
        ch = self._cell_height

        if start > 0:
            painter.setPen(ThemeManager.color('text_muted'))
            f = QFont("Consolas", 9)
            painter.setFont(f)
            painter.drawText(2, bar_y + ch // 2 + 4, "…")

        for vi, i in enumerate(range(start, end)):
            st = self._dp_array[i]
            cx = start_x + vi * cw
            cy = bar_y

            if not st.reachable:
                painter.fillRect(cx + 1, cy + 1, cw - 2, ch - 2, ThemeManager.color('bg_surface'))
                painter.setPen(ThemeManager.color('border'))
                painter.drawRect(cx, cy, cw, ch)
                painter.setPen(ThemeManager.color('text_muted'))
                f = QFont("Consolas", 8)
                painter.setFont(f)
                painter.drawText(cx + 2, cy + 18, str(st.position))
                continue

            is_optimal = st.position in self._optimal_positions
            is_current = (st.position == self._current_pos)
            is_hovered = (st.position == self._hover_pos)

            if max_tc > min_tc:
                t = (st.token_count - min_tc) / max(max_tc - min_tc, 1)
            else:
                t = 0

            if is_current:
                bg = QColor(ThemeManager.color('accent'))
                bg.setAlpha(50)
                border = QPen(ThemeManager.color('accent'), 2.5)
            elif is_optimal:
                bg = QColor(ThemeManager.color('success'))
                bg.setAlpha(35)
                border = QPen(ThemeManager.color('success'), 2)
            elif is_hovered:
                bg = QColor(ThemeManager.color('border_dark'))
                bg.setAlpha(40)
                border = QPen(ThemeManager.color('text_secondary'), 1.5)
            else:
                bg = ThemeManager.color('bg_surface')
                border = QPen(ThemeManager.color('border'), 1)

            grad = QLinearGradient(cx + 2, cy + 2, cx + 2, cy + ch - 2)
            if t <= 0.5:
                r1, g1, b1 = 34, 197, 94
                r2, g2, b2 = 22, 163, 74
            else:
                r1 = int(34 + t * (239 - 34))
                g1 = int(197 - t * (197 - 68))
                b1 = int(94 - t * (94 - 68))
                r2, g2, b2 = int(r1 * 0.85), int(g1 * 0.85), int(b1 * 0.85)
            grad.setColorAt(0, QColor(r1, g1, b1, 180))
            grad.setColorAt(1, QColor(r2, g2, b2, 180))
            painter.fillRect(cx + 2, cy + 2, cw - 4, ch - 4, grad)
            painter.setPen(border)
            painter.drawRect(cx, cy, cw, ch)

            tc_color = QColor("#ffffff") if t > 0.5 else ThemeManager.color('text_primary')
            painter.setPen(tc_color)
            f = QFont("Consolas", 10, QFont.Weight.Bold if is_optimal or is_current else QFont.Weight.Normal)
            painter.setFont(f)
            painter.drawText(QRect(cx + 2, cy + 2, cw - 4, 20), Qt.AlignmentFlag.AlignCenter, str(st.token_count))

            painter.setPen(ThemeManager.color('text_secondary'))
            f2 = QFont("Consolas", 8)
            painter.setFont(f2)
            painter.drawText(QRect(cx + 2, cy + 20, cw - 4, 16), Qt.AlignmentFlag.AlignCenter, f"p:{st.position}")

            off = st.choice.offset
            ln = st.choice.length
            if off > 0:
                painter.setPen(ThemeManager.color('chart_5'))
                f3 = QFont("Consolas", 8)
                painter.setFont(f3)
                painter.drawText(QRect(cx + 2, cy + 36, cw - 4, 16), Qt.AlignmentFlag.AlignCenter, f"({off},{ln})")
            else:
                painter.setPen(ThemeManager.color('text_secondary'))
                f3 = QFont("Consolas", 8)
                painter.setFont(f3)
                nb = st.choice.literal
                ch_repr = chr(nb) if 32 <= nb < 127 else f"{nb:02x}"
                painter.drawText(QRect(cx + 2, cy + 36, cw - 4, 16), Qt.AlignmentFlag.AlignCenter, f"'{ch_repr}'")

        if end < n:
            ell_x = start_x + (end - start) * cw + 4
            painter.setPen(ThemeManager.color('text_muted'))
            f = QFont("Consolas", 9)
            painter.setFont(f)
            painter.drawText(ell_x, bar_y + ch // 2 + 4, "…")

        painter.setPen(ThemeManager.color('text_secondary'))
        f = QFont("Microsoft YaHei", 9)
        painter.setFont(f)
        legend_x = start_x
        legend_y = bar_y + ch + 16
        painter.drawText(legend_x, legend_y, f"位置 {start}~{end - 1} / {n - 1}")
        painter.setPen(ThemeManager.color('success'))
        painter.drawText(legend_x + 160, legend_y, "■ 最优路径")
        painter.setPen(ThemeManager.color('accent'))
        painter.drawText(legend_x + 250, legend_y, "■ 当前")
        painter.setPen(ThemeManager.color('text_secondary'))
        painter.drawText(legend_x + 310, legend_y, f"token数 {min_tc}(绿) → {max_tc}(红)")


class LZDPDPSliderWidget(QWidget):
    def __init__(self, text: str, tokens: list[Token], byte_to_char: list[int],
                 dp_viz, algorithm: str, parent=None, grayscale: bool = False):
        super().__init__(parent)
        self._grayscale = grayscale
        logger.info("[LZDPDP] __init__ text=%d tokens=%d dp_viz=%s", len(text), len(tokens), type(dp_viz).__name__ if dp_viz else "None")
        self._text = text
        self._tokens = tokens
        self._byte_to_char = byte_to_char
        self._dp_viz = dp_viz
        self._algorithm = algorithm
        self._current_step = 0
        self._is_playing = False
        self._optimal_positions: set[int] = set()
        self._dp_step_index: dict[int, object] = {}
        self._precompute()
        self._setup_ui()

    def _precompute(self) -> None:
        if self._dp_viz and hasattr(self._dp_viz, 'dp_array') and self._dp_viz.dp_array:
            dp_arr = self._dp_viz.dp_array
            final_pos = self._dp_viz.input_length
            if final_pos < len(dp_arr) and dp_arr[final_pos].reachable:
                cur = final_pos
                while cur != 18446744073709551615:
                    self._optimal_positions.add(cur)
                    cur = dp_arr[cur].predecessor
        if self._dp_viz and hasattr(self._dp_viz, 'steps') and self._dp_viz.steps:
            for ds in self._dp_viz.steps:
                self._dp_step_index[ds.position] = ds

    def _setup_ui(self) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)

        ctrl = QHBoxLayout()
        self._prev_btn = QPushButton("◀ 上一步")
        self._prev_btn.clicked.connect(self._step_prev)
        ctrl.addWidget(self._prev_btn)

        self._slider = QSlider(Qt.Orientation.Horizontal)
        self._slider.setMinimum(0)
        
        max_steps = len(self._dp_viz.steps) - 1 if (self._dp_viz and hasattr(self._dp_viz, 'steps') and self._dp_viz.steps) else max(len(self._tokens) - 1, 0)
        self._slider.setMaximum(max(max_steps, 0))
        self._slider.setValue(0)
        self._slider.valueChanged.connect(self._on_slider)
        ctrl.addWidget(self._slider, stretch=1)

        self._next_btn = QPushButton("下一步 ▶")
        self._next_btn.clicked.connect(self._step_next)
        ctrl.addWidget(self._next_btn)

        self._play_btn = QPushButton("▶ 自动播放")
        self._play_btn.clicked.connect(self._toggle_play)
        ctrl.addWidget(self._play_btn)

        self._step_label = QLabel("0 / 0")
        self._step_label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 12px;")
        ctrl.addWidget(self._step_label)

        layout.addLayout(ctrl)

        main_splitter = QSplitter(Qt.Orientation.Vertical)

        self._text_display = QTextEdit()
        self._text_display.setReadOnly(True)
        font = QFont("Consolas", 10)
        font.setStyleHint(QFont.StyleHint.Monospace)
        self._text_display.setFont(font)
        self._text_display.setLineWrapMode(QTextEdit.LineWrapMode.WidgetWidth)
        main_splitter.addWidget(self._text_display)

        dp_group = QGroupBox("DP 动态规划过程")
        dp_layout = QVBoxLayout(dp_group)

        self._dp_array_bar = DPArrayBar()
        dp_layout.addWidget(self._dp_array_bar)

        dp_splitter = QSplitter(Qt.Orientation.Horizontal)

        from PyQt6.QtWidgets import QTableWidget, QTableWidgetItem, QAbstractItemView
        self._candidate_table = QTableWidget()
        self._candidate_table.setColumnCount(5)
        self._candidate_table.setHorizontalHeaderLabels(["#", "偏移(offset)", "长度(length)", "下一字节", "状态"])
        self._candidate_table.horizontalHeader().setStretchLastSection(True)
        self._candidate_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._candidate_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._candidate_table.setAlternatingRowColors(True)
        self._candidate_table.setStyleSheet(
            f"QTableWidget {{ alternate-background-color: {ThemeManager.hex('bg_hover')}; }}"
        )
        dp_splitter.addWidget(self._candidate_table)

        from gui.widgets.info_panel import InfoPanel

        self._dp_info = InfoPanel()
        self._dp_info.setMaximumWidth(350)
        dp_splitter.addWidget(self._dp_info)

        dp_splitter.setSizes([600, 300])
        dp_layout.addWidget(dp_splitter)
        self._dp_splitter = dp_splitter

        main_splitter.addWidget(dp_group)
        main_splitter.setSizes([400, 400])

        layout.addWidget(main_splitter, stretch=1)

        self._detail_label = QLabel("")
        self._detail_label.setWordWrap(True)
        self._detail_label.setTextFormat(Qt.TextFormat.RichText)
        self._detail_label.setStyleSheet(
            f"color: {ThemeManager.hex('text_primary')}; font-size: 12px; font-family: Consolas; "
            f"background: {ThemeManager.hex('bg_surface')}; padding: 10px; border-radius: 8px;"
        )
        layout.addWidget(self._detail_label)

        self._render_step()

    def _get_token_char_range(self, idx: int) -> tuple[int, int]:
        if idx < 0 or idx >= len(self._tokens):
            return 0, 0
        t = self._tokens[idx]
        bs = t.original_start
        be = min(bs + t.original_length, len(self._byte_to_char))
        cs = self._byte_to_char[bs] if bs < len(self._byte_to_char) else 0
        ce = self._byte_to_char[be] if be < len(self._byte_to_char) else len(self._text)
        return cs, ce

    def _get_prev_end(self, idx: int) -> int:
        if idx <= 0:
            return 0
        _, ce = self._get_token_char_range(idx - 1)
        return ce

    def _render_step(self) -> None:
        try:
            step = self._current_step
            has_steps = self._dp_viz and hasattr(self._dp_viz, 'steps') and self._dp_viz.steps
            n = len(self._dp_viz.steps) if has_steps else len(self._tokens)
            
            self._step_label.setText(f"{step + 1} / {n}")

            if has_steps:
                ds = self._dp_viz.steps[step]
                cs = self._byte_to_char[ds.position] if ds.position < len(self._byte_to_char) else len(self._text)
                
                best_cand = None
                for c in ds.candidates:
                    if c.is_chosen:
                        best_cand = c
                        break
                
                match_len = best_cand.length if best_cand else 1
                be = min(ds.position + match_len, len(self._byte_to_char))
                ce = self._byte_to_char[be] if be < len(self._byte_to_char) else len(self._text)
                prev_end = cs
            else:
                prev_end = self._get_prev_end(step)
                cs, ce = self._get_token_char_range(step)

            self._text_display.clear()
            cursor = self._text_display.textCursor()

            if prev_end > 0:
                fmt_gray = QTextCharFormat()
                fmt_gray.setForeground(ThemeManager.color('border_dark'))
                cursor.insertText(self._text[:prev_end], fmt_gray)

            if ce > cs:
                fmt_hl = QTextCharFormat()
                bg = QColor(ThemeManager.hex('brand_primary'))
                bg.setAlpha(140)
                fmt_hl.setBackground(QBrush(bg))
                fmt_hl.setForeground(ThemeManager.color('bg_surface'))
                fmt_hl.setFontWeight(700)
                cursor.insertText(self._text[cs:ce], fmt_hl)

            if ce < len(self._text):
                fmt_pending = QTextCharFormat()
                fmt_pending.setForeground(ThemeManager.color('text_muted'))
                cursor.insertText(self._text[ce:], fmt_pending)

            highlight_cursor = self._text_display.textCursor()
            highlight_cursor.setPosition(cs)
            self._text_display.setTextCursor(highlight_cursor)
            self._text_display.ensureCursorVisible()

            self._render_dp_panel(step)

            if has_steps:
                ds = self._dp_viz.steps[step]
                detail = (
                    f"DP Step #{step}   "
                    f"位置: byte {ds.position} | "
                    f"当前最优Token数: {ds.best_token_count}\n"
                )
                if best_cand:
                    detail += f"-> 最佳转移: MATCH (offset={best_cand.offset}, length={best_cand.length})\n"
                else:
                    detail += f"-> 最佳转移: LITERAL\n"
                    
                detail += f"候选项数: {len(ds.candidates)}"
                self._detail_label.setText(detail)
            else:
                t = self._tokens[step]
                type_label = {TokenType.LITERAL: "LITERAL", TokenType.MATCH: "MATCH",
                              TokenType.LITERAL_RUN: "LITERAL_RUN"}.get(t.type, t.type.value)
                color = _ratio_to_qcolor(t.compression_ratio)
                color_name = color.name()
                detail = (
                    f"Token #{step}   "
                    f"{type_label}\n"
                    f"位置: byte {t.original_start}~{t.original_start + t.original_length - 1} | "
                    f"长度: {t.original_length}B | 编码: {t.compressed_size:.2f}B | "
                    f"压缩率: {t.compression_ratio * 100:.1f}%"
                )
                if t.type == TokenType.MATCH:
                    detail += f"\n        回退偏移: {t.match_offset} 字符"
                self._detail_label.setText(detail)

        except Exception as e:
            logger.error("[LZDPDP] _render_step failed at step=%d: %s", self._current_step, e, exc_info=True)
            try:
                self._detail_label.setText(f"渲染错误 (step={self._current_step}):\n{str(e)}")
            except:
                pass

    def _render_dp_panel(self, step: int) -> None:
        from PyQt6.QtCore import Qt
        from PyQt6.QtWidgets import QTableWidgetItem
        
        has_steps = self._dp_viz and hasattr(self._dp_viz, 'steps') and self._dp_viz.steps

        if self._dp_viz and hasattr(self._dp_viz, 'dp_array') and self._dp_viz.dp_array:
            dp_arr = self._dp_viz.dp_array
            current_dp_pos = self._dp_viz.steps[step].position if has_steps else (self._tokens[step].original_start if step < len(self._tokens) else -1)
            self._dp_array_bar.set_data(dp_arr, self._optimal_positions, current_dp_pos)

        dp_step = self._dp_viz.steps[step] if has_steps else self._dp_step_index.get(
            self._tokens[step].original_start if step < len(self._tokens) else -1)

        if dp_step is None:
            self._candidate_table.setRowCount(0)
            cur_pos = self._tokens[step].original_start if step < len(self._tokens) else -1
            cur_state = None
            if self._dp_viz and hasattr(self._dp_viz, 'dp_array') and self._dp_viz.dp_array:
                for s in self._dp_viz.dp_array:
                    if s.position == cur_pos and s.reachable:
                        cur_state = s
                        break
            if cur_state is not None:
                from gui.widgets.info_panel import create_dp_literal_info
                new_panel = create_dp_literal_info(cur_pos, cur_state)
            else:
                from gui.widgets.info_panel import create_dp_empty_info
                new_panel = create_dp_empty_info()
            idx = self._dp_splitter.indexOf(self._dp_info)
            if idx >= 0:
                self._dp_splitter.replaceWidget(idx, new_panel)
                self._dp_info.deleteLater()
                self._dp_info = new_panel
            return

        candidates = dp_step.candidates
        self._candidate_table.setRowCount(len(candidates))
        for i, cand in enumerate(candidates):
            num_item = QTableWidgetItem(str(i + 1))
            num_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            self._candidate_table.setItem(i, 0, num_item)

            off_item = QTableWidgetItem(str(cand.offset))
            off_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            self._candidate_table.setItem(i, 1, off_item)

            len_item = QTableWidgetItem(str(cand.length))
            len_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            self._candidate_table.setItem(i, 2, len_item)

            nb = cand.literal
            byte_str = chr(nb) if 32 <= nb < 127 else f"0x{nb:02x}"
            nb_item = QTableWidgetItem(byte_str)
            nb_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            self._candidate_table.setItem(i, 3, nb_item)

            if cand.is_chosen:
                status_item = QTableWidgetItem("✅ 最优选择")
                status_item.setForeground(ThemeManager.color('success'))
                for col in range(5):
                    item = self._candidate_table.item(i, col)
                    if item:
                        f = item.font()
                        f.setBold(True)
                        item.setFont(f)
                        bg = QColor(ThemeManager.color('success'))
                        bg.setAlpha(80)
                        item.setBackground(bg)
            else:
                status_item = QTableWidgetItem("候选")
                status_item.setForeground(ThemeManager.color('text_secondary'))
            status_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            self._candidate_table.setItem(i, 4, status_item)

        best_tc = dp_step.best_token_count
        cur_pos = dp_step.position
        cur_state = None
        if self._dp_viz and hasattr(self._dp_viz, 'dp_array') and self._dp_viz.dp_array:
            for s in self._dp_viz.dp_array:
                if s.position == cur_pos and s.reachable:
                    cur_state = s
                    break
        from gui.widgets.info_panel import create_dp_step_info
        new_panel = create_dp_step_info(dp_step, candidates, best_tc, cur_state)
        idx = self._dp_splitter.indexOf(self._dp_info)
        if idx >= 0:
            self._dp_splitter.replaceWidget(idx, new_panel)
            self._dp_info.deleteLater()
            self._dp_info = new_panel

    def _on_slider(self, val) -> None:
        self._current_step = val
        self._render_step()

    def _max_step_index(self) -> int:
        if self._dp_viz and hasattr(self._dp_viz, "steps") and self._dp_viz.steps:
            return max(len(self._dp_viz.steps) - 1, 0)
        return max(len(self._tokens) - 1, 0)

    def _step_prev(self) -> None:
        logger.debug("[LZDPDP] _step_prev called, current_step=%d", self._current_step)
        if self._current_step > 0:
            self._current_step -= 1
            logger.info("[LZDPDP] _step_prev: moving to step=%d", self._current_step)
            self._slider.setValue(self._current_step)

    def _step_next(self) -> None:
        mx = self._max_step_index()
        logger.debug("[LZDPDP] _step_next called, current_step=%d, max=%d", self._current_step, mx)
        if self._current_step < mx:
            self._current_step += 1
            logger.info("[LZDPDP] _step_next: moving to step=%d", self._current_step)
            self._slider.setValue(self._current_step)

    def _toggle_play(self) -> None:
        self._is_playing = not self._is_playing
        if self._is_playing:
            self._play_btn.setText("⏸ 暂停")
            from PyQt6.QtCore import QTimer
            if not hasattr(self, '_timer'):
                self._timer = QTimer(self)
                self._timer.timeout.connect(self._auto_step)
            self._timer.start(600)
        else:
            self._play_btn.setText("▶ 自动播放")
            if hasattr(self, '_timer'):
                self._timer.stop()

    def _auto_step(self) -> None:
        if self._current_step < self._max_step_index():
            self._current_step += 1
            self._slider.setValue(self._current_step)
        else:
            self._is_playing = False
            self._play_btn.setText("▶ 自动播放")
            if hasattr(self, '_timer'):
                self._timer.stop()


class LZDPDPDialog(QDialog):
    def __init__(self, text: str, tokens: list[Token], byte_to_char: list[int],
                 dp_viz, filename: str, algorithm: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"LZDP DP 算法演示 - {filename}")
        self.resize(1200, 900)
        layout = QVBoxLayout(self)
        dp_arr_len = len(dp_viz.dp_array) if (dp_viz and hasattr(dp_viz, 'dp_array') and dp_viz.dp_array) else 0
        reachable_count = sum(1 for s in dp_viz.dp_array if s.reachable) if (dp_viz and hasattr(dp_viz, 'dp_array') and dp_viz.dp_array) else 0
        final_tc = dp_viz.dp_array[dp_viz.input_length].token_count if (dp_viz and hasattr(dp_viz, 'dp_array') and dp_viz.dp_array and dp_viz.input_length < len(dp_viz.dp_array) and dp_viz.dp_array[dp_viz.input_length].reachable) else 0
        header = QLabel(
            f"🧠 LZDP DP 算法演示"
            f"文件: {filename} | 算法: {algorithm} | "
            f"DP数组: {reachable_count}/{dp_arr_len} 可达 | "
            f"最优Token数: {final_tc} | "
            f"DP步骤: {len(dp_viz.steps) if dp_viz else 0}"
        )
        header.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(header)
        self._slider_widget = LZDPDPSliderWidget(text, tokens, byte_to_char, dp_viz, algorithm)
        layout.addWidget(self._slider_widget, stretch=1)
        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.setStyleSheet(f"background:{ThemeManager.hex('border_dark')}; color:{ThemeManager.hex('text_primary')}; padding:6px 20px; border-radius:6px;")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


class BlockHeatmapCanvas(QWidget):
    _block_hovered = pyqtSignal(int)

    def __init__(self, blocks: list[dict], parent=None):
        super().__init__(parent)
        self._blocks = blocks
        self._hover_idx = -1
        self._cell_size = 20
        self._gap = 2
        self.setMouseTracking(True)
        self.setMinimumSize(400, 300)
        t = ThemeManager.get()
        self._bg = QColor(t.bg_primary)
        self._tooltip_bg = QColor(t.bg_elevated)
        self._tooltip_border = QColor(t.border_dark)
        self._tooltip_text = QColor(t.text_primary)
        self._tooltip_label = QColor(t.text_secondary)

    def sizeHint(self) -> QSize:
        cols = max(1, int((self.width() - 40) / (self._cell_size + self._gap)))
        rows = (len(self._blocks) + cols - 1) // cols if cols > 0 else len(self._blocks)
        return QSize(600, max(300, rows * (self._cell_size + self._gap) + 60))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w = self.width() - 40
        cols = max(1, int(w / (self._cell_size + self._gap)))
        cw = self._cell_size + self._gap

        for i, b in enumerate(self._blocks):
            col = i % cols
            row = i // cols
            x = 20 + col * cw
            y = 30 + row * cw
            color = QColor(b["color"])
            if i == self._hover_idx:
                rect = QRectF(x - 2, y - 2, self._cell_size + 4, self._cell_size + 4)
                painter.setBrush(QBrush(color))
                pen = QPen(QColor(255, 255, 255, 180), 2)
                painter.setPen(pen)
                painter.drawRoundedRect(rect, 4, 4)
            else:
                rect = QRectF(x, y, self._cell_size, self._cell_size)
                painter.setBrush(QBrush(color))
                painter.setPen(Qt.PenStyle.NoPen)
                painter.drawRoundedRect(rect, 3, 3)

        if self._hover_idx >= 0 and self._hover_idx < len(self._blocks):
            b = self._blocks[self._hover_idx]
            mx = self.mapFromGlobal(self.cursor().pos()).x() + 16
            my = self.mapFromGlobal(self.cursor().pos()).y() + 16
            lines = [
                f"偏移: 0x{b['offset']:X}",
                f"块大小: {b['size']} B",
                f"压缩后: {b['compressed_size']} B",
                f"压缩率: {b['ratio'] * 100:.1f}%",
            ]
            tw = max(painter.fontMetrics().horizontalAdvance(l) for l in lines) + 28
            th = len(lines) * 18 + 16
            mx = min(mx, self.width() - tw - 8)
            my = min(my, self.height() - th - 8)
            tip_rect = QRectF(mx, my, tw, th)
            painter.setBrush(QBrush(self._tooltip_bg))
            painter.setPen(QPen(self._tooltip_border, 1))
            painter.drawRoundedRect(tip_rect, 6, 6)
            painter.setPen(self._tooltip_label)
            for j, line in enumerate(lines):
                painter.drawText(int(mx + 14), int(my + 16 + j * 18), line)

        painter.end()

    def mouseMoveEvent(self, event):
        pos = event.position().toPoint()
        w = self.width() - 40
        cols = max(1, int(w / (self._cell_size + self._gap)))
        cw = self._cell_size + self._gap
        col = max(0, min(cols - 1, int((pos.x() - 20) / cw)))
        row = max(0, int((pos.y() - 30) / cw))
        idx = row * cols + col
        if idx != self._hover_idx and 0 <= idx < len(self._blocks):
            self._hover_idx = idx
            self.update()
        elif idx >= len(self._blocks):
            if self._hover_idx != -1:
                self._hover_idx = -1
                self.update()

    def leaveEvent(self, event):
        self._hover_idx = -1
        self.update()


class BlockHeatmapDialog(QDialog):
    def __init__(self, raw_data: bytes, compressed_data: bytes,
                 filename: str, algorithm: str,
                 original_size: int, compressed_size: int,
                 time_ms: float, block_size: int = 256, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"压缩热力图 - {filename}")
        self.setMinimumSize(750, 550)
        t = ThemeManager.get()
        self.setStyleSheet(
            f"QDialog {{ background: {t.bg_primary}; }}\n"
            f"QLabel {{ color: {t.text_primary}; background: transparent; }}\n"
            f"QGroupBox {{ color: {t.text_primary}; font-weight: 600; border: 1px solid {t.border}; "
            f"border-radius: 8px; margin-top: 8px; padding-top: 8px; }}\n"
            f"QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 4px; }}\n"
            f"QPushButton {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 6px; padding: 6px 20px; }}\n"
            f"QPushButton:hover {{ background: {t.bg_hover}; }}\n"
        )
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(12)

        header = QLabel(f"\U0001f4ca 压缩热力图")
        header.setStyleSheet(f"font-size: 18px; font-weight: 700; color: {t.text_primary}; background: transparent;")
        layout.addWidget(header)

        meta = QLabel(
            f"文件: {filename}  |  算法: {algorithm}  |  "
            f"原始: {original_size:,} B  |  压缩后: {compressed_size:,} B  |  耗时: {time_ms:.1f} ms"
        )
        meta.setStyleSheet(f"font-size: 12px; color: {t.text_secondary}; background: transparent;")
        layout.addWidget(meta)

        from gui.core.engine import CompressionEngine
        algo_map = {e.value: e for e in AlgorithmType}
        algo = algo_map.get(algorithm, AlgorithmType.DEFLATE)
        engine = CompressionEngine()
        n_blocks = max(1, (len(raw_data) + block_size - 1) // block_size)
        blocks = []
        for i in range(n_blocks):
            start = i * block_size
            end = min(start + block_size, len(raw_data))
            block_raw = raw_data[start:end]
            r = engine.compress(block_raw, algo)
            ratio = r.compressed_size / len(block_raw) if len(block_raw) > 0 else 1.0
            blocks.append({
                "index": i, "offset": start, "size": end - start,
                "compressed_size": r.compressed_size, "ratio": round(ratio, 4),
                "color": _ratio_to_qcolor(ratio).name(),
            })

        legend_layout = QHBoxLayout()
        legend_layout.addWidget(QLabel("高压缩"))
        grad_w = QWidget()
        grad_w.setFixedWidth(200)
        grad_w.setFixedHeight(12)
        grad_w.setStyleSheet("background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #22c55e,stop:0.5 #eab308,stop:1 #ef4444); border-radius: 6px;")
        legend_layout.addWidget(grad_w)
        legend_layout.addWidget(QLabel("低压缩"))
        legend_layout.addWidget(QLabel(f"  |  块大小: {block_size} B"))
        legend_layout.addStretch()
        layout.addLayout(legend_layout)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll.setStyleSheet(f"QScrollArea {{ border: none; background: transparent; }} "
                           f"QScrollArea > QWidget > QWidget {{ background: {t.bg_primary}; }}")
        canvas = BlockHeatmapCanvas(blocks)
        scroll.setWidget(canvas)
        layout.addWidget(scroll, stretch=1)

        stats_group = QGroupBox("统计")
        stats_layout = QHBoxLayout(stats_group)
        ratio_pct = f"{compressed_size / original_size * 100:.1f}" if original_size else "0"
        for label_text, value in [("原始大小", f"{original_size:,} B"),
                                   ("压缩后", f"{compressed_size:,} B"),
                                   ("压缩率", f"{ratio_pct}%"),
                                   ("耗时", f"{time_ms:.1f} ms")]:
            card = QWidget()
            card_l = QVBoxLayout(card)
            card_l.setContentsMargins(12, 10, 12, 10)
            val_lbl = QLabel(value)
            val_lbl.setStyleSheet(f"font-size: 22px; font-weight: 700; color: {t.accent}; background: transparent;")
            desc_lbl = QLabel(label_text)
            desc_lbl.setStyleSheet(f"font-size: 11px; color: {t.text_secondary}; background: transparent;")
            card_l.addWidget(val_lbl)
            card_l.addWidget(desc_lbl)
            stats_layout.addWidget(card)
        layout.addWidget(stats_group)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


class NetworkSimDialog(QDialog):
    def __init__(self, original_size: int, compressed_size: int,
                 compression_time_ms: float, filename: str,
                 algorithm: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"网络传输模拟 - {filename}")
        self.setMinimumSize(720, 580)
        t = ThemeManager.get()
        self.setStyleSheet(
            f"QDialog {{ background: {t.bg_primary}; }}\n"
            f"QLabel {{ color: {t.text_primary}; background: transparent; }}\n"
            f"QGroupBox {{ color: {t.text_primary}; font-weight: 600; border: 1px solid {t.border}; "
            f"border-radius: 8px; margin-top: 8px; padding-top: 8px; }}\n"
            f"QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 4px; }}\n"
            f"QPushButton {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 6px; padding: 6px 20px; }}\n"
            f"QPushButton:hover {{ background: {t.bg_hover}; }}\n"
            f"QProgressBar {{ border: 1px solid {t.border_dark}; border-radius: 4px; "
            f"background: {t.bg_surface}; text-align: center; color: {t.bg_primary}; "
            f"font-weight: 600; font-size: 11px; min-height: 22px; }}\n"
            f"QProgressBar::chunk {{ border-radius: 3px; }}\n"
        )
        from gui.core.models import NETWORK_PROFILES
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(12)

        header = QLabel("\U0001f310 网络传输模拟")
        header.setStyleSheet(f"font-size: 18px; font-weight: 700; color: {t.text_primary}; background: transparent;")
        layout.addWidget(header)

        meta = QLabel(
            f"文件: {filename}  |  算法: {algorithm}  |  "
            f"原始: {original_size:,} B  |  压缩后: {compressed_size:,} B  |  压缩耗时: {compression_time_ms:.1f} ms"
        )
        meta.setStyleSheet(f"font-size: 12px; color: {t.text_secondary}; background: transparent;")
        layout.addWidget(meta)

        cards_scroll = QScrollArea()
        cards_scroll.setWidgetResizable(True)
        cards_scroll.setStyleSheet(f"QScrollArea {{ border: none; background: transparent; }}")
        cards_container = QWidget()
        cards_layout = QVBoxLayout(cards_container)
        cards_layout.setSpacing(12)

        profiles_data = []
        worth_count = 0
        max_t_raw = 0.001
        for name, profile in NETWORK_PROFILES.items():
            t_raw = profile.transfer_time(original_size)
            t_comp = profile.transfer_time(compressed_size)
            saving = t_raw - t_comp
            net_saving = saving - compression_time_ms / 1000
            worth_it = net_saving > 0
            if worth_it:
                worth_count += 1
            max_t_raw = max(max_t_raw, t_raw)
            profiles_data.append({
                "name": name, "bandwidth_mbps": profile.bandwidth_bps / 1_000_000,
                "latency_ms": profile.latency_ms, "t_raw": t_raw, "t_comp": t_comp,
                "saving": saving, "net_saving": net_saving, "worth_it": worth_it,
            })

        for p in profiles_data:
            card = QGroupBox()
            card.setStyleSheet(f"QGroupBox {{ background: {t.bg_surface}; }}")
            cl = QVBoxLayout(card)
            cl.setSpacing(8)
            hdr = QHBoxLayout()
            title_lbl = QLabel(p["name"])
            title_lbl.setStyleSheet(f"font-size: 15px; font-weight: 700; color: {t.text_primary}; background: transparent;")
            hdr.addWidget(title_lbl)
            hdr.addStretch()
            badge = QLabel("\u2705 值得压缩" if p["worth_it"] else "\u2717 不值得")
            badge_color = "#166534" if p["worth_it"] else "#7f1d1d"
            badge_fg = "#86efac" if p["worth_it"] else "#fca5a5"
            badge.setStyleSheet(
                f"font-size: 11px; padding: 3px 10px; border-radius: 12px; font-weight: 600; "
                f"background: {badge_color}; color: {badge_fg};"
            )
            hdr.addWidget(badge)
            cl.addLayout(hdr)

            bw_lbl = QLabel(f"\u2193 {p['bandwidth_mbps']:.1f} Mbps  |  延迟 {p['latency_ms']:.0f} ms")
            bw_lbl.setStyleSheet(f"font-size: 11px; color: {t.text_muted}; background: transparent;")
            cl.addWidget(bw_lbl)

            raw_pct = int(min(100, (p["t_raw"] / max_t_raw) * 100))
            comp_pct = int(min(100, (p["t_comp"] / max_t_raw) * 100))

            for label_text, pct, is_comp in [("原始传输", raw_pct, False),
                                              ("压缩后传输", comp_pct, True)]:
                lbl = QLabel(label_text)
                lbl.setStyleSheet(f"font-size: 11px; color: {t.text_secondary}; background: transparent;")
                cl.addWidget(lbl)
                bar = QProgressBar()
                bar.setMaximum(100)
                bar.setValue(pct)
                bar.setTextVisible(True)
                bar.setFormat(_format_time_ns(p["t_raw"] if not is_comp else p["t_comp"]))
                c = "#ef4444" if not is_comp else "#22c55e"
                bar.setStyleSheet(bar.styleSheet() +
                    f"QProgressBar::chunk {{ background: {c}; border-radius: 3px; }}")
                cl.addWidget(bar)

            saving_val = p["saving"]
            net_val = p["net_saving"]
            saving_row = QHBoxLayout()
            saving_row.addWidget(QLabel("传输节省"))
            saving_row.addStretch()
            sv = QLabel(f"+{_format_time_ns(saving_val)} ({saving_val/max_t_raw*100:.1f}%)")
            sv.setStyleSheet(f"font-size: 13px; font-weight: 700; color: #22c55e; background: transparent;")
            saving_row.addWidget(sv)
            cl.addLayout(saving_row)

            net_row = QHBoxLayout()
            net_row.addWidget(QLabel("净节省（扣除压缩耗时）"))
            net_row.addStretch()
            nv = QLabel(f"{'+' if net_val > 0 else '-'}{_format_time_ns(abs(net_val))}")
            nv_css = f"font-size: 13px; font-weight: 700; color: {'#22c55e' if net_val > 0 else '#ef4444'}; background: transparent;"
            nv.setStyleSheet(nv_css)
            net_row.addWidget(nv)
            cl.addLayout(net_row)

            cards_layout.addWidget(card)

        cards_scroll.setWidget(cards_container)
        layout.addWidget(cards_scroll, stretch=1)

        analysis_group = QGroupBox("\U0001f4a1 分析")
        al = QVBoxLayout(analysis_group)
        ratio_str = f"{compressed_size / original_size * 100:.1f}" if original_size else "0"
        saving_bytes = original_size - compressed_size
        analysis_text = (
            f"使用 {algorithm} 压缩后，文件从 {original_size:,} B 缩小到 {compressed_size:,} B"
            f"（压缩率 {ratio_str}%），节省 {saving_bytes:,} B。\n"
            f"在 {len(profiles_data)} 种网络环境中，有 {worth_count} 种值得压缩（传输节省 > 压缩耗时）。"
        )
        if worth_count < len(profiles_data):
            analysis_text += "\n在高速网络（如 Ethernet）下，小文件的压缩耗时可能超过传输节省，此时直接传输更优。"
        else:
            analysis_text += "\n所有网络环境下压缩均有收益。"
        analysis_lbl = QLabel(analysis_text)
        analysis_lbl.setStyleSheet(f"font-size: 13px; color: {t.text_secondary}; background: transparent; line-height: 1.6;")
        analysis_lbl.setWordWrap(True)
        al.addWidget(analysis_lbl)
        layout.addWidget(analysis_group)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


def _format_time_ns(seconds: float) -> str:
    if seconds < 0.001:
        return f"{seconds * 1_000_000:.0f} \u03bcs"
    if seconds < 1:
        return f"{seconds * 1000:.1f} ms"
    return f"{seconds:.2f} s"


class LZSliderWidget(QWidget):
    def __init__(self, text: str, tokens: list[Token], byte_to_char: list[int],
                 algorithm: str, parent=None, grayscale: bool = False):
        super().__init__(parent)
        self._grayscale = grayscale
        self._text = text
        self._tokens = tokens
        self._byte_to_char = byte_to_char
        self._algorithm = algorithm
        self._current_step = 0
        self._is_playing = False
        self._setup_ui()

    def _setup_ui(self) -> None:
        try:
            self.setStyleSheet(ThemeManager.full_dialog_sheet())

            layout = QVBoxLayout(self)

            ctrl = QHBoxLayout()
            self._prev_btn = QPushButton("◀ 上一步")
            self._prev_btn.clicked.connect(self._step_prev)
            ctrl.addWidget(self._prev_btn)

            self._slider = QSlider(Qt.Orientation.Horizontal)
            self._slider.setMinimum(0)
            self._slider.setMaximum(max(len(self._tokens) - 1, 0))
            self._slider.setValue(0)
            self._slider.valueChanged.connect(self._on_slider)
            ctrl.addWidget(self._slider, stretch=1)

            self._next_btn = QPushButton("下一步 ▶")
            self._next_btn.clicked.connect(self._step_next)
            ctrl.addWidget(self._next_btn)

            self._play_btn = QPushButton("▶ 自动播放")
            self._play_btn.clicked.connect(self._toggle_play)
            ctrl.addWidget(self._play_btn)

            self._step_label = QLabel("0 / 0")
            self._step_label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 12px;")
            ctrl.addWidget(self._step_label)

            layout.addLayout(ctrl)

            self._text_display = QTextEdit()
            self._text_display.setReadOnly(True)
            font = QFont("Consolas", 10)
            font.setStyleHint(QFont.StyleHint.Monospace)
            self._text_display.setFont(font)
            self._text_display.setLineWrapMode(QTextEdit.LineWrapMode.WidgetWidth)
            self._text_display.setStyleSheet(
                f"QTextEdit {{ background: {ThemeManager.hex('bg_primary')}; color: {ThemeManager.hex('text_primary')}; "
                f"border: 1px solid {ThemeManager.hex('border_dark')}; border-radius: 8px; padding: 8px; }}"
            )
            layout.addWidget(self._text_display, stretch=1)

            self._detail_label = QLabel("")
            self._detail_label.setWordWrap(True)
            self._detail_label.setTextFormat(Qt.TextFormat.RichText)
            self._detail_label.setStyleSheet(f"color: {ThemeManager.hex('text_primary')}; font-size: 12px; font-family: Consolas; background: {ThemeManager.hex('bg_surface')}; padding: 10px; border-radius: 8px;")
            layout.addWidget(self._detail_label)

            self._render_step()

        except Exception as e:
            logger.error("[LZSliderWidget] _setup_ui failed: %s", e, exc_info=True)
            error_layout = QVBoxLayout(self)
            error_label = QLabel(f"初始化LZ滑块组件失败:\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            error_layout.addWidget(error_label)

    def _get_token_char_range(self, idx: int) -> tuple[int, int]:
        if idx < 0 or idx >= len(self._tokens):
            return 0, 0
        t = self._tokens[idx]
        bs = t.original_start
        be = min(bs + t.original_length, len(self._byte_to_char))
        cs = self._byte_to_char[bs] if bs < len(self._byte_to_char) else 0
        ce = self._byte_to_char[be] if be < len(self._byte_to_char) else len(self._text)
        return cs, ce

    def _get_prev_end(self, idx: int) -> int:
        if idx <= 0:
            return 0
        _, ce = self._get_token_char_range(idx - 1)
        return ce

    def _render_step(self) -> None:
        try:
            step = self._current_step
            n = len(self._tokens)
            logger.info("[LZSlider] _render_step step=%d/%d text_len=%d", step, n, len(self._text))
            self._step_label.setText(f"{step + 1} / {n}")

            logger.debug("[LZSlider] getting prev_end and char range for step=%d", step)
            prev_end = self._get_prev_end(step)
            cs, ce = self._get_token_char_range(step)
            logger.debug("[LZSlider] prev_end=%d cs=%d ce=%d text_len=%d", prev_end, cs, ce, len(self._text))

            logger.debug("[LZSlider] clearing text display")
            self._text_display.clear()
            cursor = self._text_display.textCursor()

            if prev_end > 0:
                logger.debug("[LZSlider] inserting gray text: %d chars", prev_end)
                fmt_gray = QTextCharFormat()
                fmt_gray.setForeground(ThemeManager.color('border_dark'))
                cursor.insertText(self._text[:prev_end], fmt_gray)

            if ce > cs:
                logger.debug("[LZSlider] inserting highlighted text: [%d:%d]", cs, ce)
                fmt_hl = QTextCharFormat()
                color = _ratio_to_qcolor(self._tokens[step].compression_ratio)
                bg = QColor(color)
                bg.setAlpha(140)
                fmt_hl.setBackground(QBrush(bg))
                fmt_hl.setForeground(ThemeManager.color('bg_surface'))
                fmt_hl.setFontWeight(700)
                cursor.insertText(self._text[cs:ce], fmt_hl)

            if ce < len(self._text):
                logger.debug("[LZSlider] inserting pending text: from %d to end", ce)
                fmt_pending = QTextCharFormat()
                fmt_pending.setForeground(ThemeManager.color('text_muted'))
                cursor.insertText(self._text[ce:], fmt_pending)

            logger.debug("[LZSlider] setting cursor position to %d", cs)
            highlight_cursor = self._text_display.textCursor()
            highlight_cursor.setPosition(cs)
            self._text_display.setTextCursor(highlight_cursor)
            self._text_display.ensureCursorVisible()

            logger.debug("[LZSlider] building detail label for token at step=%d", step)
            t = self._tokens[step]
            type_label = {TokenType.LITERAL: "LITERAL", TokenType.MATCH: "MATCH",
                          TokenType.LITERAL_RUN: "LITERAL_RUN"}.get(t.type, t.type.value)
            color = _ratio_to_qcolor(t.compression_ratio)
            color_name = color.name()
            detail = (
                f"Token #{step}   "
                f"{type_label}\n"
                f"位置: byte {t.original_start}~{t.original_start + t.original_length - 1} | "
                f"长度: {t.original_length}B | 编码: {t.compressed_size:.2f}B | "
                f"压缩率: {t.compression_ratio * 100:.1f}%"
            )
            if t.type == TokenType.MATCH:
                detail += f"\n        回退偏移: {t.match_offset} 字符"
            if t.huffman_bits > 0:
                detail += f"\n        Huffman: {t.huffman_bits:.1f}bit | {t.huffman_detail}\""
            logger.debug("[LZSlider] setting detail label text")
            self._detail_label.setText(detail)

            logger.info("[LZSlider] _render_step completed successfully for step=%d", step)

        except Exception as e:
            logger.error("[LZSlider] _render_step failed at step=%d: %s", self._current_step, e, exc_info=True)
            try:
                self._detail_label.setText(f"渲染错误 (step={self._current_step}):\n{str(e)}")
            except:
                pass

    def _on_slider(self, val) -> None:
        self._current_step = val
        self._render_step()

    def _step_prev(self) -> None:
        logger.debug("[LZSlider] _step_prev called, current_step=%d, total=%d", self._current_step, len(self._tokens))
        if self._current_step > 0:
            self._current_step -= 1
            logger.info("[LZSlider] _step_prev: moving to step=%d", self._current_step)
            self._slider.setValue(self._current_step)

    def _step_next(self) -> None:
        logger.debug("[LZSlider] _step_next called, current_step=%d, total=%d", self._current_step, len(self._tokens))
        if self._current_step < len(self._tokens) - 1:
            self._current_step += 1
            logger.info("[LZSlider] _step_next: moving to step=%d", self._current_step)
            self._slider.setValue(self._current_step)

    def _toggle_play(self) -> None:
        self._is_playing = not self._is_playing
        if self._is_playing:
            self._play_btn.setText("⏸ 暂停")
            from PyQt6.QtCore import QTimer
            if not hasattr(self, '_timer'):
                self._timer = QTimer(self)
                self._timer.timeout.connect(self._auto_step)
            self._timer.start(600)
        else:
            self._play_btn.setText("▶ 自动播放")
            if hasattr(self, '_timer'):
                self._timer.stop()

    def _auto_step(self) -> None:
        if self._current_step < len(self._tokens) - 1:
            self._current_step += 1
            self._slider.setValue(self._current_step)
        else:
            self._is_playing = False
            self._play_btn.setText("▶ 自动播放")
            if hasattr(self, '_timer'):
                self._timer.stop()


class LZSliderDialog(QDialog):
    def __init__(self, text: str, tokens: list[Token], byte_to_char: list[int],
                 filename: str, algorithm: str, dp_viz=None, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"LZ 算法步骤演示 - {filename}")
        self.resize(1200, 800)
        layout = QVBoxLayout(self)
        header = QLabel(f"🎚️ LZ 算法步骤演示文件: {filename} | 算法: {algorithm}")
        header.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(header)
        
        if algorithm.lower() in ('lzdp', 'dpflate') and dp_viz is not None:
            self._slider_widget = LZDPDPSliderWidget(text, tokens, byte_to_char, dp_viz, algorithm)
        else:
            self._slider_widget = LZSliderWidget(text, tokens, byte_to_char, algorithm)
            
        layout.addWidget(self._slider_widget, stretch=1)
        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.setStyleSheet(f"background:{ThemeManager.hex('border_dark')}; color:{ThemeManager.hex('text_primary')}; padding:6px 20px; border-radius:6px;")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


class FlateDemoDialog(QDialog):
    def __init__(self, text: str, tokens: list[Token], byte_to_char: list[int],
                 filename: str, algorithm: str, huffman_trees: list, dp_viz=None, parent=None):
        super().__init__(parent)
        self._text = text
        self._tokens = tokens
        self._byte_to_char = byte_to_char
        self._huffman_trees = huffman_trees or []
        self._dp_viz = dp_viz
        self.setWindowTitle(f"Flate 算法演示 - {filename}")
        self.resize(1300, 850)
        self._setup_ui(filename, algorithm)

    def _setup_ui(self, filename: str, algorithm: str) -> None:
        try:
            self.setStyleSheet(ThemeManager.full_dialog_sheet())

            layout = QVBoxLayout(self)
            header = QLabel(
                f"🔧 Flate 算法两层演示"
                f"文件: {filename} | 算法: {algorithm} | "
                f"Tokens: {len(self._tokens)} | Huffman 树: {len(self._huffman_trees)}"
            )
            header.setTextFormat(Qt.TextFormat.RichText)
            layout.addWidget(header)

            tabs = QTabWidget()

            lz_tab = self._build_lz_tab(algorithm)
            tabs.addTab(lz_tab, "🔧 第1层: LZ 压缩")

            if self._huffman_trees:
                huff_tab = self._build_huffman_tab()
                tabs.addTab(huff_tab, "🌳 第2层: Huffman 编码")

            layout.addWidget(tabs, stretch=1)

            btn_bar = QHBoxLayout()
            btn_bar.addStretch()
            close_btn = QPushButton("关闭")
            close_btn.setStyleSheet(f"background:{ThemeManager.hex('border_dark')}; color:{ThemeManager.hex('text_primary')}; padding:6px 24px; border-radius:6px;")
            close_btn.clicked.connect(self.accept)
            btn_bar.addWidget(close_btn)
            layout.addLayout(btn_bar)

        except Exception as e:
            logger.error("[FlateDemoDialog] _setup_ui failed: %s", e, exc_info=True)
            error_layout = QVBoxLayout(self)
            error_label = QLabel(f"初始化Flate算法演示失败:\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            error_layout.addWidget(error_label)

    def _build_lz_tab(self, algorithm: str) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)

        if algorithm.lower() == 'dpflate':
            desc = QLabel(
                f"Flate 算法第 1 层：将原始数据通过 DP 动态规划解析为 (literal, match) token 序列。\n"
                "拖动滑块逐步查看每个 token 如何通过最优路径覆盖原文。"
            )
            desc.setWordWrap(True)
            desc.setTextFormat(Qt.TextFormat.RichText)
            lay.addWidget(desc)

            if self._dp_viz:
                self._lz_slider = LZDPDPSliderWidget(self._text, self._tokens, self._byte_to_char, self._dp_viz, algorithm)
            else:
                self._lz_slider = LZSliderWidget(self._text, self._tokens, self._byte_to_char, algorithm)
        else:
            desc = QLabel(
                f"Flate 算法第 1 层：将原始数据通过 LZ77 (贪心匹配) 算法压缩为 (literal, match) token 序列。\n"
                "拖动滑块逐步查看每个 token 如何覆盖原文。"
            )
            desc.setWordWrap(True)
            desc.setTextFormat(Qt.TextFormat.RichText)
            lay.addWidget(desc)

            self._lz_slider = LZSliderWidget(self._text, self._tokens, self._byte_to_char, algorithm)

        lay.addWidget(self._lz_slider, stretch=1)
        return w

    def _build_huffman_tab(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)

        if len(self._huffman_trees) > 1:
            selector = QHBoxLayout()
            selector.addWidget(QLabel("选择 Huffman 树:"))
            self._tree_combo = QComboBox()
            for i, td in enumerate(self._huffman_trees):
                self._tree_combo.addItem(f"树 {i+1}: {td.tree_type}")
            self._tree_combo.currentIndexChanged.connect(self._on_tree_changed)
            selector.addWidget(self._tree_combo)
            selector.addStretch()
            lay.addLayout(selector)

        self._build_animator = HuffmanBuildAnimator(self._huffman_trees[0].codes)
        lay.addWidget(self._build_animator, stretch=1)

        code_bar = QHBoxLayout()
        code_label = QLabel("最终编码表")
        code_label.setStyleSheet(f"color:{ThemeManager.hex('text_secondary')};font-size:12px;")
        code_bar.addWidget(code_label)
        code_bar.addStretch()

        self._code_table = QTableWidget()
        self._code_table.setColumnCount(3)
        self._code_table.setHorizontalHeaderLabels(["符号", "Huffman 码", "码长"])
        self._code_table.horizontalHeader().setStretchLastSection(True)
        self._code_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._code_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._code_table.setMaximumHeight(180)
        self._code_table.setAlternatingRowColors(True)
        self._code_table.setStyleSheet(
            f"QTableWidget{{background:{ThemeManager.hex('bg_surface')};color:{ThemeManager.hex('text_primary')};border:1px solid {ThemeManager.hex('border_dark')};border-radius:8px;"
            f"gridline-color:{ThemeManager.hex('border_dark')};}}"
            "QTableWidget::item{padding:4px 10px;} "
            f"QHeaderView::section{{background:{ThemeManager.hex('bg_surface')};color:{ThemeManager.hex('text_secondary')};border:1px solid {ThemeManager.hex('border_dark')};padding:5px 10px;}}"
            f"QTableWidget{{alternate-background-color:{ThemeManager.hex('bg_hover')};}}"
        )
        self._populate_code_table(self._huffman_trees[0])
        lay.addLayout(code_bar)
        lay.addWidget(self._code_table)

        summary = QLabel("")
        summary.setWordWrap(True)
        summary.setTextFormat(Qt.TextFormat.RichText)
        td = self._huffman_trees[0]
        codes = td.codes
        total_freq = sum(e.frequency for e in codes)
        num_symbols = len(codes)
        max_len = max((e.code_length for e in codes), default=0)
        avg_len = sum(e.code_length * e.frequency for e in codes) / total_freq if total_freq > 0 else 0
        summary.setText(
            f""
            f"符号数: {num_symbols}  |  总频率: {total_freq}  |  "
            f"最大码长: {max_len} bit  |  平均码长: {avg_len:.2f} bit  |  "
            f"树类型: {td.tree_type}"
        )
        lay.addWidget(summary)
        return w

    def _on_tree_changed(self, idx: int) -> None:
        if 0 <= idx < len(self._huffman_trees):
            td = self._huffman_trees[idx]
            self._build_animator.deleteLater()
            self._build_animator = HuffmanBuildAnimator(td.codes)
            tab_widget = self.findChild(QTabWidget)
            if tab_widget:
                huff_tab = tab_widget.widget(1)
                if huff_tab:
                    layout = huff_tab.layout()
                    if layout:
                        layout.insertWidget(1, self._build_animator, stretch=1)
            self._populate_code_table(td)

    def _populate_code_table(self, td) -> None:
        codes = sorted(td.codes, key=lambda e: (-e.frequency, e.symbol))
        self._code_table.setRowCount(len(codes))
        for i, entry in enumerate(codes):
            sym_item = QTableWidgetItem(str(entry.symbol))
            sym_item.setForeground(ThemeManager.color('text_primary'))
            self._code_table.setItem(i, 0, sym_item)
            code_item = QTableWidgetItem(entry.code)
            code_item.setForeground(ThemeManager.color('text_secondary'))
            self._code_table.setItem(i, 1, code_item)
            len_item = QTableWidgetItem(f"{entry.code_length}")
            len_item.setForeground(ThemeManager.color('text_secondary'))
            self._code_table.setItem(i, 2, len_item)


class _BuildStep:
    __slots__ = ('step_idx', 'action', 'queue_snapshot', 'merged_a', 'merged_b', 'new_node', 'tree_root')
    def __init__(self, step_idx: int, action: str, queue_snapshot: list, merged_a=None,
                 merged_b=None, new_node=None, tree_root=None):
        self.step_idx = step_idx
        self.action = action
        self.queue_snapshot = queue_snapshot
        self.merged_a = merged_a
        self.merged_b = merged_b
        self.new_node = new_node
        self.tree_root = tree_root


class _QueueNode:
    __slots__ = ('node_id', 'symbol', 'freq', 'is_leaf', 'left_id', 'right_id', 'parent_id')
    def __init__(self, node_id: int, symbol: int | None, freq: int, is_leaf: bool,
                 left_id: int = -1, right_id: int = -1, parent_id: int = -1):
        self.node_id = node_id
        self.symbol = symbol
        self.freq = freq
        self.is_leaf = is_leaf
        self.left_id = left_id
        self.right_id = right_id
        self.parent_id = parent_id

    def _label(self) -> str:
        return f"'{chr(self.symbol)}'" if self.is_leaf and 32 <= self.symbol < 127 else f"#{self.node_id}"


def _replay_huffman_build(codes: list) -> tuple[list[_BuildStep], dict[int, _QueueNode]]:
    import heapq
    leaves = [(e.frequency, i, e.symbol) for i, e in enumerate(codes)]
    leaves.sort()
    nodes: dict[int, _QueueNode] = {}
    for i, (_, _, sym) in enumerate(leaves):
        nodes[i] = _QueueNode(i, sym, leaves[i][0], True)
    counter = len(leaves)
    heap = [(n.freq, n.node_id) for n in nodes.values()]
    heapq.heapify(heap)
    steps: list[_BuildStep] = []
    q_snap = sorted([(nodes[nid].symbol, nodes[nid].freq, nid) for _, nid in heap],
                     key=lambda x: (x[1], x[0] if x[0] is not None else 999999))
    steps.append(_BuildStep(0, "初始状态：所有叶子节点入队", q_snap))
    root_id = -1
    step_num = 1
    while len(heap) > 1:
        f1, id1 = heapq.heappop(heap)
        f2, id2 = heapq.heappop(heap)
        new_id = counter
        counter += 1
        new_freq = f1 + f2
        nodes[new_id] = _QueueNode(new_id, None, new_freq, False, id1, id2)
        nodes[id1].parent_id = new_id
        nodes[id2].parent_id = new_id
        heapq.heappush(heap, (new_freq, new_id))
        root_id = new_id
        q_snap = sorted([(nodes[nid].symbol, nodes[nid].freq, nid) for _, nid in heap],
                         key=lambda x: (x[1], x[0] if x[0] is not None else 999999))
        steps.append(_BuildStep(step_num, f"合并节点 {nodes[id1]._label()}(f={f1}) + {nodes[id2]._label()}(f={f2}) → 新内部节点(f={new_freq})",
        q_snap, nodes[id1], nodes[id2], nodes[new_id], root_id))
        step_num += 1
    if root_id >= 0 and heap:
        steps.append(_BuildStep(step_num, "建树完成！", [], tree_root=root_id))
    return steps, nodes


class _ZoomableTreeCanvas(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._scale = 1.0
        self._offset_x = 0.0
        self._offset_y = 0.0
        self._dragging = False
        self._last_pos = None
        self.setMinimumHeight(360)
        self.setMouseTracking(True)
        self._paint_fn = None

    def set_paint_fn(self, fn):
        self._paint_fn = fn

    def paintEvent(self, event):
        if self._paint_fn:
            self._paint_fn(event)
        else:
            painter = QPainter(self)
            painter.fillRect(0, 0, self.width(), self.height(), ThemeManager.color('bg_primary'))
            painter.end()

    def wheelEvent(self, event):
        delta = event.angleDelta().y()
        factor = 1.15 if delta > 0 else 1.0 / 1.15
        self._scale = max(0.3, min(self._scale * factor, 4.0))
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._dragging = True
            self._last_pos = event.position()

    def mouseMoveEvent(self, event):
        if self._dragging and self._last_pos is not None:
            pos = event.position()
            dx = pos.x() - self._last_pos.x()
            dy = pos.y() - self._last_pos.y()
            self._offset_x += dx / self._scale
            self._offset_y += dy / self._scale
            self._last_pos = pos
            self.update()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._dragging = False
            self._last_pos = None

    def zoom_in(self):
        self._scale = min(self._scale * 1.2, 4.0)
        self.update()

    def zoom_out(self):
        self._scale = max(self._scale / 1.2, 0.3)
        self.update()

    def reset_view(self):
        self._scale = 1.0
        self._offset_x = 0.0
        self._offset_y = 0.0
        self.update()


class HuffmanBuildAnimator(QWidget):
    def __init__(self, codes: list, parent=None, grayscale: bool = False):
        super().__init__(parent)
        self._grayscale = grayscale
        self._codes = codes or []
        self._steps: list[_BuildStep] = []
        self._nodes: dict[int, _QueueNode] = {}
        self._current_step = 0
        self._is_playing = False
        self._timer_id = -1
        self._setup_ui()
        self._precompute()

    def _setup_ui(self) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)

        ctrl = QHBoxLayout()
        self._prev_btn = QPushButton("◀ 上一步")
        self._prev_btn.clicked.connect(self._step_prev)
        ctrl.addWidget(self._prev_btn)
        self._slider = QSlider(Qt.Orientation.Horizontal)
        self._slider.setMinimum(0)
        self._slider.setMaximum(0)
        self._slider.valueChanged.connect(self._on_slider)
        ctrl.addWidget(self._slider, stretch=1)
        self._next_btn = QPushButton("下一步 ▶")
        self._next_btn.clicked.connect(self._step_next)
        ctrl.addWidget(self._next_btn)
        self._play_btn = QPushButton("▶ 自动播放")
        self._play_btn.clicked.connect(self._toggle_play)
        ctrl.addWidget(self._play_btn)
        self._step_label = QLabel("0 / 0")
        self._step_label.setStyleSheet(f"color:{ThemeManager.hex('text_secondary')};font-size:12px;")
        self._step_label.setFixedWidth(70)
        ctrl.addWidget(self._step_label)

        ctrl.addWidget(QLabel("  缩放:"))
        zoom_out_btn = QPushButton("−")
        zoom_out_btn.setFixedWidth(30)
        zoom_out_btn.clicked.connect(lambda: self._tree_canvas.zoom_out())
        ctrl.addWidget(zoom_out_btn)
        zoom_reset_btn = QPushButton("重置")
        zoom_reset_btn.setFixedWidth(40)
        zoom_reset_btn.clicked.connect(lambda: self._tree_canvas.reset_view())
        ctrl.addWidget(zoom_reset_btn)
        zoom_in_btn = QPushButton("+")
        zoom_in_btn.setFixedWidth(30)
        zoom_in_btn.clicked.connect(lambda: self._tree_canvas.zoom_in())
        ctrl.addWidget(zoom_in_btn)

        layout.addLayout(ctrl)

        self._info_label = QLabel("")
        self._info_label.setWordWrap(True)
        self._info_label.setTextFormat(Qt.TextFormat.RichText)
        self._info_label.setStyleSheet(f"color:{ThemeManager.hex('text_secondary')};font-size:12px;background:{ThemeManager.hex('bg_surface')};padding:8px;border-radius:6px;")
        layout.addWidget(self._info_label)

        body = QHBoxLayout()
        queue_group = QGroupBox("优先队列 (Min-Heap)")
        queue_group.setStyleSheet(f"QGroupBox{{color:{ThemeManager.hex('text_secondary')};border:1px solid {ThemeManager.hex('border_dark')};border-radius:8px;margin-top:10px;padding-top:14px;}}QGroupBox::title{{subcontrol-origin;margin;left:12px;color:{ThemeManager.hex('text_secondary')};}}")
        queue_layout = QVBoxLayout(queue_group)
        self._queue_display = QTextEdit()
        self._queue_display.setReadOnly(True)
        self._queue_display.setFont(QFont("Consolas", 11))
        self._queue_display.setStyleSheet(f"QTextEdit{{background:{ThemeManager.hex('bg_primary')};color:{ThemeManager.hex('text_primary')};border:1px solid {ThemeManager.hex('border_dark')};border-radius:6px;}}")
        self._queue_display.setMaximumWidth(320)
        queue_layout.addWidget(self._queue_display)
        body.addWidget(queue_group)

        tree_group = QGroupBox("Huffman 树构建过程（鼠标滚轮缩放 / 拖拽平移）")
        tree_group.setStyleSheet(f"QGroupBox{{color:{ThemeManager.hex('text_secondary')};border:1px solid {ThemeManager.hex('border_dark')};border-radius:8px;margin-top:10px;padding-top:14px;}}QGroupBox::title{{subcontrol-origin;margin;left:12px;color:{ThemeManager.hex('text_secondary')};}}")
        tree_layout = QVBoxLayout(tree_group)
        self._tree_canvas = _ZoomableTreeCanvas()
        self._tree_canvas.set_paint_fn(self._paint_tree)
        tree_layout.addWidget(self._tree_canvas, stretch=1)
        body.addWidget(tree_group, stretch=1)
        layout.addLayout(body, stretch=1)

    def _precompute(self) -> None:
        if not self._codes:
            return
        self._steps, self._nodes = _replay_huffman_build(self._codes)
        self._slider.setMaximum(len(self._steps) - 1)
        self._step_label.setText(f"0 / {len(self._steps)-1}")
        self._render_step()

    def _render_step(self) -> None:
        if not self._steps:
            return
        step = self._steps[self._current_step]
        self._info_label.setText(f"步骤 {step.step_idx}: {step.action}")

        lines = []
        for sym, freq, nid in step.queue_snapshot:
            label = f"'{chr(sym)}'" if sym is not None and 32 <= sym < 127 else f"#{nid}"
            lines.append(f"  [{label}]  频率 = {freq}")
        if not lines:
            lines.append("  （队列空，建树完成）")
        self._queue_display.setPlainText("优先队列（按频率升序）：\n" + "\n".join(lines))
        self._tree_canvas.update()
        self._prev_btn.setEnabled(self._current_step > 0)
        self._next_btn.setEnabled(self._current_step < len(self._steps) - 1)
        self._step_label.setText(f"{self._current_step} / {len(self._steps)-1}")
        self._slider.blockSignals(True)
        self._slider.setValue(self._current_step)
        self._slider.blockSignals(False)

    def _compute_width(self, node_id: int) -> int:
        node = self._nodes.get(node_id)
        if node is None or node.is_leaf:
            return 1
        return self._compute_width(node.left_id) + self._compute_width(node.right_id)

    def _layout_subtree(self, node_id: int, cx: float, cy: float,
                        available_width: float, positions: dict, depth: int = 0) -> None:
        node = self._nodes.get(node_id)
        if node is None or depth > 20:
            return
        positions[node_id] = (cx, cy)
        if node.is_leaf:
            return
        left_w = self._compute_width(node.left_id)
        right_w = self._compute_width(node.right_id)
        total_w = left_w + right_w
        if total_w == 0:
            return
        child_y = cy + 50
        left_cx = cx - available_width * (right_w / total_w) / 2
        right_cx = cx + available_width * (left_w / total_w) / 2
        self._layout_subtree(node.left_id, left_cx, child_y,
                             available_width * left_w / total_w, positions, depth + 1)
        self._layout_subtree(node.right_id, right_cx, child_y,
                             available_width * right_w / total_w, positions, depth + 1)

    def _draw_tree_edges(self, painter: QPainter, root_id: int, positions: dict) -> None:
        node = self._nodes.get(root_id)
        if node is None or node.is_leaf:
            return
        pen = QPen(ThemeManager.color('border_dark'), 1.5)
        painter.setPen(pen)
        px, py = positions[root_id]
        if node.left_id in positions:
            lx, ly = positions[node.left_id]
            painter.drawLine(int(px), int(py) + 14, int(lx), int(ly) - 10)
            self._draw_tree_edges(painter, node.left_id, positions)
        if node.right_id in positions:
            rx, ry = positions[node.right_id]
            painter.drawLine(int(px), int(py) + 14, int(rx), int(ry) - 10)
            self._draw_tree_edges(painter, node.right_id, positions)

    def _draw_tree_nodes(self, painter: QPainter, root_id: int, positions: dict,
                         highlight_id: int = -1) -> None:
        node = self._nodes.get(root_id)
        if node is None or root_id not in positions:
            return
        nx, ny = positions[root_id]
        node_r = 13

        if node.is_leaf:
            color = ThemeManager.color('success')
            text_color = QColor("#fffff")
        elif root_id == highlight_id:
            color = ThemeManager.color('accent')
            text_color = QColor("#fffff")
        else:
            color = ThemeManager.color('text_muted')
            text_color = ThemeManager.color('bg_primary')

        painter.setBrush(QBrush(color))
        painter.setPen(QPen(color.darker(120), 1.5))
        painter.drawEllipse(QRectF(nx - node_r, ny - node_r, node_r * 2, node_r * 2))

        if node.is_leaf:
            if node.symbol is not None and 32 <= node.symbol < 127:
                label = chr(node.symbol)
            elif node.symbol is not None:
                label = str(node.symbol)
            else:
                label = ""
        else:
            label = ""
        painter.setPen(text_color)
        painter.setFont(QFont("Consolas", 9, QFont.Weight.Bold))
        painter.drawText(QRectF(nx - node_r, ny - 8, node_r * 2, 16),
                         Qt.AlignmentFlag.AlignCenter, label)
        painter.setFont(QFont("Consolas", 8))
        painter.setPen(ThemeManager.color('text_secondary'))
        painter.drawText(QRectF(nx - 20, ny + node_r - 2, 40, 14),
                         Qt.AlignmentFlag.AlignCenter, str(node.freq))

        if not node.is_leaf:
            if node.left_id in positions:
                self._draw_tree_nodes(painter, node.left_id, positions, highlight_id)
            if node.right_id in positions:
                self._draw_tree_nodes(painter, node.right_id, positions, highlight_id)

    def _paint_tree(self, event) -> None:
        painter = QPainter(self._tree_canvas)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w = self._tree_canvas.width()
        h = self._tree_canvas.height()
        painter.fillRect(0, 0, w, h, ThemeManager.color('bg_primary'))

        if not self._steps or self._current_step >= len(self._steps):
            painter.end()
            return

        scale = self._tree_canvas._scale
        ox = self._tree_canvas._offset_x
        oy = self._tree_canvas._offset_y
        painter.translate(ox, oy)
        painter.scale(scale, scale)

        ew = w / scale
        eh = h / scale

        step = self._steps[self._current_step]

        if step.merged_a is not None and step.merged_b is not None:
            section_w = ew / 3
            tree_top = 36
            tree_h = eh - tree_top - 10

            pen = QPen(ThemeManager.color('border_dark'), 1, Qt.PenStyle.DashLine)
            painter.setPen(pen)
            painter.drawLine(int(section_w), int(tree_top - 4), int(section_w), int(eh))
            painter.drawLine(int(section_w * 2), int(tree_top - 4), int(section_w * 2), int(eh))

            painter.setPen(ThemeManager.color('text_secondary'))
            painter.setFont(QFont("Microsoft YaHei", 11))
            painter.drawText(QRectF(0, 4, section_w, 24), Qt.AlignmentFlag.AlignCenter, "左子树")
            painter.drawText(QRectF(section_w, 4, section_w, 24), Qt.AlignmentFlag.AlignCenter, "右子树")
            painter.drawText(QRectF(section_w * 2, 4, section_w, 24), Qt.AlignmentFlag.AlignCenter, "合并结果")

            spread = min(section_w * 0.42, 200)

            pos_a: dict[int, tuple[float, float]] = {}
            self._layout_subtree(step.merged_a.node_id, section_w * 0.5,
                                 tree_top + tree_h * 0.06, spread, pos_a)
            self._draw_tree_edges(painter, step.merged_a.node_id, pos_a)
            self._draw_tree_nodes(painter, step.merged_a.node_id, pos_a,
                                  highlight_id=step.merged_a.node_id)

            pos_b: dict[int, tuple[float, float]] = {}
            self._layout_subtree(step.merged_b.node_id, section_w * 1.5,
                                 tree_top + tree_h * 0.06, spread, pos_b)
            self._draw_tree_edges(painter, step.merged_b.node_id, pos_b)
            self._draw_tree_nodes(painter, step.merged_b.node_id, pos_b,
                                  highlight_id=step.merged_b.node_id)

            pos_m: dict[int, tuple[float, float]] = {}
            self._layout_subtree(step.tree_root, section_w * 2.5,
                                 tree_top + tree_h * 0.06, spread, pos_m)
            self._draw_tree_edges(painter, step.tree_root, pos_m)
            highlight = step.new_node.node_id if step.new_node else -1
            self._draw_tree_nodes(painter, step.tree_root, pos_m, highlight_id=highlight)
        elif step.tree_root is not None:
            pos: dict[int, tuple[float, float]] = {}
            spread = min(ew * 0.42, 280)
            self._layout_subtree(step.tree_root, ew * 0.5, 40, spread, pos)
            self._draw_tree_edges(painter, step.tree_root, pos)
            self._draw_tree_nodes(painter, step.tree_root, pos)
        else:
            painter.setPen(ThemeManager.color('text_secondary'))
            painter.setFont(QFont("Microsoft YaHei", 12))
            painter.drawText(QRectF(0, 0, ew, eh), Qt.AlignmentFlag.AlignCenter, "等待建树...")

        painter.end()

    def _step_prev(self) -> None:
        if self._current_step > 0:
            self._current_step -= 1
            self._render_step()

    def _step_next(self) -> None:
        if self._current_step < len(self._steps) - 1:
            self._current_step += 1
            self._render_step()

    def _on_slider(self, val: int) -> None:
        self._current_step = val
        self._render_step()

    def _toggle_play(self) -> None:
        if self._is_playing:
            self._stop_play()
        else:
            self._start_play()

    def _start_play(self) -> None:
        if self._current_step >= len(self._steps) - 1:
            self._current_step = 0
            self._render_step()
        self._is_playing = True
        self._play_btn.setText("⏸ 暂停")
        self._timer_id = self.startTimer(800)

    def _stop_play(self) -> None:
        self._is_playing = False
        self._play_btn.setText("▶ 自动播放")
        if self._timer_id >= 0:
            self.killTimer(self._timer_id)
            self._timer_id = -1

    def timerEvent(self, event) -> None:
        if event.timerId() == self._timer_id:
            if self._current_step < len(self._steps) - 1:
                self._current_step += 1
                self._render_step()
            else:
                self._stop_play()


class ComparisonDialog(QDialog):
    def __init__(self, results: list[dict], filename: str = "unknown",
                 original_size: int = 0, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"算法压缩对比 - {filename}")
        self.resize(800, 600)
        self._setup_ui(results, filename, original_size)

    def _setup_ui(self, results: list[dict], filename: str, original_size: int) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)

        header = QLabel(
            f"📊 算法压缩对比"
            f"文件: {filename}  |  原始大小: {original_size:,} B"
        )
        header.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(header)

        legend = QLabel(
            f""
            "压缩率: "
            f"● 低(好)  → "
            f"● 中  → "
            f"● 高(差)   "
            f"(色彩连续渐变)"
        )
        legend.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(legend)

        bar_group = QGroupBox("压缩率对比")
        bar_layout = QVBoxLayout(bar_group)

        max_ratio = max((r["ratio"] for r in results), default=0.01)
        for r in results:
            row = QHBoxLayout()
            label = QLabel(r["name"])
            label.setFixedWidth(100)
            label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 13px;")
            row.addWidget(label)
            bar = QProgressBar()
            bar.setMinimum(0)
            bar.setMaximum(1000)
            pct = int((r["ratio"] / max_ratio) * 1000) if max_ratio > 0 else 0
            bar.setValue(pct)
            bar.setFormat(f"{r['ratio'] * 100:.1f}%")

            c = _ratio_to_qcolor(r["ratio"])
            bar.setStyleSheet(
                f"QProgressBar {{ border: 1px solid {ThemeManager.hex('border_dark')}; border-radius: 6px; background: {ThemeManager.hex('bg_surface')}; text-align: center; color: {ThemeManager.hex('bg_primary')}; font-weight: 600; font-size: 12px; min-height: 24px; }}"
                f"QProgressBar::chunk {{ border-radius: 5px; background: {c.name()}; }}"
            )
            row.addWidget(bar, stretch=1)

            time_label = QLabel(f"{r['time_ms']:.1f} ms")
            time_label.setFixedWidth(80)
            time_label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            time_label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 12px;")
            row.addWidget(time_label)

            bar_layout.addLayout(row)

        layout.addWidget(bar_group)

        table_group = QGroupBox("详细数据")
        table_layout = QVBoxLayout(table_group)
        table = QTableWidget()
        table.setColumnCount(5)
        table.setHorizontalHeaderLabels(["算法", "压缩后", "压缩率", "耗时", "节省"])
        table.horizontalHeader().setStretchLastSection(True)
        table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        table.setAlternatingRowColors(True)
        table.setStyleSheet(table.styleSheet() + f"QTableWidget {{ alternate-background-color: {ThemeManager.hex('bg_hover')}; }}")
        table.setRowCount(len(results))

        best_ratio = min(r["ratio"] for r in results)
        for i, r in enumerate(results):
            name_item = QTableWidgetItem(r["name"])
            name_item.setForeground(ThemeManager.color('text_primary'))
            table.setItem(i, 0, name_item)

            size_item = QTableWidgetItem(f"{r['compressed_size']:,} B")
            size_item.setForeground(ThemeManager.color('text_primary'))
            table.setItem(i, 1, size_item)

            ratio_item = QTableWidgetItem(f"{r['ratio'] * 100:.2f}%")
            ratio_item.setForeground(ThemeManager.color('text_secondary') if r[f"ratio"] == best_ratio else ThemeManager.color('text_primary'))
            table.setItem(i, 2, ratio_item)

            time_item = QTableWidgetItem(f"{r['time_ms']:.1f} ms")
            time_item.setForeground(ThemeManager.color('text_primary'))
            table.setItem(i, 3, time_item)

            saving = original_size - r["compressed_size"]
            saving_text = f"-{saving:,} B" if saving > 0 else "+"
            saving_item = QTableWidgetItem(saving_text)
            saving_item.setForeground(ThemeManager.color('text_secondary'))
            table.setItem(i, 4, saving_item)

        table.resizeColumnsToContents()
        table_layout.addWidget(table)
        layout.addWidget(table_group, stretch=1)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.setStyleSheet(f"background:{ThemeManager.hex('border_dark')}; color:{ThemeManager.hex('text_primary')}; padding:6px 20px; border-radius:6px;")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)
