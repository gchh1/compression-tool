"""DEFLATE / DPFlate two-layer demo dialog + Huffman build animator."""

from __future__ import annotations

import logging

from PyQt6.QtCore import Qt, QRectF
from PyQt6.QtGui import QColor, QPainter, QPen, QBrush, QFont
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QDialog,
    QSlider, QGroupBox, QTextEdit, QTableWidget, QTableWidgetItem,
    QAbstractItemView, QComboBox, QTabWidget,
)

logger = logging.getLogger(__name__)

from gui.engine.token_parser import Token
from gui.config.theme import ThemeManager
from gui.ui.dialogs.lz_demo_dialog import LZDPDPSliderWidget, LZSliderWidget


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

