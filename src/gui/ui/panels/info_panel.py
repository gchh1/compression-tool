from __future__ import annotations

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QFrame
)
from PyQt6.QtCore import Qt

from gui.config.theme import ThemeManager


class InfoPanel(QWidget):
    """纯Qt实现的信息面板，替代QTextBrowser HTML渲染"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(8, 8, 8, 8)
        self._layout.setSpacing(4)

    def clear(self):
        """清空面板内容"""
        while self._layout.count():
            item = self._layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

    def add_title(self, text: str, color_token: str = "warning"):
        """添加标题行"""
        label = QLabel(text)
        label.setStyleSheet(
            f"font-weight: bold; color: {ThemeManager.hex(color_token)};"
        )
        self._layout.addWidget(label)
        self.add_spacing()

    def add_info_row(self, label_text: str, value_text: str,
                     value_color: str = None, value_bold: bool = True):
        """添加信息行（标签: 值）"""
        row = QHBoxLayout()
        row.setSpacing(8)

        label = QLabel(label_text)
        label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')};")
        row.addWidget(label)

        value = QLabel(value_text)
        style_parts = []
        if value_bold:
            style_parts.append("font-weight: bold")
        if value_color:
            style_parts.append(f"color: {value_color}")
        if style_parts:
            value.setStyleSheet("; ".join(style_parts))
        row.addWidget(value)

        row.addStretch()
        self._add_layout(row)

    def add_text(self, text: str, color_token: str = "text_muted",
                 font_size: int = 10):
        """添加普通文本"""
        label = QLabel(text)
        label.setStyleSheet(
            f"color: {ThemeManager.hex(color_token)}; font-size: {font_size}px;"
        )
        label.setWordWrap(True)
        self._layout.addWidget(label)

    def add_spacing(self, size: int = 8):
        """添加间距"""
        self._layout.addSpacing(size)

    def _add_layout(self, layout):
        """添加子布局"""
        container = QWidget()
        container.setLayout(layout)
        self._layout.addWidget(container)


def create_dp_literal_info(cur_pos, cur_state) -> InfoPanel:
    """创建DP字面量信息面板"""
    panel = InfoPanel()
    
    pred_str = str(cur_state.predecessor) if cur_state.predecessor != 18446744073709551615 else "无(起点)"
    
    panel.add_title(f"LITERAL @ pos {cur_pos}", "warning")
    panel.add_info_row("token 数:", str(cur_state.token_count))
    panel.add_info_row("前驱:", pred_str, ThemeManager.hex("chart_5"))
    
    byte_val = cur_state.choice.literal
    byte_str = f"0x{byte_val:02X}"
    panel.add_info_row("字节:", byte_str)
    
    panel.add_spacing()
    panel.add_text(
        "此位置为字面量输出\n"
        "(offset=0, 无有效匹配或\n"
        "匹配长度小于最小阈值)"
    )
    
    return panel


def create_dp_empty_info() -> InfoPanel:
    """创建DP空状态信息面板"""
    panel = InfoPanel()
    panel.add_text(
        "当前位置无 DP 候选\n"
        "(可能为 LITERAL token，无需匹配搜索)",
        "text_secondary", 12
    )
    return panel


def create_dp_step_info(dp_step, candidates, best_tc, cur_state) -> InfoPanel:
    """创建DP步骤信息面板"""
    panel = InfoPanel()
    
    panel.add_title(f"DP 步骤 @ 位置 {dp_step.position}", "text_secondary")
    panel.add_info_row("候选数量:", str(len(candidates)))
    panel.add_info_row("最优 token 计数:", str(best_tc), ThemeManager.hex("success"))
    
    if cur_state is not None:
        pred_str = str(cur_state.predecessor) if cur_state.predecessor != 18446744073709551615 else "无(起点)"
        panel.add_info_row("前驱位置:", pred_str, ThemeManager.hex("chart_5"))
        
        choice = cur_state.choice
        panel.add_info_row(
            "选择:",
            f"(off={choice.offset}, len={choice.length})"
        )
    
    panel.add_spacing()
    panel.add_text(
        f"DP 从 {len(candidates)} 个候选匹配中\n"
        f"选择使总 token 数最少的方案\n"
        f"贪心策略只看最长匹配，\n"
        f"DP 考虑全局最优"
    )
    
    return panel
