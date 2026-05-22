from __future__ import annotations

import logging
from dataclasses import dataclass, fields

from PyQt6.QtCore import QObject, pyqtSignal
from PyQt6.QtGui import QColor

from gui.config.theme_tokens import DEFAULT_COMPONENT_MAPPINGS

logger = logging.getLogger(__name__)

# ──────────────────────────────────────────────
#  Design tokens
# ──────────────────────────────────────────────

@dataclass(frozen=True)
class Theme:
    # ── colour tokens ──
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

    # ── spacing / radius tokens ──
    border_radius_sm: int = 4
    border_radius_md: int = 6
    border_radius_lg: int = 8
    padding_sm: int = 4
    padding_md: int = 8
    padding_lg: int = 16
    font_size_xs: int = 11
    font_size_sm: int = 12
    font_size_md: int = 13
    font_size_lg: int = 14
    font_size_xl: int = 18
    font_size_xxl: int = 22
    scrollbar_width: int = 10
    scrollbar_min_handle: int = 30

    def to_dict(self) -> dict:
        return {f.name: getattr(self, f.name) for f in fields(self)}

    @classmethod
    def from_dict(cls, data: dict) -> 'Theme':
        valid_fields = {f.name for f in fields(cls)}
        filtered_data: dict[str, object] = {}
        for k, v in data.items():
            if k in valid_fields:
                filtered_data[k] = v
            else:
                logger.warning("[theme] unknown key in config: %s", k)
        # Validate hex format for colour fields
        for k, v in filtered_data.items():
            if isinstance(v, str) and k not in (
                "border_radius_sm", "border_radius_md", "border_radius_lg",
                "padding_sm", "padding_md", "padding_lg",
                "font_size_xs", "font_size_sm", "font_size_md",
                "font_size_lg", "font_size_xl", "font_size_xxl",
                "scrollbar_width", "scrollbar_min_handle",
            ):
                if not _is_valid_hex(v):
                    logger.warning("[theme] invalid hex for %s: %s", k, v)
        return cls(**{k: v for k, v in filtered_data.items()})


# ──────────────────────────────────────────────
#  Presets (data, not functions)
# ──────────────────────────────────────────────

THEME_PRESETS: dict[str, dict] = {
    "light": {
        "bg_primary": "#ffffff",
        "bg_surface": "#f8f9fa",
        "bg_elevated": "#ffffff",
        "bg_hover": "#f0f1f3",
        "bg_selection": "#e3f2fd",
        "text_primary": "#212529",
        "text_secondary": "#6c757d",
        "text_muted": "#888888",
        "border": "#dee2e6",
        "border_dark": "#ced4da",
        "accent": "#4a90d9",
        "accent_hover": "#357abd",
        "accent_text": "#ffffff",
        "success": "#27ae60",
        "warning": "#f5a623",
        "error": "#e74c3c",
        "chart_1": "#4a90d9",
        "chart_2": "#f5a623",
        "chart_3": "#7ed321",
        "chart_4": "#d0021b",
        "chart_5": "#9013fe",
        "chart_6": "#50e3c2",
    },
    "dark": {
        "bg_primary": "#0f172a",
        "bg_surface": "#1e293b",
        "bg_elevated": "#253448",
        "bg_hover": "#334155",
        "bg_selection": "#1e3a5f",
        "text_primary": "#e2e8f0",
        "text_secondary": "#94a3b8",
        "text_muted": "#64748b",
        "border": "#334155",
        "border_dark": "#475569",
        "accent": "#38bdf8",
        "accent_hover": "#0ea5e9",
        "accent_text": "#ffffff",
        "success": "#22c55e",
        "warning": "#eab308",
        "error": "#ef4444",
        "chart_1": "#38bdf8",
        "chart_2": "#fbbf24",
        "chart_3": "#22c55e",
        "chart_4": "#f87171",
        "chart_5": "#a78bfa",
        "chart_6": "#2dd4bf",
    },
}

DEFAULT_THEME = Theme(**THEME_PRESETS["dark"])


def _light_theme() -> Theme:
    return Theme(**THEME_PRESETS["light"])


def _dark_theme() -> Theme:
    return Theme(**THEME_PRESETS["dark"])


# ──────────────────────────────────────────────
#  Theme field lists (preserved for compat)
# ──────────────────────────────────────────────

THEME_FIELDS = [
    "bg_primary", "bg_surface", "bg_elevated", "bg_hover", "bg_selection",
    "text_primary", "text_secondary", "text_muted",
    "border", "border_dark",
    "accent", "accent_hover", "accent_text",
    "success", "warning", "error",
]
CHART_FIELDS = ["chart_1", "chart_2", "chart_3", "chart_4", "chart_5", "chart_6"]

