"""Huffman tree visualization from code lengths."""

from __future__ import annotations

import math

from PyQt6.QtCore import Qt, QRectF, pyqtSignal
from PyQt6.QtGui import (
    QColor, QPainter, QPen, QFont, QFontMetrics, QPainterPath,
    QRadialGradient,
)
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QLabel


def _code_lengths_to_codes(ll_lengths: list[int],
                           dist_lengths: list[int] | None = None
                           ) -> tuple[dict[int, str], dict[int, str]]:
    """Build canonical Huffman codes from code lengths.

    Returns (ll_codes: {symbol: bitstring}, dist_codes: {symbol: bitstring}).
    """
    def _build(lengths: list[int]) -> dict[int, str]:
        # Count symbols per length
        bl_count: dict[int, int] = {}
        for l in lengths:
            if l > 0:
                bl_count[l] = bl_count.get(l, 0) + 1

        if not bl_count:
            return {}

        max_len = max(bl_count.keys())
        code = 0
        next_code: dict[int, int] = {}
        for bits in range(1, max_len + 1):
            code = (code + bl_count.get(bits - 1, 0)) << 1
            next_code[bits] = code

        codes: dict[int, str] = {}
        for sym, l in enumerate(lengths):
            if l > 0:
                c = next_code[l]
                next_code[l] += 1
                codes[sym] = format(c, f'0{l}b')
        return codes

    ll = _build(list(ll_lengths))
    d = _build(list(dist_lengths)) if dist_lengths else {}
    return ll, d


