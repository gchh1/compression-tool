"""LZ / LZDP step-by-step demo widgets and dialogs."""

from __future__ import annotations

import logging

from PyQt6.QtCore import Qt, QRect, QRectF, QSize, pyqtSignal
from PyQt6.QtGui import (
    QPainter, QColor, QFont, QPen, QTextCharFormat, QTextCursor,
    QBrush, QLinearGradient,
)
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTextEdit, QSlider, QScrollArea, QDialog,
    QSplitter, QGroupBox,
    QTableWidget, QTableWidgetItem, QAbstractItemView,
)

logger = logging.getLogger(__name__)

from gui.engine.token_parser import Token, TokenType, LZDPTokenParser
from gui.models import AlgorithmType
from gui.config.theme import ThemeManager
from gui.ui.views.visualizers.token_heatmap import ratio_to_qcolor as _ratio_to_qcolor

# 与热力图相同：颜色由 token_heatmap.ratio_to_qcolor(ratio) 决定；改渐变请编辑该函数。
# 仅调整演示里「匹配段」背景不透明度时改下面常量即可。
LZ_DEMO_MATCH_HIGHLIGHT_ALPHA = 130


def _lzdp_candidate_compression_ratio(c: dict) -> float:
    """与 LZDPTokenParser 构造 Token 时的 compressed/original 口径一致。"""
    sb = LZDPTokenParser.SEARCH_BYTELENGTH
    lb = LZDPTokenParser.LOOKAHEAD_BYTELENGTH
    o = int(c.get("offset", 0) or 0)
    ln = int(c.get("length", 0) or 0)
    if o == 0 and ln == 0:
        orig = 1.0
        comp = float(sb + lb + 1)
    elif o == 0:
        orig = float(max(ln, 1))
        comp = float(sb + lb + orig)
    else:
        orig = float(max(ln + 1, 1))
        comp = float(sb + lb + 1)
    return comp / orig


def _char_format_for_token_ratio(ratio: float) -> QTextCharFormat:
    c = _ratio_to_qcolor(ratio)
    bg = QColor(c)
    bg.setAlpha(LZ_DEMO_MATCH_HIGHLIGHT_ALPHA)
    fmt = QTextCharFormat()
    fmt.setBackground(QBrush(bg))
    lum = (0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()) / 255.0
    fmt.setForeground(QColor(15, 23, 42) if lum > 0.62 else QColor(248, 250, 252))
    fmt.setFontWeight(700)
    return fmt


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

        from gui.ui.panels.info_panel import InfoPanel

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
                if has_steps and best_cand:
                    r_hl = _lzdp_candidate_compression_ratio(best_cand)
                elif not has_steps and step < len(self._tokens):
                    r_hl = self._tokens[step].compression_ratio
                else:
                    r_hl = 1.0
                fmt_hl = _char_format_for_token_ratio(r_hl)
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
                from gui.ui.panels.info_panel import create_dp_literal_info
                new_panel = create_dp_literal_info(cur_pos, cur_state)
            else:
                from gui.ui.panels.info_panel import create_dp_empty_info
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
        from gui.ui.panels.info_panel import create_dp_step_info
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
                fmt_hl = _char_format_for_token_ratio(self._tokens[step].compression_ratio)
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