LABELS_CN = {
    "bg_primary": "主背景色",
    "bg_surface": "面板/卡片背景",
    "bg_elevated": "表头/标签页",
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


# ──────────────────────────────────────────────
#  ThemeManager
# ──────────────────────────────────────────────

class ThemeManager(QObject):
    """Singleton theme manager with reactive propagation.

    Connect to ``theme_changed`` for automatic widget refresh.
    Call ``resolve_hex(token)`` / ``resolve_color(token)`` as the
    single entry-point for colour lookups — it walks override →
    component-mapping → primitive-theme-field.
    """

    _instance: ThemeManager | None = None
    _theme: Theme = DEFAULT_THEME
    _component_mappings: dict[str, str] = dict(DEFAULT_COMPONENT_MAPPINGS)
    _component_overrides: dict[str, str] = {}

    theme_changed = pyqtSignal()

    def __new__(cls) -> ThemeManager:
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._qobject_inited = False
        return cls._instance

    def __init__(self) -> None:
        if self._qobject_inited:
            return
        super().__init__()
        self._qobject_inited = True

    # ── public API (classmethods — stable) ──────────────

    @classmethod
    def get(cls) -> Theme:
        return cls._theme

    @classmethod
    def apply(cls, theme: Theme) -> None:
        cls._theme = theme
        logger.info("[theme] theme applied")
        cls._emit_changed()

    @classmethod
    def reset_to_default(cls) -> None:
        cls._theme = DEFAULT_THEME
        logger.info("[theme] reset to default (dark)")
        cls._emit_changed()

    @classmethod
    def set_dark(cls) -> None:
        cls._theme = _dark_theme()
        logger.info("[theme] switched to dark mode")
        cls._emit_changed()

    @classmethod
    def set_light(cls) -> None:
        cls._theme = _light_theme()
        logger.info("[theme] switched to light mode")
        cls._emit_changed()

    @classmethod
    def is_dark(cls) -> bool:
        t = cls._theme
        r, g, b = int(t.bg_primary[1:3], 16), int(t.bg_primary[3:5], 16), int(t.bg_primary[5:7], 16)
        return (r * 299 + g * 587 + b * 114) < 128000

    @classmethod
    def color(cls, token: str) -> QColor:
        return QColor(cls.resolve_hex(token))

    @classmethod
    def hex(cls, token: str) -> str:
        # Legacy entry-point: for tokens that ARE primitive names, return directly.
        # For component tokens that share a primitive name, this also works
        # because resolve_hex falls through to the Theme field.
        return cls.resolve_hex(token)

    # ── resolution (the single colour entry-point) ──────

    @classmethod
    def resolve_hex(cls, token: str, alpha: float | None = None) -> str:
        """Resolve *token* → hex string.

        Priority: component-overrides → component-mappings → Theme field.
        """
        # 1. direct component override
        if token in cls._component_overrides:
            val = cls._component_overrides[token]
            if _is_valid_hex(val):
                return val
        # 2. component → primitive mapping
        primitive = cls._component_mappings.get(token, token)
        # 3. Theme field
        val = getattr(cls._theme, primitive, "#000000")
        if not isinstance(val, str):
            val = "#000000"
        return val

    @classmethod
    def resolve_color(cls, token: str, alpha: int | None = None) -> QColor:
        """Resolve *token* → QColor, optionally with *alpha* (0-255)."""
        c = QColor(cls.resolve_hex(token))
        if alpha is not None:
            c.setAlpha(alpha)
        return c

    @classmethod
    def set_component_mappings(cls, mappings: dict[str, str]) -> None:
        cls._component_mappings = dict(DEFAULT_COMPONENT_MAPPINGS)
        cls._component_mappings.update(mappings)

    @classmethod
    def set_component_overrides(cls, overrides: dict[str, str]) -> None:
        cls._component_overrides = dict(overrides)

    # ── to/from dict ────────────────────────────────────

    @classmethod
    def to_dict(cls) -> dict[str, str]:
        result: dict[str, str] = {}
        for fname in THEME_FIELDS + CHART_FIELDS:
            result[fname] = getattr(cls._theme, fname)
        return result

    @classmethod
    def from_dict(cls, d: dict) -> Theme:
        kwargs: dict[str, object] = {}
        for fname in THEME_FIELDS + CHART_FIELDS:
            if fname in d and d[fname]:
                val = str(d[fname]).strip()
                if not val.startswith("#"):
                    val = "#" + val
                kwargs[fname] = val
        return Theme(**kwargs)

    # ── validation ──────────────────────────────────────

    @classmethod
    def contrast_ratio(cls, hex1: str, hex2: str) -> float:
        """WCAG 2.0 relative luminance contrast ratio."""
        def _srgb(c8: int) -> float:
            s = c8 / 255.0
            return s / 12.92 if s <= 0.03928 else ((s + 0.055) / 1.055) ** 2.4

        def _lum(hex_color: str) -> float:
            h = hex_color.lstrip("#")
            r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
            return 0.2126 * _srgb(r) + 0.7152 * _srgb(g) + 0.0722 * _srgb(b)

        l1, l2 = _lum(hex1), _lum(hex2)
        if l1 < l2:
            l1, l2 = l2, l1
        return (l1 + 0.05) / (l2 + 0.05)

    # ── QSS sheets ──────────────────────────────────────

    @classmethod
    def dialog_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QDialog {{ background: {t.bg_primary}; }}\n"
            f"QLabel {{ color: {t.text_primary}; }}\n"
            f"QGroupBox {{ color: {t.text_secondary}; "
            f"border: {t.border_radius_sm}px solid {t.border}; border-radius: {t.border_radius_lg}px; "
            f"margin-top: 12px; padding-top: {t.padding_lg}px; }}"
            f"QGroupBox::title {{ subcontrol-origin: margin; left: 14px; padding: 0 {t.padding_sm}px; }}\n"
            f"QScrollArea {{ border: none; background: {t.bg_primary}; }}\n"
            f"QComboBox {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_sm}px; "
            f"padding: {t.padding_sm}px {t.padding_md}px; }}\n"
            f"QComboBox::drop-down {{ border: none; width: 20px; }}\n"
            f"QComboBox QAbstractItemView {{ background: {t.bg_elevated}; "
            f"color: {t.text_primary}; selection-background-color: {t.bg_selection}; }}\n"
            f"QSpinBox, QDoubleSpinBox {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_sm}px; "
            f"padding: {t.padding_sm}px {t.padding_md}px; }}\n"
            f"QSpinBox:focus, QDoubleSpinBox:focus {{ border-color: {t.accent}; }}\n"
            f"QCheckBox {{ color: {t.text_primary}; spacing: 6px; }}\n"
            f"QCheckBox::indicator {{ width: 16px; height: 16px; }}\n"
        )

    @classmethod
    def table_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QTableWidget {{ background: {t.bg_surface}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_lg}px; "
            f"gridline-color: {t.border}; "
            f"selection-background-color: {t.bg_selection}; selection-color: {t.text_primary}; "
            f"show-decoration-selected: 1; }}\n"
            f"QTableWidget::item:selected {{ background: {t.bg_selection}; color: {t.text_primary}; }}\n"
            f"QTreeWidget {{ background: {t.bg_surface}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_lg}px; "
            f"selection-background-color: {t.bg_selection}; selection-color: {t.text_primary}; "
            f"show-decoration-selected: 1; alternate-background-color: {t.bg_hover}; }}\n"
            f"QTreeWidget::item:selected {{ background: {t.bg_selection}; color: {t.text_primary}; }}\n"
            f"QTreeWidget::branch {{ background: {t.bg_surface}; }}\n"
            f"QHeaderView::section {{ background: {t.bg_elevated}; color: {t.text_secondary}; "
            f"border: 1px solid {t.border}; padding: {t.padding_md}px 10px; font-weight: bold; }}\n"
        )

    @classmethod
    def tab_widget_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QTabWidget::pane {{ border: 1px solid {t.border}; border-radius: {t.border_radius_lg}px; "
            f"background: {t.bg_primary}; }}\n"
            f"QTabBar::tab {{ background: {t.bg_surface}; color: {t.text_secondary}; "
            f"padding: {t.padding_md}px {t.padding_lg}px; border: 1px solid {t.border}; border-bottom: none; }}\n"
            f"QTabBar::tab:selected {{ background: {t.bg_elevated}; color: {t.text_primary}; }}\n"
            f"QTabBar::tab:hover:!selected {{ background: {t.bg_hover}; }}\n"
        )

    @classmethod
    def button_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QPushButton {{ background: {cls.resolve_hex('button_bg')}; "
            f"color: {cls.resolve_hex('button_text')}; "
            f"padding: {t.padding_md}px {t.padding_lg}px; "
            f"border-radius: {t.border_radius_md}px; font-weight: bold; border: none; }}\n"
            f"QPushButton:hover {{ background: {cls.resolve_hex('button_hover_bg')}; }}\n"
            f"QPushButton:disabled {{ background: {cls.resolve_hex('button_disabled_bg')}; "
            f"color: {cls.resolve_hex('button_disabled_text')}; }}\n"
        )

    @classmethod
    def plain_edit_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QPlainTextEdit {{ background: {t.bg_surface}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_lg}px; "
            f"padding: {t.padding_md}px; }}\n"
            f"QTextEdit {{ background: {t.bg_surface}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_lg}px; "
            f"padding: {t.padding_md}px; }}\n"
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
            f".meta {{ color: {t.text_secondary}; font-size: {t.font_size_md}px; margin-bottom: 20px; }}\n"
            f".stat {{ background: {t.bg_surface}; border-radius: {t.border_radius_lg}px; "
            f"padding: 14px 18px; min-width: 120px; }}\n"
            f".stat .num {{ font-size: {t.font_size_xxl}px; font-weight: 700; color: {t.text_primary}; }}\n"
            f".stat .desc {{ font-size: {t.font_size_sm}px; color: {t.text_secondary}; margin-top: 4px; }}\n"
            f"th {{ background: {t.bg_elevated}; color: {t.text_secondary}; padding: 10px 12px; text-align: left; "
            f"border-bottom: 1px solid {t.border}; }}\n"
            f"td {{ padding: {t.padding_md}px 12px; border-bottom: 1px solid {t.border}; }}\n"
            f"tr:hover {{ background: {t.bg_hover}; }}\n"
            f".stored {{ color: {t.warning}; }}\n"
        )

    @classmethod
    def compression_view_btn_style(cls, color_hex: str) -> str:
        hover = _darken(color_hex, 15)
        return (
            f"QPushButton {{ background: {color_hex}; color: white; font-weight: bold; "
            f"padding: 10px 18px; border-radius: {cls._theme.border_radius_lg}px; "
            f"border: none; font-size: {cls._theme.font_size_md}px; }}\n"
            f"QPushButton:hover {{ background: {hover}; }}\n"
            f"QPushButton:pressed {{ background: {_darken(color_hex, 25)}; }}\n"
        )

    @classmethod
    def dashboard_card_style(cls) -> str:
        t = cls._theme
        return (
            f"background: {t.bg_elevated}; border: 1px solid {t.border}; "
            f"border-radius: {t.border_radius_lg}px;"
        )

    @classmethod
    def main_window_sheet(cls) -> str:
        t = cls._theme
        return (
            f"QMainWindow {{ background: {t.bg_primary}; }}\n"
            f"QWidget {{ background: {t.bg_primary}; }}\n"
            f"QMenuBar {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border-bottom: 1px solid {t.border}; padding: 2px; }}\n"
            f"QMenuBar::item:selected {{ background: {t.bg_selection}; "
            f"border-radius: {t.border_radius_sm}px; }}\n"
            f"QMenu {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_md}px; "
            f"padding: {t.padding_sm}px; }}\n"
            f"QMenu::item:selected {{ background: {t.bg_selection}; "
            f"border-radius: {t.border_radius_sm}px; }}\n"
            f"QPushButton {{ background: {cls.resolve_hex('button_bg')}; "
            f"color: {cls.resolve_hex('button_text')}; "
            f"padding: {t.padding_md}px {t.padding_lg}px; "
            f"border-radius: {t.border_radius_md}px; font-weight: bold; border: none; }}\n"
            f"QPushButton:hover {{ background: {cls.resolve_hex('button_hover_bg')}; }}\n"
            f"QPushButton:pressed {{ background: {cls.resolve_hex('button_hover_bg')}; }}\n"
            f"QPushButton:disabled {{ background: {cls.resolve_hex('button_disabled_bg')}; "
            f"color: {cls.resolve_hex('button_disabled_text')}; }}\n"
            f"QToolBar {{ background: {t.bg_surface}; border: none; "
            f"border-bottom: 1px solid {t.border}; padding: {t.padding_sm}px; spacing: 4px; }}\n"
            f"QToolBar QToolButton {{ background: transparent; color: {t.text_primary}; "
            f"padding: {t.padding_md}px 12px; border-radius: {t.border_radius_sm}px; border: none; }}\n"
            f"QToolBar QToolButton:hover {{ background: {t.bg_hover}; }}\n"
            f"QStatusBar {{ background: {t.bg_surface}; color: {t.text_secondary}; "
            f"border-top: 1px solid {t.border}; font-size: {t.font_size_sm}px; }}\n"
            f"QStatusBar QLabel {{ color: {t.text_secondary}; }}\n"
            f"QComboBox {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_sm}px; "
            f"padding: {t.padding_sm}px {t.padding_md}px; }}\n"
            f"QComboBox::drop-down {{ border: none; width: 20px; }}\n"
            f"QComboBox QAbstractItemView {{ background: {t.bg_elevated}; "
            f"color: {t.text_primary}; selection-background-color: {t.bg_selection}; }}\n"
            f"QToolTip {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_sm}px; "
            f"padding: {t.padding_md}px; }}\n"
            f"QScrollBar:vertical {{ background: {cls.resolve_hex('scrollbar_bg')}; "
            f"width: {t.scrollbar_width}px; margin: 0; }}\n"
            f"QScrollBar::handle:vertical {{ background: {cls.resolve_hex('scrollbar_handle')}; "
            f"border-radius: 5px; min-height: {t.scrollbar_min_handle}px; }}\n"
            f"QScrollBar::handle:vertical:hover {{ background: {cls.resolve_hex('scrollbar_handle_hover')}; }}\n"
            f"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}\n"
            f"QProgressBar {{ background: {cls.resolve_hex('progressbar_bg')}; "
            f"border: 1px solid {t.border}; border-radius: {t.border_radius_sm}px; text-align: center; }}\n"
            f"QProgressBar::chunk {{ background: {cls.resolve_hex('progressbar_chunk')}; "
            f"border-radius: 3px; }}\n"
            f"QSplitter::handle {{ background: {t.border}; }}\n"
        )

    @classmethod
    def html_base_css(cls) -> str:
        t = cls._theme
        return (
            f"* {{ margin: 0; padding: 0; box-sizing: border-box; }}\n"
            f"body {{ font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; "
            f"background: {t.bg_primary}; color: {t.text_primary}; padding: 24px; }}\n"
            f"h1 {{ font-size: 20px; margin-bottom: {t.padding_md}px; color: {t.text_primary}; }}\n"
            f".meta {{ color: {t.text_secondary}; font-size: {t.font_size_md}px; margin-bottom: 20px; }}\n"
            f".legend {{ display: flex; align-items: center; gap: {t.padding_md}px; "
            f"margin-bottom: {t.padding_lg}px; font-size: {t.font_size_sm}px; "
            f"color: {t.text_secondary}; }}\n"
            f".stat {{ background: {t.bg_surface}; border-radius: {t.border_radius_lg}px; "
            f"padding: 14px 18px; min-width: 120px; }}\n"
            f".stat .num {{ font-size: {t.font_size_xxl}px; font-weight: 700; "
            f"color: {cls.resolve_hex('link_color')}; }}\n"
            f".stat .desc {{ font-size: {t.font_size_sm}px; color: {t.text_secondary}; margin-top: {t.padding_sm}px; }}\n"
            f".tooltip {{ display: none; position: fixed; background: {t.bg_elevated}; "
            f"border: 1px solid {t.border_dark}; border-radius: {t.border_radius_md}px; "
            f"padding: 10px 14px; "
            f"font-size: {t.font_size_sm}px; z-index: 100; pointer-events: none; "
            f"box-shadow: 0 4px 12px rgba(0,0,0,0.3); }}\n"
            f".tooltip.visible {{ display: block; }}\n"
            f".tooltip .label {{ color: {t.text_secondary}; }}\n"
            f".tooltip .value {{ color: {t.text_primary}; font-weight: 600; }}\n"
            f"th {{ background: {t.bg_elevated}; color: {t.text_secondary}; padding: 10px 12px; text-align: left; "
            f"border-bottom: 1px solid {t.border}; }}\n"
            f"td {{ padding: 10px 12px; border-bottom: 1px solid {t.border}; color: {t.text_primary}; }}\n"
            f"tr:hover td {{ background: {t.bg_hover}; }}\n"
            f".highlight {{ color: {cls.resolve_hex('link_color')}; font-weight: 600; }}\n"
        )

    # ── internal ────────────────────────────────────────

    @classmethod
    def _emit_changed(cls) -> None:
        inst = cls._instance
        if inst is not None and getattr(inst, "_qobject_inited", False):
            inst.theme_changed.emit()


# ──────────────────────────────────────────────
#  helpers
# ──────────────────────────────────────────────

def _is_valid_hex(val: str) -> bool:
    import re
    return bool(re.match(r'^#[0-9a-fA-F]{6}$', val))


def _darken(hex_color: str, amount: int = 15) -> str:
    c = QColor(hex_color)
    r = max(0, c.red() - amount)
    g = max(0, c.green() - amount)
    b = max(0, c.blue() - amount)
    return f"#{r:02x}{g:02x}{b:02x}"
