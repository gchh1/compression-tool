from __future__ import annotations

import logging
from dataclasses import dataclass, fields

from PyQt6.QtGui import QColor

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class Theme:
    bg_primary: str = "#ffffff"
    bg_surface: str = "#f8f9fa"
    bg_elevated: str = "#ffffff"
    bg_hover: str = "#f0f1f3"
    bg_selection: str = "#e3f2fd"
    text_primary: str = "#212529"
    text_secondary: str = "#6c757d"
    text_muted: str = "#888888"
    border: str = "#dee2e6"
    border_dark: str = "#ced4da"
    accent: str = "#4a90d9"
    accent_hover: str = "#357abd"
    accent_text: str = "#ffffff"
    success: str = "#27ae60"
    warning: str = "#f5a623"
    error: str = "#e74c3c"
    chart_1: str = "#4a90d9"
    chart_2: str = "#f5a623"
    chart_3: str = "#7ed321"
    chart_4: str = "#d0021b"
    chart_5: str = "#9013fe"
    chart_6: str = "#50e3c2"

    def to_dict(self) -> dict:
        return {f.name: getattr(self, f.name) for f in fields(self)}

    @classmethod
    def from_dict(cls, data: dict) -> 'Theme':
        valid_fields = {f.name for f in fields(cls)}
        filtered_data = {k: v for k, v in data.items() if k in valid_fields}
        return cls(**filtered_data)


DEFAULT_THEME = Theme()


def _dark_theme() -> Theme:
    return Theme(
        bg_primary="#0f172a",
        bg_surface="#1e293b",
        bg_elevated="#1e293b",
        bg_hover="#334155",
        bg_selection="#1e3a5f",
        text_primary="#e2e8f0",
        text_secondary="#94a3b8",
        text_muted="#64748b",
        border="#334155",
        border_dark="#475569",
        accent="#38bdf8",
        accent_hover="#0ea5e9",
        accent_text="#ffffff",
        success="#22c55e",
        warning="#eab308",
        error="#ef4444",
        chart_1="#38bdf8",
        chart_2="#fbbf24",
        chart_3="#22c55e",
        chart_4="#f87171",
        chart_5="#a78bfa",
        chart_6="#2dd4bf",
    )


THEME_FIELDS = [f.name for f in fields(Theme) if f.name != "chart_1" and not f.name.startswith("chart_")]
CHART_FIELDS = ["chart_1", "chart_2", "chart_3", "chart_4", "chart_5", "chart_6"]

LABELS_CN = {
    "bg_primary": "主背景色",
    "bg_surface": "面板/卡片背景",
    "bg_elevated": "表头/标签页选中",
    "bg_hover": "悬停高亮",
    "bg_selection": "行选中背景",
    "text_primary": "主文字颜色",
    "text_secondary": "次要文字",
    "text_muted": "弱化文字",
    "border": "边框线",
    "border_dark": "深色边框/分割线",
    "accent": "强调色(按钮/链接)",
    "accent_hover": "强调色悬停",
    "accent_text": "按钮文字色",
    "success": "成功/压缩完成",
    "warning": "警告/存储标记",
    "error": "错误/失败",
}


