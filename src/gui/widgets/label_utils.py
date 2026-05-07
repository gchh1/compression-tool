from __future__ import annotations

from PyQt6.QtWidgets import QLabel
from gui.core.theme import ThemeManager


def create_bold_label(text: str) -> QLabel:
    """创建加粗标签"""
    label = QLabel(text)
    label.setStyleSheet("font-weight: bold")
    return label


def create_italic_label(text: str) -> QLabel:
    """创建斜体标签"""
    label = QLabel(text)
    label.setStyleSheet("font-style: italic")
    return label


def create_styled_label(text: str, color_token: str = None,
                        font_size: int = None, bold: bool = False) -> QLabel:
    """创建带样式的标签"""
    label = QLabel(text)
    style_parts = []
    if color_token:
        style_parts.append(f"color: {ThemeManager.hex(color_token)}")
    if font_size:
        style_parts.append(f"font-size: {font_size}px")
    if bold:
        style_parts.append("font-weight: bold")
    if style_parts:
        label.setStyleSheet("; ".join(style_parts))
    return label


def set_label_html_free(label: QLabel, text: str,
                        color_token: str = None, font_size: int = None):
    """设置标签文本（纯Qt方式，替代HTML）"""
    label.setText(text)
    style_parts = []
    if color_token:
        style_parts.append(f"color: {ThemeManager.hex(color_token)}")
    if font_size:
        style_parts.append(f"font-size: {font_size}px")
    if style_parts:
        current = label.styleSheet() or ""
        label.setStyleSheet(current + ("; " if current else "") + "; ".join(style_parts))


def create_color_dot(color_token: str, text: str = "●") -> QLabel:
    """创建带颜色圆点的标签（用于图例）"""
    label = QLabel(text)
    label.setStyleSheet(f"color: {ThemeManager.hex(color_token)}; font-size: 14px;")
    return label