class HuffmanTreeWidget(QWidget):
    """Interactive Huffman tree visualization using QPainter.

    Shows the Literal/Length tree or Distance tree as a binary tree layout.
    Leaf nodes: rounded rectangles, size proportional to symbol frequency.
    Internal nodes: small circles.
    """

    NODE_RADIUS = 8
    LEAF_W_MIN = 28
    LEAF_H_MIN = 18
    H_GAP = 10
    V_GAP = 40
    MARGIN = 30

    def __init__(self, parent=None):
        super().__init__(parent)
        self._codes: dict[int, str] = {}
        self._title: str = "Huffman 树"
        self._selected_sym: int = -1
        self._hovered_sym: int = -1
        self._nodes: list = []  # [(x, y, is_leaf, symbol, code_str)]
        self._edges: list = []  # [(x1, y1, x2, y2)]
        self.setMouseTracking(True)
        self.setMinimumSize(400, 300)

    def set_ll_tree(self, ll_lengths: list[int]) -> None:
        """Set from Literal/Length code lengths (286 entries)."""
        codes, _ = _code_lengths_to_codes(ll_lengths)
        self._codes = codes
        self._title = f"Huffman 树 — Literal/Length ({len(codes)} symbols)"
        self._layout()
        self.update()

    def set_dist_tree(self, dist_lengths: list[int]) -> None:
        """Set from Distance code lengths (30 entries)."""
        _, codes = _code_lengths_to_codes([], dist_lengths)
        self._codes = codes
        self._title = f"Huffman 树 — Distance ({len(codes)} symbols)"
        self._layout()
        self.update()

    def set_block_index(self, idx: int) -> None:
        if not self._codes:
            return
        self._title = f"Huffman 树 — Block #{idx}"

    def _layout(self) -> None:
        """Recursively layout the Huffman tree from code strings."""
        self._nodes.clear()
        self._edges.clear()

        if not self._codes:
            return

        # Build a prefix-code tree from the codes
        # Tree: (left, right, symbol_or_none)
        # -1 means internal node
        root = {}

        for sym, code in sorted(self._codes.items(), key=lambda x: (len(x[1]), x[1])):
            node = root
            for bit in code[:-1]:
                key = int(bit)
                if key not in node:
                    node[key] = {}
                elif isinstance(node[key], int):
                    # Invalid: prefix collision — replace with internal node
                    node[key] = {}
                node = node[key]
            key = int(code[-1])
            if isinstance(node.get(key), dict):
                node[key] = {}  # shouldn't happen for valid codes
            node[key] = sym  # leaf

        # Calculate subtree sizes for leaf spacing
        def leaf_count(t) -> int:
            if isinstance(t, int):
                return 1
            count = 0
            if 0 in t:
                count += leaf_count(t[0])
            if 1 in t:
                count += leaf_count(t[1])
            return count

        total_leaves = leaf_count(root)

        # Leaf spacing
        leaf_w = self.LEAF_W_MIN + self.H_GAP
        total_w = total_leaves * leaf_w
        max_depth = max(len(c) for c in self._codes.values()) if self._codes else 1

        # Assign positions in-order
        leaf_idx = 0

        def layout_subtree(t: dict | int, depth: int, x_offset: float,
                           x_scale: float):
            nonlocal leaf_idx
            if isinstance(t, int):
                sym = t
                code = self._codes.get(sym, '')
                x = x_offset + leaf_idx * x_scale
                leaf_idx += 1
                y = self.MARGIN + depth * self.V_GAP
                self._nodes.append((x, y, True, sym, code))
                return x
            else:
                x_children = []
                for k in (0, 1):
                    if k in t:
                        cx = layout_subtree(t[k], depth + 1, x_offset, x_scale)
                        x_children.append(cx)
                    else:
                        x_children.append(None)

                if all(c is not None for c in x_children):
                    x = (x_children[0] + x_children[1]) / 2
                else:
                    x = x_children[0] if x_children[0] is not None else x_children[1]

                y = self.MARGIN + depth * self.V_GAP
                self._nodes.append((x, y, False, -1, ''))

                for cx in x_children:
                    if cx is not None:
                        self._edges.append((x, y, cx,
                                            self.MARGIN + (depth + 1) * self.V_GAP))
                return x

        x_scale = max(leaf_w, total_w / max(total_leaves, 1))
        layout_subtree(root, 0, self.MARGIN, x_scale)

        # Update widget size
        max_y = self.MARGIN + max_depth * self.V_GAP + self.MARGIN
        self.setMinimumHeight(int(max_y))

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Background
        painter.fillRect(self.rect(), QColor(250, 250, 250))

        if not self._codes:
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                             f"{self._title}\n(no data)")
            painter.end()
            return

        font = QFont("Monospace", 9)
        bold_font = QFont("Monospace", 9)
        bold_font.setBold(True)
        fm = QFontMetrics(font)

        # Draw edges
        edge_pen = QPen(QColor(180, 180, 180), 1)
        for x1, y1, x2, y2 in self._edges:
            painter.setPen(edge_pen)
            painter.drawLine(int(x1), int(y1 + self.NODE_RADIUS),
                             int(x2), int(y2 - self.NODE_RADIUS))

        # Draw nodes (internal first, then leaves on top)
        for x, y, is_leaf, sym, code in self._nodes:
            if is_leaf:
                continue

            highlight = (sym == self._hovered_sym or sym == self._selected_sym)
            r = self.NODE_RADIUS
            if highlight:
                painter.setBrush(QColor(100, 160, 255))
            else:
                painter.setBrush(QColor(200, 200, 200))
            painter.setPen(QPen(QColor(150, 150, 150), 1))
            painter.drawEllipse(QRectF(x - r, y - r, r * 2, r * 2))

        for x, y, is_leaf, sym, code in self._nodes:
            if not is_leaf:
                continue

            highlight = (sym == self._hovered_sym or sym == self._selected_sym)

            # Label text
            if sym < 256:
                if 0x20 <= sym < 0x7f:
                    label = chr(sym) if sym != 0x20 else '␣'
                else:
                    label = f'{sym:02X}'
            elif sym == 256:
                label = 'EOF'
            else:
                label = f'L{sym}'

            text_w = max(fm.horizontalAdvance(label),
                         fm.horizontalAdvance(code)) + 8
            leaf_w = max(self.LEAF_W_MIN, text_w)
            leaf_h = self.LEAF_H_MIN + 14  # extra for code line

            # Background
            path = QPainterPath()
            path.addRoundedRect(QRectF(x - leaf_w / 2, y - leaf_h / 2,
                                       leaf_w, leaf_h), 4, 4)

            if highlight:
                painter.setBrush(QColor(255, 220, 100))
                painter.setPen(QPen(QColor(200, 150, 0), 2))
            else:
                depth = len(code)
                v = max(120, 255 - depth * 20)
                painter.setBrush(QColor(220, v, 220))
                painter.setPen(QPen(QColor(120, 160, 120), 1))

            painter.drawPath(path)

            # Symbol text
            painter.setFont(bold_font)
            painter.setPen(QColor(0, 0, 0))
            painter.drawText(QRectF(x - leaf_w / 2, y - leaf_h / 2 + 1,
                                    leaf_w, leaf_h / 2),
                             Qt.AlignmentFlag.AlignHCenter |
                             Qt.AlignmentFlag.AlignBottom,
                             label)

            # Code text
            painter.setFont(font)
            painter.setPen(QColor(80, 80, 80))
            painter.drawText(QRectF(x - leaf_w / 2, y + 1,
                                    leaf_w, leaf_h / 2),
                             Qt.AlignmentFlag.AlignHCenter |
                             Qt.AlignmentFlag.AlignTop,
                             code)

        # Title
        title_font = QFont("Sans", 11, QFont.Weight.Bold)
        painter.setFont(title_font)
        painter.setPen(QColor(60, 60, 60))
        painter.drawText(QRectF(0, 4, self.width(), 24),
                         Qt.AlignmentFlag.AlignCenter,
                         self._title)

        painter.end()

    def _node_at(self, px: float, py: float) -> int:
        """Return symbol index if a leaf was hit, otherwise -1."""
        for x, y, is_leaf, sym, code in reversed(self._nodes):
            if not is_leaf:
                continue
            leaf_w = max(self.LEAF_W_MIN, 36)
            leaf_h = self.LEAF_H_MIN + 14
            if (abs(px - x) < leaf_w / 2 and abs(py - y) < leaf_h / 2):
                return sym
        return -1

    def mouseMoveEvent(self, event) -> None:
        sym = self._node_at(event.position().x(), event.position().y())
        if sym != self._hovered_sym:
            self._hovered_sym = sym
            self.update()

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._selected_sym = self._hovered_sym
            self.update()

    def leaveEvent(self, event) -> None:
        self._hovered_sym = -1
        self.update()


class HuffmanTreePanel(QWidget):
    """Panel wrapping Huffman trees for both LL and Distance trees."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._label = QLabel("Huffman 树")
        self._label.setStyleSheet("font-weight: bold; padding: 4px;")
        layout.addWidget(self._label)

        self._ll_tree = HuffmanTreeWidget()
        self._dist_tree = HuffmanTreeWidget()
        layout.addWidget(self._ll_tree)
        layout.addWidget(self._dist_tree)

    def set_block(self, block: dict) -> None:
        idx = block.get('block_index', 0)
        ll_lengths = block.get('ll_code_lengths', [])
        dist_lengths = block.get('dist_code_lengths', [])

        if ll_lengths:
            self._ll_tree.set_block_index(idx)
            self._ll_tree.set_ll_tree(ll_lengths)
        if dist_lengths:
            self._dist_tree.set_block_index(idx)
            self._dist_tree.set_dist_tree(dist_lengths)

        self._label.setText(
            f"Huffman 树 — Block #{idx} "
            f"({sum(1 for l in ll_lengths if l > 0)} LL symbols, "
            f"{sum(1 for l in dist_lengths if l > 0)} Dist symbols)"
        )