class ThemeManager:
    _instance: ThemeManager | None = None
    _theme: Theme = DEFAULT_THEME

    def __new__(cls) -> ThemeManager:
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    @classmethod
    def get(cls) -> Theme:
        return cls._theme

    @classmethod
    def apply(cls, theme: Theme) -> None:
        old_theme = cls._theme
        cls._theme = theme
        logger.info("[theme] theme applied")

    @classmethod
    def reset_to_default(cls) -> None:
        cls._theme = DEFAULT_THEME
        logger.info("[theme] reset to default (light)")

    @classmethod
    def set_dark(cls) -> None:
        cls._theme = _dark_theme()
        logger.info("[theme] switched to dark mode")

    @classmethod
    def is_dark(cls) -> bool:
        t = cls._theme
        r, g, b = int(t.bg_primary[1:3], 16), int(t.bg_primary[3:5], 16), int(t.bg_primary[5:7], 16)
        return (r * 299 + g * 587 + b * 114) < 128000

    @classmethod
    def color(cls, token: str) -> QColor:
        return QColor(getattr(cls._theme, token, "#000000"))

    @classmethod
    def hex(cls, token: str) -> str:
        return getattr(cls._theme, token, "#000000")

    @classmethod
    def to_dict(cls) -> dict[str, str]:
        result = {}
        for fname in THEME_FIELDS + CHART_FIELDS:
            result[fname] = getattr(cls._theme, fname)
        return result

    @classmethod
    def from_dict(cls, d: dict) -> Theme:
        kwargs = {}
        for fname in THEME_FIELDS + CHART_FIELDS:
            if fname in d and d[fname]:
                val = str(d[fname]).strip()
                if not val.startswith("#"):
                    val = "#" + val
                kwargs[fname] = val
        return Theme(**kwargs)

    @classmethod
    def dialog_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QDialog {{ background: {t.bg_primary}; }}\n"
            f"QLabel {{ color: {t.text_primary}; }}\n"
            f"QGroupBox {{ color: {t.text_secondary}; "
            f"border: 1px solid {t.border}; border-radius: 8px; margin-top: 12px; padding-top: 16px; }}"
            f"QGroupBox::title {{ subcontrol-origin: margin; left: 14px; padding: 0 4px; }}\n"
            f"QScrollArea {{ border: none; background: {t.bg_primary}; }}\n"
        )

    @classmethod
    def table_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QTableWidget {{ background: {t.bg_surface}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 8px; gridline-color: {t.border}; }}\n"
            f"QTableWidget::item:selected {{ background: {t.bg_selection}; color: {t.text_primary}; }}\n"
            f"QHeaderView::section {{ background: {t.bg_elevated}; color: {t.text_secondary}; "
            f"border: 1px solid {t.border}; padding: 6px 10px; font-weight: bold; }}\n"
        )

    @classmethod
    def tab_widget_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QTabWidget::pane {{ border: 1px solid {t.border}; border-radius: 8px; background: {t.bg_primary}; }}\n"
            f"QTabBar::tab {{ background: {t.bg_surface}; color: {t.text_secondary}; "
            f"padding: 8px 16px; border: 1px solid {t.border}; border-bottom: none; }}\n"
            f"QTabBar::tab:selected {{ background: {t.bg_elevated}; color: {t.text_primary}; }}\n"
            f"QTabBar::tab:hover:!selected {{ background: {t.bg_hover}; }}\n"
        )

    @classmethod
    def button_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QPushButton {{ background: {t.accent}; color: {t.accent_text}; padding: 6px 16px; "
            f"border-radius: 6px; font-weight: bold; border: none; }}\n"
            f"QPushButton:hover {{ background: {t.accent_hover}; }}\n"
            f"QPushButton:disabled {{ background: {t.bg_surface}; color: {t.text_muted}; }}\n"
        )

    @classmethod
    def plain_edit_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QPlainTextEdit {{ background: {t.bg_surface}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 8px; padding: 8px; }}\n"
            f"QTextEdit {{ background: {t.bg_surface}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 8px; padding: 8px; }}\n"
        )

    @classmethod
    def full_dialog_sheet(cls) -> str:
        sheets = [
            cls.dialog_sheet(),
            cls.table_sheet(),
            cls.tab_widget_sheet(),
            cls.button_sheet(),
            cls.plain_edit_sheet(),
        ]
        return "\n".join(sheets)

    @classmethod
    def html_report_css(cls) -> str:
        t = cls._theme
        return (
            f"body {{ font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; "
            f"background: {t.bg_primary}; color: {t.text_primary}; padding: 24px; }}\n"
            f".meta {{ color: {t.text_secondary}; font-size: 13px; margin-bottom: 20px; }}\n"
            f".stat {{ background: {t.bg_surface}; border-radius: 8px; padding: 14px 18px; min-width: 120px; }}\n"
            f".stat .num {{ font-size: 22px; font-weight: 700; color: {t.text_primary}; }}\n"
            f".stat .desc {{ font-size: 12px; color: {t.text_secondary}; margin-top: 4px; }}\n"
            f"th {{ background: {t.bg_elevated}; color: {t.text_secondary}; padding: 10px 12px; text-align: left; "
            f"border-bottom: 1px solid {t.border}; }}\n"
            f"td {{ padding: 8px 12px; border-bottom: 1px solid {t.border}; }}\n"
            f"tr:hover {{ background: {t.bg_hover}; }}\n"
            f".stored {{ color: {t.warning}; }}\n"
        )

    @classmethod
    def compression_view_btn_style(cls, color_hex: str) -> str:
        hover = _darken(color_hex, 15)
        return (
            f"QPushButton {{ background: {color_hex}; color: white; font-weight: bold; "
            f"padding: 10px 18px; border-radius: 8px; border: none; font-size: 13px; }}\n"
            f"QPushButton:hover {{ background: {hover}; }}\n"
            f"QPushButton:pressed {{ background: {_darken(color_hex, 25)}; }}\n"
        )

    @classmethod
    def dashboard_card_style(cls) -> str:
        t = cls._theme
        return (
            f"background: {t.bg_elevated}; border: 1px solid {t.border}; border-radius: 8px;"
        )

    @classmethod
    def main_window_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QMainWindow {{ background: {t.bg_primary}; }}\n"
            f"QWidget {{ background: {t.bg_primary}; }}\n"
            f"QMenuBar {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border-bottom: 1px solid {t.border}; padding: 2px; }}\n"
            f"QMenuBar::item:selected {{ background: {t.bg_selection}; border-radius: 4px; }}\n"
            f"QMenu {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 6px; padding: 4px; }}\n"
            f"QMenu::item:selected {{ background: {t.bg_selection}; border-radius: 4px; }}\n"
            f"QToolBar {{ background: {t.bg_surface}; border: none; "
            f"border-bottom: 1px solid {t.border}; padding: 4px; spacing: 4px; }}\n"
            f"QToolBar QToolButton {{ background: transparent; color: {t.text_primary}; "
            f"padding: 6px 12px; border-radius: 4px; border: none; }}\n"
            f"QToolBar QToolButton:hover {{ background: {t.bg_hover}; }}\n"
            f"QStatusBar {{ background: {t.bg_surface}; color: {t.text_secondary}; "
            f"border-top: 1px solid {t.border}; font-size: 12px; }}\n"
            f"QStatusBar QLabel {{ color: {t.text_secondary}; }}\n"
            f"QComboBox {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 4px; padding: 4px 8px; }}\n"
            f"QComboBox::drop-down {{ border: none; width: 20px; }}\n"
            f"QComboBox QAbstractItemView {{ background: {t.bg_elevated}; "
            f"color: {t.text_primary}; selection-background-color: {t.bg_selection}; }}\n"
            f"QToolTip {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 4px; padding: 6px; }}\n"
            f"QScrollBar:vertical {{ background: {t.bg_surface}; width: 10px; margin: 0; }}\n"
            f"QScrollBar::handle:vertical {{ background: {t.border_dark}; border-radius: 5px; min-height: 30px; }}\n"
            f"QScrollBar::handle:vertical:hover {{ background: {t.text_muted}; }}\n"
            f"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}\n"
            f"QProgressBar {{ background: {t.bg_surface}; border: 1px solid {t.border}; border-radius: 4px; text-align: center; }}\n"
            f"QProgressBar::chunk {{ background: {t.accent}; border-radius: 3px; }}\n"
            f"QSplitter::handle {{ background: {t.border}; }}\n"
        )

    @classmethod
    def html_base_css(cls) -> str:
        t = cls._theme
        return (
            f"* {{ margin: 0; padding: 0; box-sizing: border-box; }}\n"
            f"body {{ font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; "
            f"background: {t.bg_primary}; color: {t.text_primary}; padding: 24px; }}\n"
            f"h1 {{ font-size: 20px; margin-bottom: 8px; color: {t.text_primary}; }}\n"
            f".meta {{ color: {t.text_secondary}; font-size: 13px; margin-bottom: 20px; }}\n"
            f".legend {{ display: flex; align-items: center; gap: 8px; margin-bottom: 16px; font-size: 12px; color: {t.text_secondary}; }}\n"
            f".stat {{ background: {t.bg_surface}; border-radius: 8px; padding: 14px 18px; min-width: 120px; }}\n"
            f".stat .num {{ font-size: 22px; font-weight: 700; color: {t.accent}; }}\n"
            f".stat .desc {{ font-size: 12px; color: {t.text_secondary}; margin-top: 4px; }}\n"
            f".tooltip {{ display: none; position: fixed; background: {t.bg_elevated}; "
            f"border: 1px solid {t.border_dark}; border-radius: 6px; padding: 10px 14px; "
            f"font-size: 12px; z-index: 100; pointer-events: none; "
            f"box-shadow: 0 4px 12px rgba(0,0,0,0.3); }}\n"
            f".tooltip.visible {{ display: block; }}\n"
            f".tooltip .label {{ color: {t.text_secondary}; }}\n"
            f".tooltip .value {{ color: {t.text_primary}; font-weight: 600; }}\n"
            f"th {{ background: {t.bg_elevated}; color: {t.text_secondary}; padding: 10px 12px; text-align: left; "
            f"border-bottom: 1px solid {t.border}; }}\n"
            f"td {{ padding: 10px 12px; border-bottom: 1px solid {t.border}; color: {t.text_primary}; }}\n"
            f"tr:hover td {{ background: {t.bg_hover}; }}\n"
            f".highlight {{ color: {t.accent}; font-weight: 600; }}\n"
        )


def _darken(hex_color: str, amount: int = 15) -> str:
    c = QColor(hex_color)
    r = max(0, c.red() - amount)
    g = max(0, c.green() - amount)
    b = max(0, c.blue() - amount)
    return f"#{r:02x}{g:02x}{b:02x}"
