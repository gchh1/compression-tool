from __future__ import annotations

import logging
import math
import os
import time
from functools import partial
from pathlib import Path

from PyQt6.QtCore import Qt, QMimeData, QUrl, QThread, QTimer, pyqtSignal
from PyQt6.QtGui import QAction, QDragEnterEvent, QDropEvent, QBrush, QColor, QCloseEvent
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QMessageBox, QToolBar, QWidget,
    QStatusBar, QProgressBar, QLabel, QVBoxLayout, QHBoxLayout,
    QTableWidget, QTableWidgetItem, QHeaderView, QComboBox, QMenu, QDialog, QPushButton,
    QTabWidget, QFormLayout, QSpinBox, QDoubleSpinBox, QGroupBox, QScrollArea, QColorDialog,
    QCheckBox,
    QAbstractButton,
)

logger = logging.getLogger("gui.main_window")

from gui.models import (
    Record,
    FileRecord,
    FolderRecord,
    CompressionStatus,
    AlgorithmType,
    ResourceType,
    formatted_size,
    compressed_payload_size,
    ALGORITHM_PARAMS,
    get_default_config,
    merge_decision_overrides_into_algo_config,
    format_algorithm_config_param_lines,
    STREAMING_CHUNK_SIZE_KB,
    STREAMING_THRESHOLD_MB,
)
from gui.engine.file_protocol import file_record_compression_blob, strip_wcx_if_present
from gui.config.settings import load_config, get_defaults
from gui.config.theme import ThemeManager
from gui.ui.table import FileTableWidget
from gui.ui.worker import CompressionWorker, ComparisonWorker, COMPARISON_ALGORITHMS
from gui.utils.interaction_log import log_ui, log_ui_flush, preview_paths
from gui.utils.logging import flush_logging


def _compressed_size(record: Record) -> int:
    """Stored compressed artifact size (memory or ``compressed_path``)."""
    return compressed_payload_size(record)


def _read_file_text_mmap(filepath: str) -> tuple[str, list[int]]:
    """mmap *filepath* → decoded text + byte-to-char index.

    Uses ``mmap`` so the OS manages the underlying pages; the only large
    allocation is the decoded Python string.  Does **not** persist raw
    bytes on ``record.raw_data``.
    """
    import mmap
    with open(filepath, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            raw = mm.read()
    try:
        text = raw.decode("utf-8", errors="replace")
    finally:
        del raw
    byte_to_char: list[int] = []
    char_idx = 0
    for ch in text:
        for _ in range(len(ch.encode("utf-8"))):
            byte_to_char.append(char_idx)
        char_idx += 1
    return text, byte_to_char


WINDOW_WIDTH = 900
WINDOW_HEIGHT = 700


class StatusBarWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._status_label = QLabel("就绪")
        layout.addWidget(self._status_label, stretch=1)

        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setFixedWidth(200)
        self._progress.setTextVisible(True)
        layout.addWidget(self._progress)

    def set_status_text(self, text: str) -> None:
        self._status_label.setText(text)

    def set_progress_value(self, value: int) -> None:
        self._progress.setValue(value)

    def reset(self) -> None:
        self.set_status_text("就绪")
        self.set_progress_value(0)

    def refresh_theme(self):
        from gui.config.theme import ThemeManager
        t = ThemeManager.get()
        self._status_label.setStyleSheet(f"color: {t.text_secondary}; font-size: 12px;")


# ============================================================
#  算法选择器
# ============================================================

class AlgorithmSelector(QComboBox):
    ALGORITHMS = [
        ("LZDP", AlgorithmType.LZDP),
        ("DPFlate (HashChain DP+Huffman)", AlgorithmType.DPFLATE),
        ("LZSS", AlgorithmType.LZSS),
        ("Deflate", AlgorithmType.DEFLATE),
        ("Gzip (zlib标准)", AlgorithmType.GZIP),
        ("Brotli", AlgorithmType.BROTLI),
        ("Zstd", AlgorithmType.ZSTD),
        ("JPEG (图片有损)", AlgorithmType.JPEG),
        ("WebP (图片有损)", AlgorithmType.WEBP),
        ("Transformer (beta) 🧠", AlgorithmType.TRANSFORMER),
        ("Auto", AlgorithmType.AUTO),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        for label, value in self.ALGORITHMS:
            self.addItem(label, value)
        self.setToolTip("选择压缩算法")

    @property
    def current_algorithm(self) -> AlgorithmType:
        return self.currentData()


# ============================================================
#  压缩演示对话框
# ============================================================

# ============================================================
#  算法配置对话框
# ============================================================

class ThemeConfigDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("主题配置")
        self.setMinimumSize(850, 650)
        self._color_buttons: dict[str, QPushButton] = {}
        self._current_theme_dict = {}
        self._setup_ui()
        self._update_preview()

    def _setup_ui(self) -> None:
        root = QVBoxLayout(self)
        body = QWidget()
        root.addWidget(body, 1)
        layout = QHBoxLayout(body)
        
        try:
            from gui.config.theme import ThemeManager, THEME_FIELDS, LABELS_CN
            from gui.config.settings import get_theme_config
            from PyQt6.QtWidgets import QColorDialog, QFormLayout, QScrollArea, QGroupBox, QTableWidget, QTableWidgetItem, QHeaderView
            from PyQt6.QtGui import QColor, QBrush
            
            # 读取当前主题到内存
            self._current_theme_dict = get_theme_config()
            for field in THEME_FIELDS:
                if field not in self._current_theme_dict:
                    self._current_theme_dict[field] = ThemeManager.hex(field)

            # --- 左侧：配置面板 ---
            left_panel = QWidget()
            left_layout = QVBoxLayout(left_panel)
            left_layout.setContentsMargins(0,0,0,0)
            
            title_label = QLabel("自定义界面主题颜色")
            title_label.setStyleSheet(f"font-size: 14px; font-weight: bold; color: {ThemeManager.hex('text_primary')}; padding: 8px;")
            left_layout.addWidget(title_label)

            desc_label = QLabel("修改颜色将实时展示在右侧预览区。点击\"应用\"以保存。")
            desc_label.setWordWrap(True)
            desc_label.setStyleSheet(f"font-size: 11px; color: {ThemeManager.hex('text_muted')}; padding: 4px 0 12px;")
            left_layout.addWidget(desc_label)

            scroll = QScrollArea()
            scroll.setWidgetResizable(True)
            scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            form_inner = QWidget()
            form = QFormLayout(form_inner)
            form.setSpacing(10)
            form.setContentsMargins(8, 8, 8, 8)
            
            # 分组配置
            GROUPS = {
                "🔲 基础背景": ["bg_primary", "bg_surface", "bg_elevated", "bg_hover", "bg_selection"],
                "🅰️ 文字颜色": ["text_primary", "text_secondary", "text_muted"],
                "📏 边框与线条": ["border", "border_dark"],
                "✨ 强调与状态色": ["accent", "accent_hover", "accent_text", "success", "warning", "error"]
            }
            
            for group_name, fields_list in GROUPS.items():
                lbl = QLabel(group_name)
                lbl.setStyleSheet(f"font-weight: bold; color: {ThemeManager.hex('text_primary')}; padding-top: 10px; font-size: 12px;")
                form.addRow(lbl)
                for field_name in fields_list:
                    if field_name not in THEME_FIELDS: continue
                    label_cn = LABELS_CN.get(field_name, field_name)
                    row_layout = QHBoxLayout()
                    row_layout.setSpacing(8)

                    color_btn = QPushButton()
                    color_btn.setFixedSize(36, 28)
                    color_val = self._current_theme_dict[field_name]
                    if not str(color_val).startswith("#"): color_val = "#" + str(color_val)
                    
                    color_btn.setStyleSheet(f"background: {color_val}; border: 1px solid gray; border-radius: 4px;")
                    color_btn.setToolTip(f"点击选择颜色: {label_cn}")
                    color_btn.clicked.connect(lambda checked, f=field_name: self._pick_color(f))
                    self._color_buttons[field_name] = color_btn

                    hex_label = QLabel(color_val.upper())
                    hex_label.setMinimumWidth(70)
                    hex_label.setStyleSheet(f"font-family: Consolas, monospace; font-size: 11px; color: {ThemeManager.hex('text_secondary')};")
                    setattr(self, f"_theme_hex_{field_name}", hex_label)

                    name_label = QLabel(label_cn)
                    name_label.setStyleSheet(f"color: {ThemeManager.hex('text_primary')};")

                    row_layout.addWidget(color_btn)
                    row_layout.addWidget(hex_label)
                    row_layout.addStretch()

                    form.addRow(name_label, row_layout)
            
            scroll.setWidget(form_inner)
            left_layout.addWidget(scroll, 1)

            preset_group = QGroupBox("快速预设")
            preset_group.setStyleSheet(f"QGroupBox {{ font-weight: bold; color: {ThemeManager.hex('text_primary')}; border: 1px solid {ThemeManager.hex('border')}; border-radius: 6px; margin-top: 8px; padding-top: 16px; }} QGroupBox::title {{ subcontrol-origin: margin; left: 12px; padding: 0 6px; }}")
            preset_layout = QHBoxLayout(preset_group)

            dark_btn = QPushButton("🌙 暗色模式")
            dark_btn.clicked.connect(self._on_switch_dark)
            preset_layout.addWidget(dark_btn)

            light_btn = QPushButton("☀️ 亮色模式")
            light_btn.clicked.connect(self._on_switch_light)
            preset_layout.addWidget(light_btn)

            reset_btn = QPushButton("↩️ 恢复默认")
            reset_btn.clicked.connect(self._on_reset_all)
            preset_layout.addWidget(reset_btn)

            preset_layout.addStretch()
            left_layout.addWidget(preset_group)
            
            layout.addWidget(left_panel, 1)

            # --- 右侧：实时预览面板 ---
            right_panel = QWidget()
            right_layout = QVBoxLayout(right_panel)
            right_layout.setContentsMargins(15, 0, 0, 0)
            
            preview_group = QGroupBox("界面预览")
            preview_group.setStyleSheet(f"QGroupBox {{ font-weight: bold; color: {ThemeManager.hex('text_primary')}; border: 1px dashed {ThemeManager.hex('border')}; border-radius: 6px; padding-top: 16px; }} QGroupBox::title {{ subcontrol-origin: margin; left: 12px; padding: 0 6px; }}")
            preview_layout = QVBoxLayout(preview_group)
            
            self._preview_container = QWidget()
            pl = QVBoxLayout(self._preview_container)
            pl.setSpacing(15)
            
            self._lbl_preview_title = QLabel("WebCompress 预览视图")
            self._lbl_preview_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
            pl.addWidget(self._lbl_preview_title)
            
            # 按钮区预览
            btn_row = QHBoxLayout()
            self._preview_btn = QPushButton("强调色按钮")
            self._preview_btn_hover = QPushButton("Hover效果")
            btn_row.addWidget(self._preview_btn)
            btn_row.addWidget(self._preview_btn_hover)
            pl.addLayout(btn_row)
            
            # 表格区预览
            self._preview_table = QTableWidget()
            self._preview_table.setColumnCount(3)
            self._preview_table.setRowCount(3)
            self._preview_table.setHorizontalHeaderLabels(["状态", "文件名", "大小"])
            self._preview_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
            self._preview_table.setItem(0, 0, QTableWidgetItem("done"))
            self._preview_table.setItem(0, 1, QTableWidgetItem("sample.txt"))
            self._preview_table.setItem(0, 2, QTableWidgetItem("10 KB"))
            self._preview_table.setItem(1, 0, QTableWidgetItem("failed"))
            self._preview_table.setItem(1, 1, QTableWidgetItem("data.bin"))
            self._preview_table.setItem(1, 2, QTableWidgetItem("45 MB"))
            self._preview_table.setItem(2, 0, QTableWidgetItem("warning"))
            self._preview_table.setItem(2, 1, QTableWidgetItem("config.json"))
            self._preview_table.setItem(2, 2, QTableWidgetItem("2 KB"))
            self._preview_table.selectRow(0)
            self._preview_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
            self._preview_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
            self._preview_table.setAlternatingRowColors(True)
            pl.addWidget(self._preview_table, 1)
            
            preview_layout.addWidget(self._preview_container)
            right_layout.addWidget(preview_group, 1)
            
            layout.addWidget(right_panel, 1)

        except Exception as e:
            logger.error("[ThemeConfigDialog] _setup_ui failed: %s", e, exc_info=True)
            error_label = QLabel(f"初始化主题配置对话框失败:\\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            layout.addWidget(error_label)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        cancel_btn = QPushButton("取消")
        cancel_btn.clicked.connect(self.reject)
        btn_layout.addWidget(cancel_btn)
        apply_btn = QPushButton("应用")
        apply_btn.setDefault(True)
        apply_btn.clicked.connect(self._on_apply)
        btn_layout.addWidget(apply_btn)
        root.addLayout(btn_layout)

    def _pick_color(self, field_name: str) -> None:
        from gui.config.theme import LABELS_CN
        from PyQt6.QtWidgets import QColorDialog
        from PyQt6.QtGui import QColor
        current_hex = self._current_theme_dict.get(field_name, "#000000")
        color = QColorDialog.getColor(QColor(current_hex), self, f"选择颜色: {LABELS_CN.get(field_name, field_name)}")
        if color.isValid():
            self._update_color_button(field_name, color.name())

    def _update_color_button(self, field_name: str, hex_val: str) -> None:
        self._current_theme_dict[field_name] = hex_val
        btn = self._color_buttons.get(field_name)
        if btn:
            btn.setStyleSheet(f"background: {hex_val}; border: 1px solid gray; border-radius: 4px;")
        hex_label = getattr(self, f"_theme_hex_{field_name}", None)
        if hex_label:
            hex_label.setText(hex_val.upper())
        self._update_preview()

    def _on_switch_dark(self) -> None:
        from gui.config.theme import _dark_theme, THEME_FIELDS
        t = _dark_theme()
        for fname in THEME_FIELDS:
            self._update_color_button(fname, getattr(t, fname))

    def _on_switch_light(self) -> None:
        from gui.config.theme import _light_theme, THEME_FIELDS
        t = _light_theme()
        for fname in THEME_FIELDS:
            self._update_color_button(fname, getattr(t, fname))

    def _on_reset_all(self) -> None:
        from gui.config.theme import DEFAULT_THEME, THEME_FIELDS
        t = DEFAULT_THEME
        for fname in THEME_FIELDS:
            self._update_color_button(fname, getattr(t, fname))

    def _update_preview(self) -> None:
        try:
            from gui.config.theme import ThemeManager, Theme
            from PyQt6.QtGui import QColor, QBrush
            t = Theme(**self._current_theme_dict)
            self._preview_container.setStyleSheet(f"background: {t.bg_primary}; border-radius: 8px;")
            self._lbl_preview_title.setStyleSheet(f"color: {t.text_primary}; font-weight: bold; font-size: 14px;")
            self._preview_btn.setStyleSheet(f"background: {t.accent}; color: {t.accent_text}; padding: 8px; border-radius: 4px; border: none; font-weight: bold;")
            self._preview_btn_hover.setStyleSheet(f"background: {t.accent_hover}; color: {t.accent_text}; padding: 8px; border-radius: 4px; border: none; font-weight: bold;")
            
            table_style = (
                f"QTableWidget {{ background: {t.bg_surface}; color: {t.text_primary}; "
                f"border: 1px solid {t.border}; border-radius: 4px; gridline-color: {t.border}; "
                f"alternate-background-color: {t.bg_hover}; "
                f"selection-background-color: {t.bg_selection}; selection-color: {t.text_primary}; "
                f"show-decoration-selected: 1; }}\n"
                f"QTableWidget::item:selected {{ background: {t.bg_selection}; color: {t.text_primary}; }}\n"
                f"QHeaderView::section {{ background: {t.bg_elevated}; color: {t.text_secondary}; "
                f"border: 1px solid {t.border}; padding: 4px; font-weight: bold; }}\n"
            )
            self._preview_table.setStyleSheet(table_style)
            
            item_done = self._preview_table.item(0, 0)
            item_fail = self._preview_table.item(1, 0)
            item_warn = self._preview_table.item(2, 0)
            if item_done: item_done.setForeground(QBrush(QColor(t.success)))
            if item_fail: item_fail.setForeground(QBrush(QColor(t.error)))
            if item_warn: item_warn.setForeground(QBrush(QColor(t.warning)))
        except Exception:
            pass

    def _on_apply(self) -> None:
        from gui.config.settings import save_theme
        from gui.config.theme import ThemeManager, Theme
        
        theme_obj = Theme(**self._current_theme_dict)
        ThemeManager.apply(theme_obj)
        save_theme(self._current_theme_dict)
        # Notify main window to reload theme visually
        if hasattr(self.parent(), "refresh_theme"):
            self.parent().refresh_theme()
        self.accept()



class DecisionEngineManagerDialog(QDialog):
    """决策引擎管理窗口 - 查看 ADE 状态、训练数据、探索统计"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("决策引擎管理")
        self.setMinimumSize(700, 550)
        self._setup_ui()
        try:
            self._refresh()
        except Exception as e:
            logger.exception("[DecisionEngineManagerDialog] _refresh failed: %s", e)

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        tabs = QTabWidget()
        layout.addWidget(tabs)

        tab_engine = QWidget()
        tab_explorer = QWidget()
        tab_training = QWidget()
        tab_config = QWidget()

        tabs.addTab(tab_engine, "引擎状态")
        tabs.addTab(tab_explorer, "探索统计")
        tabs.addTab(tab_training, "训练数据")
        tabs.addTab(tab_config, "配置")

        self._build_engine_tab(tab_engine)
        self._build_explorer_tab(tab_explorer)
        self._build_training_tab(tab_training)
        self._build_config_tab(tab_config)

        btn_layout = QHBoxLayout()
        refresh_btn = QPushButton("刷新")
        refresh_btn.clicked.connect(self._refresh)
        btn_layout.addStretch()
        btn_layout.addWidget(refresh_btn)
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)

    def _build_engine_tab(self, tab):
        form = QFormLayout(tab)

        try:
            from gui.ade.engine import DecisionEngine
            de = DecisionEngine.get()
        except Exception:
            de = None

        if de is not None:
            self._de_status_val = QLabel("已加载" if de._ade is not None else "未初始化")
            form.addRow("ADE 引擎:", self._de_status_val)

            mode_text = de._mode.name if hasattr(de, '_mode') and de._mode else "未知"
            self._de_mode_val = QLabel(mode_text)
            form.addRow("决策模式:", self._de_mode_val)

            self._de_strategy_val = QLabel(type(de).__name__)
            form.addRow("策略类:", self._de_strategy_val)
        else:
            form.addRow("ADE 引擎:", QLabel("❌ 不可用"))
            self._de_status_val = None
            self._de_mode_val = None

        try:
            from gui.engine.compressor import CompressionEngine
            eng = CompressionEngine()
            self._engine_status_val = QLabel("可用" if eng.available else "不可用")
        except Exception:
            self._engine_status_val = QLabel("❌ 加载失败")
        form.addRow("压缩引擎:", self._engine_status_val)

        try:
            from gui.ade.features import BaseFeatures
            dims = len(BaseFeatures.__dataclass_fields__) if hasattr(BaseFeatures, '__dataclass_fields__') else 20
            self._feat_dim_val = QLabel(f"{dims} 维")
        except Exception:
            self._feat_dim_val = QLabel("未知")
        form.addRow("特征维度:", self._feat_dim_val)

    def _build_explorer_tab(self, tab):
        layout = QVBoxLayout(tab)

        try:
            from gui.ade.explorer import SilentExplorer
            explorer = SilentExplorer.get()
            stats = explorer.get_stats()
        except Exception:
            stats = {}

        info_group = QGroupBox("探索器概览")
        info_form = QFormLayout(info_group)

        enabled = stats.get('enabled', False)
        self._expl_enabled_val = QLabel("✅ 启用" if enabled else "⏸ 已禁用")
        info_form.addRow("探索状态:", self._expl_enabled_val)

        total = stats.get('total_samples', 0)
        explore = stats.get('explore_count', 0)
        discovery = stats.get('discovery_count', 0)
        self._expl_total_val = QLabel(f"{total}")
        info_form.addRow("总样本数:", self._expl_total_val)
        self._expl_explore_val = QLabel(f"{explore} ({explore/max(1,total)*100:.1f}%)")
        info_form.addRow("探索次数:", self._expl_explore_val)
        self._expl_discovery_val = QLabel(f"{discovery} ({discovery/max(1,explore)*100:.1f}%)" if explore > 0 else "0")
        info_form.addRow("发现次数:", self._expl_discovery_val)

        n_clusters = stats.get('n_clusters', 0)
        active = stats.get('active_threads', 0)
        self._expl_clusters_val = QLabel(f"{n_clusters}")
        info_form.addRow("特征簇数:", self._expl_clusters_val)
        self._expl_active_val = QLabel(f"{active}")
        info_form.addRow("活跃线程:", self._expl_active_val)

        ct = stats.get('total_compress_time_s', 0)
        et = stats.get('total_explore_time_s', 0)
        ratio = f"{et/max(ct,0.001)*100:.1f}%" if ct > 0 else "N/A"
        self._expl_budget_val = QLabel(f"压缩 {ct:.1f}s / 探索 {et:.1f}s ({ratio})")
        info_form.addRow("时间预算:", self._expl_budget_val)

        layout.addWidget(info_group)

        cluster_group = QGroupBox("簇级统计")
        cluster_layout = QVBoxLayout(cluster_group)
        self._cluster_table = QTableWidget()
        self._cluster_table.setColumnCount(4)
        self._cluster_table.setHorizontalHeaderLabels(["簇ID", "算法", "样本数", "平均压缩率"])
        self._cluster_table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        cluster_summary = stats.get('clusters_summary', {})
        row = 0
        self._cluster_table.setRowCount(len(cluster_summary))
        for cid, algos in sorted(cluster_summary.items()):
            self._cluster_table.setItem(row, 0, QTableWidgetItem(str(cid)))
            if not algos:
                self._cluster_table.setItem(row, 1, QTableWidgetItem("—"))
                self._cluster_table.setItem(row, 2, QTableWidgetItem("0"))
                self._cluster_table.setItem(row, 3, QTableWidgetItem("—"))
            else:
                algo_str = ", ".join(
                    f"{k}({v['count']})" for k, v in sorted(algos.items(), key=lambda x: -x[1]['count'])
                )
                self._cluster_table.setItem(row, 1, QTableWidgetItem(algo_str))
                total_c = sum(v['count'] for v in algos.values())
                self._cluster_table.setItem(row, 2, QTableWidgetItem(str(total_c)))
                best_algo = min(algos.items(), key=lambda x: x[1]['mean'])
                self._cluster_table.setItem(row, 3, QTableWidgetItem(f"{best_algo[1]['mean']:.4f}"))
            row += 1
        cluster_layout.addWidget(self._cluster_table)
        layout.addWidget(cluster_group)

    def _build_training_tab(self, tab):
        layout = QVBoxLayout(tab)

        try:
            from gui.ade.training import get_training_store
            store = get_training_store()
            ts = store.get_stats()
        except Exception:
            ts = {}

        info_group = QGroupBox("训练存储状态")
        info_form = QFormLayout(info_group)

        total_s = ts.get('total_samples', 0)
        valid_s = ts.get('valid_samples', 0)
        rejected_s = ts.get('rejected_samples', 0)
        self._train_total_val = QLabel(f"{total_s}")
        info_form.addRow("总样本数:", self._train_total_val)
        self._train_valid_val = QLabel(f"{valid_s} ({valid_s/max(1,total_s)*100:.1f}%)")
        info_form.addRow("有效样本:", self._train_valid_val)
        self._train_rejected_val = QLabel(f"{rejected_s}")
        info_form.addRow("拒绝样本:", self._train_rejected_val)

        store_path = ts.get('store_path', '未知')
        store_size = ts.get('store_size_bytes', 0)
        size_str = f"{store_size / 1024:.1f} KB" if store_size < 1024*1024 else f"{store_size/1024/1024:.1f} MB"
        self._train_path_val = QLabel(store_path)
        info_form.addRow("存储路径:", self._train_path_val)
        self._train_size_val = QLabel(size_str)
        info_form.addRow("文件大小:", self._train_size_val)

        layout.addWidget(info_group)

        placeholder = QLabel("(训练数据详细查看功能开发中...)")
        placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        placeholder.setStyleSheet("color: #888; font-size: 13px;")
        layout.addWidget(placeholder)

    def _build_config_tab(self, tab):
        layout = QVBoxLayout(tab)

        config_group = QGroupBox("当前配置")
        config_form = QFormLayout(config_group)

        try:
            from gui.ade.explorer import SilentExplorer
            cfg = SilentExplorer.DEFAULT_CONFIG
        except Exception:
            cfg = {}

        self._cfg_enabled_cb = QCheckBox()
        self._cfg_enabled_cb.setChecked(cfg.get('enabled', True))
        config_form.addRow("启用探索:", self._cfg_enabled_cb)

        self._cfg_epsilon_val = QLabel(f"{cfg.get('epsilon_base', 0.25)}")
        config_form.addRow("基础探索率 (ε):", self._cfg_epsilon_val)

        self._cfg_alpha_val = QLabel(f"{cfg.get('alpha_ucb', 1.41)}")
        config_form.addRow("UCB 系数 (α):", self._cfg_alpha_val)

        self._cfg_max_concurrent_val = QLabel(f"{cfg.get('max_concurrent', 2)}")
        config_form.addRow("最大并发线程:", self._cfg_max_concurrent_val)

        self._cfg_timeout_val = QLabel(f"{cfg.get('timeout_seconds', 30)}s")
        config_form.addRow("单次超时:", self._cfg_timeout_val)

        self._cfg_budget_val = QLabel(f"{cfg.get('budget_ratio', 0.30)*100:.0f}%")
        config_form.addRow("时间预算上限:", self._cfg_budget_val)

        warmup = cfg.get('warmup_samples', 50)
        stages = [
            ("冷启动 (0-50)", 0.40),
            ("学习期 (51-500)", 0.25),
            ("成熟期 (501-2000)", 0.15),
            ("稳定期 (2000+)", 0.08),
        ]
        stage_text = "\n".join(f"  {s[0]}: ε={s[1]}" for s in stages)
        self._cfg_stages_val = QLabel(stage_text)
        config_form.addRow("自适应衰减阶段:", self._cfg_stages_val)

        layout.addWidget(config_group)

        action_group = QGroupBox("操作")
        action_layout = QVBoxLayout(action_group)

        reset_btn = QPushButton("重置探索器")
        reset_btn.setToolTip("清除所有探索统计数据，重新开始学习")
        reset_btn.clicked.connect(self._on_reset_explorer)
        action_layout.addWidget(reset_btn)

        export_btn = QPushButton("导出训练数据")
        export_btn.setToolTip("将训练样本导出为 JSON 格式")
        export_btn.setEnabled(False)
        action_layout.addWidget(export_btn)

        train_btn = QPushButton("训练模型")
        train_btn.setToolTip("使用收集的训练数据重新训练 ADE 模型")
        train_btn.setEnabled(False)
        action_layout.addWidget(train_btn)

        placeholder = QLabel("更多功能（模型训练、数据导出、参数调优）将在后续版本实现")
        placeholder.setStyleSheet("color: #888; font-size: 12px; padding: 8px;")
        action_layout.addWidget(placeholder)
        layout.addWidget(action_group)

    def _on_reset_explorer(self):
        reply = QMessageBox.question(
            self, "确认重置",
            "确定要重置探索器的所有统计数据吗？\n这将清除特征簇信息和历史采样记录。",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply == QMessageBox.StandardButton.Yes:
            try:
                from gui.ade.explorer import SilentExplorer
                SilentExplorer.reset()
                logger.info("[de_manager] explorer reset by user")
                self._refresh()
                QMessageBox.information(self, "已完成", "探索器已重置")
            except Exception as e:
                QMessageBox.warning(self, "错误", f"重置失败: {e}")

    def _refresh(self):
        try:
            from gui.ade.engine import DecisionEngine
            de = DecisionEngine.get()
            if hasattr(self, '_de_status_val') and self._de_status_val:
                self._de_status_val.setText("已加载" if de._ade is not None else "未初始化")
            if hasattr(self, '_de_mode_val') and self._de_mode_val:
                mode_text = de._mode.name if hasattr(de, '_mode') and de._mode else "未知"
                self._de_mode_val.setText(mode_text)
            if hasattr(self, '_de_strategy_val') and self._de_strategy_val:
                self._de_strategy_val.setText(type(de).__name__)
        except Exception:
            pass

        try:
            from gui.ade.explorer import SilentExplorer
            stats = SilentExplorer.get().get_stats()
            if hasattr(self, '_expl_enabled_val'):
                self._expl_enabled_val.setText("✅ 启用" if stats.get('enabled') else "⏸ 已禁用")
            if hasattr(self, '_expl_total_val'):
                self._expl_total_val.setText(f"{stats.get('total_samples', 0)}")
            if hasattr(self, '_expl_explore_val'):
                t = stats.get('total_samples', 0); e = stats.get('explore_count', 0)
                self._expl_explore_val.setText(f"{e} ({e/max(1,t)*100:.1f}%)")
            if hasattr(self, '_expl_discovery_val'):
                e = stats.get('explore_count', 0); d = stats.get('discovery_count', 0)
                self._expl_discovery_val.setText(f"{d} ({d/max(1,e)*100:.1f}%)" if e > 0 else "0")
            if hasattr(self, '_expl_clusters_val'):
                self._expl_clusters_val.setText(f"{stats.get('n_clusters', 0)}")
            if hasattr(self, '_expl_active_val'):
                self._expl_active_val.setText(f"{stats.get('active_threads', 0)}")
        except Exception:
            pass

        try:
            from gui.ade.training import get_training_store
            ts = get_training_store().get_stats()
            if hasattr(self, '_train_total_val'):
                self._train_total_val.setText(f"{ts.get('total_samples', 0)}")
            if hasattr(self, '_train_valid_val'):
                t = ts.get('total_samples', 0); v = ts.get('valid_samples', 0)
                self._train_valid_val.setText(f"{v} ({v/max(1,t)*100:.1f}%)")
            if hasattr(self, '_train_rejected_val'):
                self._train_rejected_val.setText(f"{ts.get('rejected_samples', 0)}")
        except Exception:
            pass


def _streaming_follow_toggle_button(label: str, *, checked: bool) -> QPushButton:
    """☐/☑ 文案 + 扁平按钮，与主窗口工具栏「全选」同一套符号，避免原生 QCheckBox 在主题下难点、难看。"""
    from gui.config.theme import ThemeManager

    btn = QPushButton()
    btn.setCheckable(True)
    btn.setChecked(checked)
    btn.setFlat(True)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
    tp = ThemeManager.hex("text_primary")
    bh = ThemeManager.hex("bg_hover")
    bs = ThemeManager.hex("bg_selection")
    btn.setStyleSheet(
        f"QPushButton {{ background: transparent; color: {tp}; "
        f"text-align: left; padding: 4px 2px; border: none; border-radius: 4px; font-size: 13px; }}"
        f"QPushButton:hover {{ background: {bh}; }}"
        f"QPushButton:pressed {{ background: {bs}; }}"
        f"QPushButton:checked {{ background: {bs}; color: {tp}; }}"
    )

    def sync_text() -> None:
        sym = "☑" if btn.isChecked() else "☐"
        btn.setText(f"{sym} {label}")

    btn.toggled.connect(sync_text)
    sync_text()
    return btn


class MinMatchWidget(QWidget):
    def __init__(self, p, current_val, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.spin = QSpinBox()
        self.spin.setMinimum(max(1, p.min_val))
        self.spin.setMaximum(p.max_val)
        self.spin.setSingleStep(p.step)

        self.auto_btn = _streaming_follow_toggle_button("Auto计算", checked=False)

        layout.addWidget(self.spin)
        layout.addWidget(self.auto_btn)

        self.auto_btn.toggled.connect(self._on_auto_toggled)

        self.default_val = p.default if p.default > 0 else 3
        self.setValue(current_val)

    def _on_auto_toggled(self, checked):
        self.spin.setEnabled(not checked)

    def value(self):
        if self.auto_btn.isChecked():
            return 0
        return self.spin.value()

    def setValue(self, val):
        if val == 0:
            self.auto_btn.setChecked(True)
            self.spin.setValue(self.default_val)
            self.spin.setEnabled(False)
        else:
            self.auto_btn.setChecked(False)
            self.spin.setValue(val)
            self.spin.setEnabled(True)


class AlgorithmConfigDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("算法配置")
        self.setMinimumSize(640, 520)
        self._spinboxes: dict[AlgorithmType, dict[str, QWidget]] = {}
        self._encoding_preview_labels: dict[AlgorithmType, QLabel] = {}
        self._streaming_threshold_spin: QDoubleSpinBox | None = None
        self._streaming_chunk_spin: QSpinBox | None = None
        self._per_algo_stream_widgets: dict[AlgorithmType, dict[str, object]] = {}
        self._setup_ui()

    def _setup_ui(self) -> None:
        root = QVBoxLayout(self)
        body = QWidget()
        root.addWidget(body, 1)
        layout = QVBoxLayout(body)
        from gui.config.theme import ThemeManager
        from gui.config.settings import get_silent_explore_enabled, load_config as _cfg_for_ade

        self._silent_explore_btn = _streaming_follow_toggle_button(
            "启用静默探索（后台对比压缩）",
            checked=get_silent_explore_enabled(_cfg_for_ade()),
        )
        self._silent_explore_btn.setToolTip(
            "关闭：不启动后台静默压缩采样。\n"
            "开启：在部分压缩完成后可能异步试运行其他算法；正在压缩/解压时不会新起静默任务，避免与用户操作争用引擎配置。"
        )
        ade_row = QHBoxLayout()
        ade_row.addWidget(self._silent_explore_btn, 0)
        ade_hint = QLabel(
            "说明：用户压缩/解压相当于更高优先级「中断」——进行中的主路径会阻止新起静默探索；"
            "引擎全局配置在内部加锁串行，保护静默探索的 set_config / 恢复现场。"
        )
        ade_hint.setWordWrap(True)
        ade_hint.setStyleSheet(
            f"color: {ThemeManager.hex('text_muted')}; font-size: 11px; padding-left: 8px;"
        )
        ade_row.addWidget(ade_hint, 1)
        layout.addLayout(ade_row)

        try:
            from gui.engine.compressor import CompressionEngine
            from gui.config.theme import ThemeManager

            current_config = CompressionEngine.get_config()

            from gui.config.settings import (
                load_config as _cfg_load_stream,
                get_streaming_chunk_size,
                get_streaming_per_algorithm,
                get_streaming_threshold,
            )
            _stream_file_cfg = _cfg_load_stream()
            _global_chunk_kb = get_streaming_chunk_size(_stream_file_cfg)
            _global_threshold_mb = float(get_streaming_threshold(_stream_file_cfg))
            _per_algo_saved = get_streaming_per_algorithm(_stream_file_cfg)
            self._per_algo_stream_widgets = {}

            tabs = QTabWidget()
            algo_labels = {
                AlgorithmType.DEFLATE: "Deflate",
                AlgorithmType.LZDP: "LZDP",
                AlgorithmType.LZSS: "LZSS",
                AlgorithmType.DPFLATE: "DPFlate",
                AlgorithmType.GZIP: "Gzip",
                AlgorithmType.BROTLI: "Brotli",
                AlgorithmType.ZSTD: "Zstd",
                AlgorithmType.JPEG: "JPEG (图片)",
                AlgorithmType.WEBP: "WebP (图片)",
            }

            for algo in [AlgorithmType.DEFLATE, AlgorithmType.LZDP, AlgorithmType.LZSS, AlgorithmType.DPFLATE, AlgorithmType.GZIP, AlgorithmType.BROTLI, AlgorithmType.ZSTD, AlgorithmType.JPEG, AlgorithmType.WEBP]:
                params = ALGORITHM_PARAMS.get(algo, [])
                if not params:
                    continue

                tab = QWidget()
                tab_outer = QVBoxLayout(tab)
                tab_outer.setContentsMargins(0, 0, 0, 0)
                form = QFormLayout()
                form.setContentsMargins(12, 12, 12, 12)
                tab_outer.addLayout(form)
                self._spinboxes[algo] = {}

                cfg = current_config.get(algo, {})

                for p in params:
                    if p.key == "min_match":
                        current_val = cfg.get(p.key, p.default)
                        w = MinMatchWidget(p, current_val, self)
                        form.addRow(f"{p.label}:", w)
                        self._spinboxes[algo][p.key] = w
                    elif getattr(p, "choices", None) is not None:
                        combo = QComboBox()
                        for val, text in p.choices.items():
                            combo.addItem(text, val)
                        
                        current_val = cfg.get(p.key, p.default)
                        index = combo.findData(current_val)
                        if index >= 0:
                            combo.setCurrentIndex(index)
                        
                        form.addRow(f"{p.label}:", combo)
                        self._spinboxes[algo][p.key] = combo
                    else:
                        spin = QSpinBox()
                        spin.setMinimum(p.min_val)
                        spin.setMaximum(p.max_val)
                        spin.setSingleStep(p.step)
                        spin.setValue(cfg.get(p.key, p.default))
                        spin.setSuffix(p.suffix)
                        spin.setToolTip(f"范围: {p.min_val} ~ {p.max_val}")
                        form.addRow(f"{p.label}:", spin)
                        self._spinboxes[algo][p.key] = spin

                pa0 = _per_algo_saved.get(algo.value, {})
                follow_global = bool(pa0.get("follow_global_chunk", True))
                stream_gb = QGroupBox("流式（本算法）")
                sform = QFormLayout(stream_gb)
                follow_cb = _streaming_follow_toggle_button(
                    "分块大小跟随全局「流式设置」", checked=follow_global
                )
                chunk_sb = QSpinBox()
                chunk_sb.setMinimum(64)
                chunk_sb.setMaximum(128 * 1024)
                chunk_sb.setSingleStep(64)
                chunk_sb.setSuffix(" KB")
                chunk_sb.setValue(int(pa0.get("chunk_size_kb", _global_chunk_kb)))

                # 与 MinMatchWidget 一致：用 toggled(bool) + 信号里的 checked，不用 isChecked() 竞态。
                # 必须用 lambda 默认参数绑定 sb=chunk_sb，避免 for 循环闭包延迟绑定到「最后一个算法」的 SpinBox。
                follow_cb.toggled.connect(
                    lambda checked, sb=chunk_sb: sb.setEnabled(not checked)
                )
                chunk_sb.setEnabled(not follow_global)
                sform.addRow("", follow_cb)
                sform.addRow("本算法流式分块:", chunk_sb)
                follow_thresh = bool(pa0.get("follow_global_threshold", True))
                follow_thresh_cb = _streaming_follow_toggle_button(
                    "流式阈值跟随全局「流式设置」", checked=follow_thresh
                )
                thresh_sb = QDoubleSpinBox()
                thresh_sb.setMinimum(0.0)
                thresh_sb.setMaximum(20480.0)
                thresh_sb.setDecimals(4)
                thresh_sb.setSingleStep(0.001)
                thresh_sb.setSuffix(" MB")
                thresh_sb.setValue(float(pa0.get("threshold_mb", _global_threshold_mb)))

                follow_thresh_cb.toggled.connect(
                    lambda checked, sb=thresh_sb: sb.setEnabled(not checked)
                )
                thresh_sb.setEnabled(not follow_thresh)
                sform.addRow("", follow_thresh_cb)
                sform.addRow("本算法流式阈值:", thresh_sb)
                tab_outer.addWidget(stream_gb)
                self._per_algo_stream_widgets[algo] = {
                    "follow": follow_cb,
                    "chunk": chunk_sb,
                    "follow_threshold": follow_thresh_cb,
                    "threshold": thresh_sb,
                }

                if algo in (
                    AlgorithmType.DEFLATE,
                    AlgorithmType.LZSS,
                    AlgorithmType.LZDP,
                    AlgorithmType.DPFLATE,
                ):
                    prev = QLabel("")
                    prev.setWordWrap(True)
                    prev.setStyleSheet(
                        f"font-family: Consolas, monospace; font-size: 11px; color: {ThemeManager.hex('text_secondary')};"
                    )
                    prev.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
                    gb = QGroupBox("编码与位宽（实时）")
                    gb.setStyleSheet(
                        f"QGroupBox {{ font-weight: bold; color: {ThemeManager.hex('text_primary')}; "
                        f"border: 1px solid {ThemeManager.hex('border')}; border-radius: 6px; margin-top: 8px; padding-top: 12px; }}"
                    )
                    gbl = QVBoxLayout(gb)
                    gbl.addWidget(prev)
                    tab_outer.addWidget(gb)
                    self._encoding_preview_labels[algo] = prev

                tabs.addTab(tab, algo_labels.get(algo, algo.value))

            stream_tab = QWidget()
            stream_form = QFormLayout(stream_tab)
            stream_form.setContentsMargins(12, 12, 12, 12)

            from gui.engine.compressor import CompressionEngine as _CE

            cur_threshold = _CE.get_streaming_threshold()
            _chunk_kb = _global_chunk_kb

            self._streaming_threshold_spin = QDoubleSpinBox()
            self._streaming_threshold_spin.setMinimum(0.0)
            self._streaming_threshold_spin.setMaximum(20480.0)
            self._streaming_threshold_spin.setDecimals(4)
            self._streaming_threshold_spin.setSingleStep(0.001)
            self._streaming_threshold_spin.setValue(float(cur_threshold))
            self._streaming_threshold_spin.setSuffix(" MB")
            self._streaming_threshold_spin.setToolTip(
                "当文件大小严格大于该值（MB×1024² 字节）时走流式路径。\n"
                "0 = 任意非空文件即流式，便于测试；可填小数（如 0.01）测小文件。\n"
                "流式模式内存占用更平稳，适合超大文件。"
            )
            stream_form.addRow("使用流式的大小阈值:", self._streaming_threshold_spin)

            self._streaming_chunk_spin = QSpinBox()
            self._streaming_chunk_spin.setMinimum(64)
            self._streaming_chunk_spin.setMaximum(128 * 1024)
            self._streaming_chunk_spin.setSingleStep(64)
            self._streaming_chunk_spin.setValue(_chunk_kb)
            self._streaming_chunk_spin.setSuffix(" KB")
            self._streaming_chunk_spin.setToolTip(
                "流式模式下每个数据块的大小\n"
                "较大的块提高压缩率，较小的块降低内存占用"
            )
            stream_form.addRow("流式分块大小:", self._streaming_chunk_spin)

            desc_label = QLabel(
                "流式模式说明：文件超过阈值时自动启用；C++ pipeline 使用 terminator 分帧"
                "（[4B长度][数据]…[4B 0]）。\n"
                "全局「流式分块大小」与「流式阈值」为各算法默认值；各算法页可分别取消「跟随全局」单独设置。\n"
                "LZDP 文件管线与内存压缩共用同一套 ``api::pipeline_compress`` 语义（整文件明文缓冲后单次压缩；见 docs/design/lzdp-file-pipeline-design.md）。"
                "DPFlate 文件流式固定为外存 DP + 整文件 Huffman 路径（§16.6）。"
            )
            desc_label.setWordWrap(True)
            desc_label.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; font-size: 11px; padding: 4px 0;")
            stream_form.addRow(desc_label)

            tabs.addTab(stream_tab, "流式设置")

            layout.addWidget(tabs)

            for prev_algo in self._encoding_preview_labels:
                self._refresh_encoding_preview(prev_algo)
                self._wire_encoding_preview(prev_algo)

        except Exception as e:
            logger.error("[AlgorithmConfigDialog] _setup_ui failed: %s", e, exc_info=True)
            while layout.count():
                item = layout.takeAt(0)
                w = item.widget()
                if w is not None:
                    w.deleteLater()
            error_label = QLabel(f"初始化算法配置对话框失败:\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            layout.addWidget(error_label)

        btn_layout = QHBoxLayout()
        reset_btn = QPushButton("恢复默认")
        reset_btn.clicked.connect(self._on_reset)
        btn_layout.addWidget(reset_btn)
        btn_layout.addStretch()

        cancel_btn = QPushButton("取消")
        cancel_btn.clicked.connect(self.reject)
        btn_layout.addWidget(cancel_btn)

        apply_btn = QPushButton("应用")
        apply_btn.setDefault(True)
        apply_btn.clicked.connect(self._on_apply)
        btn_layout.addWidget(apply_btn)

        root.addLayout(btn_layout)

    def _read_algo_widget_int(self, algo: AlgorithmType, key: str, default: int = 0) -> int:
        w = self._spinboxes.get(algo, {}).get(key)
        if w is None:
            return default
        if isinstance(w, QSpinBox):
            return int(w.value())
        if isinstance(w, MinMatchWidget):
            return int(w.value())
        return default

    def _read_algo_flag_encoding(self, algo: AlgorithmType, default: bool = False) -> bool:
        w = self._spinboxes.get(algo, {}).get("use_flag_encoding")
        if isinstance(w, QComboBox):
            v = w.currentData()
            if v is None:
                return default
            return bool(int(v))
        return default

    def _read_algo_combo_int(self, algo: AlgorithmType, key: str, default: int = 0) -> int:
        w = self._spinboxes.get(algo, {}).get(key)
        if isinstance(w, QComboBox):
            v = w.currentData()
            if v is None:
                return default
            return int(v)
        return default

    def _refresh_encoding_preview(self, algo: AlgorithmType) -> None:
        lbl = self._encoding_preview_labels.get(algo)
        if lbl is None:
            return
        from gui.ui.helpers import (
            format_deflate_preview,
            format_lzdp_preview,
            format_lzss_preview,
            format_dpflate_lz_reference_preview,
        )
        try:
            if algo == AlgorithmType.DEFLATE:
                lbl.setText(format_deflate_preview(
                    self._read_algo_widget_int(algo, "search_size", 4096),
                    self._read_algo_widget_int(algo, "lookahead_size", 256),
                    self._read_algo_widget_int(algo, "min_match", 0),
                    self._read_algo_flag_encoding(algo, True),
                    bool(self._read_algo_combo_int(algo, "use_3hfmtree", 0)),
                    self._read_algo_widget_int(algo, "huffman_offset_chunk_bits", 8),
                    self._read_algo_widget_int(algo, "huffman_length_chunk_bits", 8),
                ))
            elif algo == AlgorithmType.LZDP:
                lbl.setText(format_lzdp_preview(
                    self._read_algo_widget_int(algo, "search_size", 4096),
                    self._read_algo_widget_int(algo, "lookahead_size", 256),
                    self._read_algo_widget_int(algo, "min_match", 0),
                    self._read_algo_flag_encoding(algo, False),
                ))
            elif algo == AlgorithmType.DPFLATE:
                lbl.setText(format_dpflate_lz_reference_preview(
                    self._read_algo_widget_int(algo, "search_size", 4096),
                    self._read_algo_widget_int(algo, "lookahead_size", 256),
                    self._read_algo_widget_int(algo, "min_match", 0),
                    self._read_algo_flag_encoding(algo, False),
                ))
            elif algo == AlgorithmType.LZSS:
                lbl.setText(format_lzss_preview(
                    self._read_algo_widget_int(algo, "search_size", 4095),
                    self._read_algo_widget_int(algo, "lookahead_size", 18),
                    self._read_algo_widget_int(algo, "min_match", 0),
                    self._read_algo_flag_encoding(algo, False),
                ))
        except Exception as e:
            lbl.setText(f"预览更新失败: {e}")

    def _wire_encoding_preview(self, algo: AlgorithmType) -> None:
        if algo not in self._encoding_preview_labels:
            return

        def refresh(*_args: object) -> None:
            self._refresh_encoding_preview(algo)

        for w in self._spinboxes.get(algo, {}).values():
            if isinstance(w, QSpinBox):
                w.valueChanged.connect(refresh)
            elif isinstance(w, QComboBox):
                w.currentIndexChanged.connect(refresh)
            elif isinstance(w, MinMatchWidget):
                w.spin.valueChanged.connect(refresh)
                w.auto_btn.toggled.connect(refresh)

    def _on_reset(self) -> None:
        from gui.models import STREAMING_THRESHOLD_MB, STREAMING_CHUNK_SIZE_KB
        from gui.engine.compressor import CompressionEngine
        from gui.config.settings import get_defaults

        CompressionEngine.reset_to_defaults()
        defaults = CompressionEngine.get_config()
        for algo, widgets in self._spinboxes.items():
            cfg = defaults.get(algo, {})
            for key, widget in widgets.items():
                if isinstance(widget, QComboBox):
                    default_val = cfg.get(key, 0)
                    index = widget.findData(default_val)
                    if index >= 0:
                        widget.setCurrentIndex(index)
                elif isinstance(widget, MinMatchWidget):
                    widget.setValue(cfg.get(key, 0))
                else:
                    fallback = widget.minimum() if hasattr(widget, 'minimum') else 0
                    widget.setValue(cfg.get(key, fallback))
        if self._streaming_threshold_spin:
            self._streaming_threshold_spin.setValue(
                float(CompressionEngine.get_streaming_threshold())
            )
        if self._streaming_chunk_spin:
            self._streaming_chunk_spin.setValue(STREAMING_CHUNK_SIZE_KB)
        dpa = get_defaults()["streaming"]["per_algorithm"]
        if hasattr(self, "_silent_explore_btn"):
            self._silent_explore_btn.setChecked(
                bool(get_defaults().get("ade", {}).get("silent_explore_enabled", False))
            )
        for algo, pack in self._per_algo_stream_widgets.items():
            follow_cb = pack["follow"]
            chunk_sb = pack["chunk"]
            follow_th = pack.get("follow_threshold")
            thresh_sb = pack.get("threshold")
            if not isinstance(follow_cb, QAbstractButton) or not isinstance(chunk_sb, QSpinBox):
                continue
            sub = dpa.get(algo.value, {})
            follow_cb.setChecked(bool(sub.get("follow_global_chunk", True)))
            chunk_sb.setValue(int(sub.get("chunk_size_kb", STREAMING_CHUNK_SIZE_KB)))
            chunk_sb.setEnabled(not follow_cb.isChecked())
            if isinstance(follow_th, QAbstractButton) and isinstance(thresh_sb, QDoubleSpinBox):
                follow_th.setChecked(bool(sub.get("follow_global_threshold", True)))
                thresh_sb.setValue(float(sub.get("threshold_mb", STREAMING_THRESHOLD_MB)))
                thresh_sb.setEnabled(not follow_th.isChecked())
        for prev_algo in self._encoding_preview_labels:
            self._refresh_encoding_preview(prev_algo)

    def _on_apply(self) -> None:
        from gui.engine.compressor import CompressionEngine

        config: dict[AlgorithmType, dict[str, int]] = {}
        for algo, widgets in self._spinboxes.items():
            config[algo] = {}
            for key, widget in widgets.items():
                if isinstance(widget, QComboBox):
                    config[algo][key] = widget.currentData()
                else:
                    config[algo][key] = widget.value()

        CompressionEngine.set_config(config)

        if self._streaming_threshold_spin:
            CompressionEngine.set_streaming_threshold(
                float(self._streaming_threshold_spin.value())
            )

        from gui.config.settings import load_config, save_config

        full = load_config()
        full.setdefault("ade", {})
        full["ade"]["silent_explore_enabled"] = bool(self._silent_explore_btn.isChecked())
        if "streaming" not in full:
            full["streaming"] = {}
        if self._streaming_chunk_spin:
            full["streaming"]["chunk_size_kb"] = int(self._streaming_chunk_spin.value())
        if self._per_algo_stream_widgets:
            pa_out: dict[str, dict] = {}
            for algo, pack in self._per_algo_stream_widgets.items():
                follow_cb = pack["follow"]
                chunk_sb = pack["chunk"]
                follow_th = pack.get("follow_threshold")
                thresh_sb = pack.get("threshold")
                if not isinstance(follow_cb, QAbstractButton) or not isinstance(chunk_sb, QSpinBox):
                    continue
                ent: dict = {
                    "follow_global_chunk": bool(follow_cb.isChecked()),
                    "chunk_size_kb": int(chunk_sb.value()),
                }
                if isinstance(follow_th, QAbstractButton) and isinstance(thresh_sb, QDoubleSpinBox):
                    ent["follow_global_threshold"] = bool(follow_th.isChecked())
                    ent["threshold_mb"] = float(thresh_sb.value())
                pa_out[algo.value] = ent
            full["streaming"]["per_algorithm"] = pa_out
        save_config(full)
        try:
            from gui.ade.explorer import SilentExplorer

            SilentExplorer.get().set_enabled(bool(full["ade"]["silent_explore_enabled"]))
        except Exception:
            pass

        self.accept()


# ============================================================
#  主窗口
# ============================================================

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("WebCompress")
        self.resize(WINDOW_WIDTH, WINDOW_HEIGHT)

        # Force native file-based decompress (C++ decompressFile streaming path).
        # Avoids in-memory pipeline_decompress segfaults.
        os.environ["WEBCOMPRESS_NATIVE_DECOMPRESS_FILE"] = "1"

        from PyQt6.QtGui import QIcon
        from gui.utils.resources import resolve_icon_path
        _icon = resolve_icon_path()
        if _icon:
            self.setWindowIcon(QIcon(str(_icon)))

        self.setAcceptDrops(True)
        self._worker: CompressionWorker | None = None
        self._comparison_worker: ComparisonWorker | None = None
        self._comparison_progress = None

        self._table = FileTableWidget()
        self._statusbar = StatusBarWidget()
        self._algo_selector = AlgorithmSelector()

        self._setup_menu()
        self._setup_toolbar()
        self._setup_central()

        bar = QStatusBar()
        self.setStatusBar(bar)
        bar.addPermanentWidget(self._statusbar)

        self._table.request_heatmap.connect(self._on_view_heatmap_row)
        self._table.request_comparison.connect(self._on_view_comparison_row)
        self._table.request_network.connect(self._on_view_network_row)
        self._table.request_webpage_heatmap.connect(self._on_webpage_heatmap)
        self._table.request_folder_summary.connect(self._on_folder_summary)
        self._table.request_decision_detail.connect(self._on_view_decision_detail)
        self._table.request_view_viz.connect(self._on_view_viz_file)
        self._table.request_folder_heatmap.connect(self._on_view_folder_heatmap)
        self._table.request_browse_decompress.connect(self._on_browse_decompress_single)

        try:
            from gui.ade.explorer import SilentExplorer

            SilentExplorer.apply_enabled_from_settings()
        except Exception:
            pass

        self.setStyleSheet(ThemeManager.main_window_sheet())
        log_ui_flush(
            "main_window.ready",
            geometry=f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}",
        )

    def closeEvent(self, event: QCloseEvent) -> None:
        log_ui_flush("main_window.closeEvent")
        super().closeEvent(event)

    def refresh_theme(self):
        self.setStyleSheet(ThemeManager.main_window_sheet())
        self._table.refresh_theme()
        if hasattr(self, '_statusbar'):
            self._statusbar.refresh_theme()

    # ========== 菜单设置 ==========
    def _setup_menu(self) -> None:
        bar = self.menuBar()

        file_menu = bar.addMenu("文件 (&F)")

        add_menu = file_menu.addMenu("添加 (&A)")
        add_files_action = QAction("添加文件...", self)
        add_files_action.triggered.connect(self._on_add_files)
        add_menu.addAction(add_files_action)
        add_folder_action = QAction("添加文件夹...", self)
        add_folder_action.triggered.connect(self._on_add_folder)
        add_menu.addAction(add_folder_action)

        open_archive_action = QAction("打开压缩包 (&O)...", self)
        open_archive_action.setShortcut("Ctrl+O")
        open_archive_action.triggered.connect(self._open_archive)
        file_menu.addAction(open_archive_action)

        file_menu.addSeparator()

        remove_action = QAction("删除选中 (&X)", self)
        remove_action.setShortcut("Del")
        remove_action.triggered.connect(self._on_remove_selected)
        file_menu.addAction(remove_action)

        clear_action = QAction("清空列表 (&C)", self)
        clear_action.setShortcut("Ctrl+Shift+C")
        clear_action.triggered.connect(self._on_clear)
        file_menu.addAction(clear_action)

        help_menu = bar.addMenu("帮助 (&H)")
        about_action = QAction("关于 (&A)", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)

        adv_menu = bar.addMenu("高级 (&A)")

        algo_config_action = QAction("算法配置 (&C)", self)
        algo_config_action.setShortcut("Ctrl+Shift+C")
        algo_config_action.triggered.connect(self._on_algo_config)
        adv_menu.addAction(algo_config_action)

        de_manager_action = QAction("决策引擎 (&D)", self)
        de_manager_action.setShortcut("Ctrl+Shift+D")
        de_manager_action.setToolTip("管理 ADE 决策引擎：查看状态、训练数据、探索统计")
        de_manager_action.triggered.connect(self._on_de_manager)
        adv_menu.addAction(de_manager_action)

        theme_config_action = QAction("主题配置 (&T)", self)
        theme_config_action.setShortcut("Ctrl+Shift+T")
        theme_config_action.setToolTip("自定义界面主题颜色和外观设置")
        theme_config_action.triggered.connect(self._on_theme_config)
        adv_menu.addAction(theme_config_action)

    # ========== 工具栏设置 ==========
    def _setup_toolbar(self) -> None:
        self._toolbar_compress = QToolBar("压缩工具栏")
        self._toolbar_compress.setMovable(False)
        self.addToolBar(self._toolbar_compress)

        # [➕ 添加] — popup menu with files + folder options
        self._add_btn = QPushButton("➕ 添加")
        self._add_btn.setToolTip("添加文件或文件夹")
        add_menu = QMenu(self._add_btn)
        add_menu.addAction("添加文件...", self._on_add_files)
        add_menu.addAction("添加文件夹...", self._on_add_folder)
        self._add_btn.setMenu(add_menu)
        self._add_btn.setStyleSheet("QPushButton::menu-indicator { image: none; }")
        self._toolbar_compress.addWidget(self._add_btn)

        self._remove_action = QAction("🗑 删除", self)
        self._remove_action.setToolTip("删除选中的项目")
        self._remove_action.triggered.connect(self._on_remove_selected)
        self._toolbar_compress.addAction(self._remove_action)

        self._toolbar_compress.addSeparator()
        self._toolbar_compress.addWidget(self._algo_selector)
        self._toolbar_compress.addSeparator()

        self._compress_action = QAction("▶ 开始压缩", self)
        self._compress_action.setToolTip("压缩列表中的所有文件")
        self._compress_action.triggered.connect(self._on_compress)
        self._toolbar_compress.addAction(self._compress_action)

        self._cancel_compress_action = QAction("⏹ 取消", self)
        self._cancel_compress_action.setToolTip("取消正在进行的压缩")
        self._cancel_compress_action.triggered.connect(self._on_cancel_compress)
        self._cancel_compress_action.setEnabled(False)
        self._toolbar_compress.addAction(self._cancel_compress_action)

        self._toolbar_compress.addSeparator()

        self._decompress_action = QAction("🔓 解压", self)
        self._decompress_action.setToolTip("解压已压缩的文件")
        self._decompress_action.triggered.connect(self._on_decompress)
        self._toolbar_compress.addAction(self._decompress_action)

        export_action = QAction("💾 导出", self)
        export_action.setToolTip("导出压缩后的文件")
        export_action.triggered.connect(self._on_export)
        self._toolbar_compress.addAction(export_action)

        self._toolbar_compress.addSeparator()

        open_archive_action = QAction("📂 打开压缩包", self)
        open_archive_action.setToolTip("打开 .wcx 压缩包浏览内容")
        open_archive_action.triggered.connect(self._open_archive)
        self._toolbar_compress.addAction(open_archive_action)

        # Browse-mode toolbar (hidden initially)
        self._toolbar_browse = QToolBar("浏览工具栏")
        self._toolbar_browse.setMovable(False)
        self._toolbar_browse.hide()

        back_action = QAction("← 返回", self)
        back_action.setToolTip("返回压缩模式")
        back_action.triggered.connect(self._switch_to_compress_mode)
        self._toolbar_browse.addAction(back_action)

        self._browse_decompress_action = QAction("🔓 解压", self)
        self._browse_decompress_action.setToolTip("解压文件")
        self._browse_decompress_action.triggered.connect(self._on_decompress)
        self._toolbar_browse.addAction(self._browse_decompress_action)

        self._browse_export_action = QAction("💾 导出", self)
        self._browse_export_action.setToolTip("导出文件")
        self._browse_export_action.triggered.connect(self._on_export)
        self._toolbar_browse.addAction(self._browse_export_action)

        self.addToolBar(self._toolbar_browse)

        self._browse_mode = False

    # ========== 中央区域 ==========
    def _setup_central(self) -> None:
        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addWidget(self._table)
        self.setCentralWidget(central)

    # ========== 文件操作 ==========
    def _on_add_files(self) -> None:
        log_ui("file_dialog.open", kind="open_files")
        flush_logging()
        paths, _ = QFileDialog.getOpenFileNames(self, "选择文件", "", "所有文件 (*)")
        log_ui("file_dialog.closed", kind="open_files", accepted=bool(paths), count=len(paths))
        flush_logging()
        if paths:
            logger.info("[add] adding %d files", len(paths))
            self._add_paths(paths, source="file_dialog")

    def _on_add_folder(self) -> None:
        log_ui("file_dialog.open", kind="open_directory")
        flush_logging()
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        log_ui("file_dialog.closed", kind="open_directory", accepted=bool(folder), path=folder or "")
        flush_logging()
        if folder:
            logger.info("[add] adding folder: %s", folder)
            self._add_paths([folder], source="folder_dialog")

    def _apply_added_status(self, msg: str, source: str) -> None:
        try:
            self._statusbar.set_status_text(msg)
            log_ui("add_paths.status_applied", source=source, msg=msg)
        except Exception as e:
            logger.exception("[ui] add_paths status bar failed: %s", e)
        flush_logging()

    def _add_paths(self, paths: list[str], *, source: str = "unspecified") -> None:
        t0 = time.perf_counter()
        rows_before = self._table.rowCount()
        log_ui_flush(
            "add_paths.begin",
            source=source,
            path_count=len(paths),
            rows_before=rows_before,
            preview=preview_paths(paths),
        )
        try:
            files, dirs = self._table.add_paths(paths)
        except Exception as e:
            logger.exception("[add] add_paths failed source=%s: %s", source, e)
            log_ui_flush("add_paths.error", source=source, error=str(e))
            raise
        dt_ms = (time.perf_counter() - t0) * 1000.0
        rows_after = self._table.rowCount()
        logger.info("[add] added %d files, %d dirs from %d paths", files, dirs, len(paths))
        log_ui_flush(
            "add_paths.end",
            source=source,
            files_added=files,
            dirs_added=dirs,
            path_count=len(paths),
            rows_after=rows_after,
            elapsed_ms=f"{dt_ms:.1f}",
        )
        parts = []
        if files > 0:
            parts.append(f"{files} 个文件")
        if dirs > 0:
            parts.append(f"{dirs} 个文件夹")
        if parts:
            msg = f"已添加 {', '.join(parts)}"
            QTimer.singleShot(0, partial(self._apply_added_status, msg, source))

    def _on_clear(self) -> None:
        n = self._table.rowCount()
        log_ui_flush("table.clear_requested", rows_before=n)
        self._table.clear_all()
        self._statusbar.set_status_text("已清空列表")
        log_ui_flush("table.clear_done", rows_after=self._table.rowCount())

    def _on_remove_selected(self) -> None:
        """Delete selected top-level items from the table."""
        rows = self._table.selected_rows
        if not rows:
            self._statusbar.set_status_text("没有选中的项目")
            return
        log_ui_flush("table.remove_requested", rows=str(rows))
        # Remove from bottom up to keep indices valid
        for row in sorted(rows, reverse=True):
            self._table.takeTopLevelItem(row)
        self._statusbar.set_status_text(f"已删除 {len(rows)} 个项目")
        log_ui_flush("table.remove_done", removed=len(rows))

    def _open_archive(self) -> None:
        """Open a .wcx archive and display its contents in browse mode."""
        path, _ = QFileDialog.getOpenFileName(
            self, "打开压缩包", "", "WCX 压缩文件 (*.wcx);;所有文件 (*)"
        )
        if not path:
            return
        log_ui_flush("archive.open", path=path)

        from gui.engine.file_protocol import (
            unpack_compressed_file, unpack_folder_archive, CompressedFileHeader,
        )
        try:
            raw = Path(path).read_bytes()
        except OSError as e:
            QMessageBox.warning(self, "打开失败", f"无法读取文件:\n{e}")
            return

        try:
            outer_header, outer_payload = unpack_compressed_file(raw)
        except Exception as e:
            logger.error("[archive] unpack header failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "打开失败", f"无法解析压缩包头:\n{e}")
            return

        self._last_opened_archive = path
        self._browse_archive_path = path

        # Build uniform list of (header, payload) for tree + decompress
        browse_entries: list[tuple[CompressedFileHeader, bytes]] = []
        if outer_header.is_folder:
            try:
                folder_entries = unpack_folder_archive(raw)
            except Exception as e:
                logger.error("[archive] unpack folder failed: %s", e, exc_info=True)
                QMessageBox.warning(self, "打开失败", f"无法解析文件夹压缩包:\n{e}")
                return
            if not folder_entries:
                QMessageBox.information(self, "提示", "压缩包中没有文件")
                return
            browse_entries = folder_entries
        else:
            browse_entries = [(outer_header, outer_payload)]

        self._browse_entries = browse_entries
        self._switch_to_browse_mode(browse_entries, Path(path).name)

    def _switch_to_browse_mode(self, entries, archive_name: str) -> None:
        """Switch to browse mode showing archive entries as a file tree."""
        from gui.engine.file_protocol import CompressedFileHeader

        self._browse_mode = True
        self._toolbar_compress.hide()
        self._toolbar_browse.show()

        self._table.clear_all()
        # Tree building: prefix tuple → folder QTreeWidgetItem
        folder_map: dict[tuple[str, ...], QTreeWidgetItem] = {}

        for hdr, payload in entries:
            filename = hdr.original_filename or archive_name
            parts = tuple(Path(filename).parts)
            # Ensure ancestor folders exist
            for depth in range(len(parts) - 1):
                prefix = parts[:depth + 1]
                if prefix not in folder_map:
                    folder_item = self._table._create_browse_folder_item(prefix[-1])
                    parent = folder_map.get(prefix[:-1])
                    if parent:
                        parent.addChild(folder_item)
                    else:
                        self._table.addTopLevelItem(folder_item)
                    folder_map[prefix] = folder_item

            # Add file item under its parent folder
            file_item = self._table._create_browse_item_from_header(hdr, len(payload))
            file_item.setData(self._table.COL_NAME, self._table.Record_Role, hdr)
            parent = folder_map.get(parts[:-1])
            if parent:
                parent.addChild(file_item)
            else:
                self._table.addTopLevelItem(file_item)

        self._statusbar.set_status_text(f"浏览: {archive_name} ({len(entries)} 个文件)")

    def _switch_to_compress_mode(self) -> None:
        """Switch back to compress mode."""
        self._browse_mode = False
        self._toolbar_browse.hide()
        self._toolbar_compress.show()
        self._table.clear_all()
        self._statusbar.set_status_text("准备就绪")

    # ========== 拖拽支持 ==========
    def dragEnterEvent(self, event: QDragEnterEvent) -> None:
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event: QDropEvent) -> None:
        urls = event.mimeData().urls()
        paths = [url.toLocalFile() for url in urls if url.isLocalFile()]
        if paths:
            log_ui("drop.accept", url_count=len(urls), local_paths=len(paths))
            flush_logging()
            self._add_paths(paths, source="drop")

    # ========== 压缩操作 ==========
    def _on_compress(self) -> None:
        log_ui_flush("compress.enter")
        logger.info("[compress] _on_compress called")
        if self._worker and self._worker.isRunning():
            logger.warning("[compress] worker already running")
            return

        if self._table.topLevelItemCount() == 0:
            QMessageBox.warning(self, "提示", "请先添加文件！")
            return

        records = self._table.get_records()
        algo = self._algo_selector.current_algorithm
        logger.info("[compress] algorithm=%s, records=%d", algo.value, len(records))
        for i, r in enumerate(records):
            logger.info("[compress]   task[%d]: type=%s, name=%s, status=%s",
                         i, type(r).__name__, getattr(r, 'name', '?'), getattr(r, 'status', '?'))
        # Pre-expand ALL nested folder items so child widgets exist before worker signals
        def _expand_all(item):
            for ci in range(item.childCount()):
                child = item.child(ci)
                child_rec = self._table.get_record(child)
                if isinstance(child_rec, FolderRecord):
                    child.setExpanded(True)
                    _expand_all(child)

        for i in range(self._table.rowCount()):
            item = self._table.topLevelItem(i)
            if item:
                rec = self._table.get_record(item)
                if isinstance(rec, FolderRecord):
                    item.setExpanded(True)
                    _expand_all(item)

        # Compress ALL top-level items (no checkbox — add = compress)
        tasks: list[tuple[int, Record]] = []
        for i in range(self._table.rowCount()):
            item = self._table.topLevelItem(i)
            if item:
                rec = self._table.get_record(item)
                if rec is not None:
                    tasks.append((i, rec))

        log_ui_flush(
            "compress.dispatch",
            algo=algo.value,
            task_count=len(tasks),
        )
        for row, record in tasks:
            if isinstance(record, FileRecord):
                record.status = CompressionStatus.COMPRESSING
            elif isinstance(record, FolderRecord):
                record.ensure_files_loaded()
                record.status = CompressionStatus.COMPRESSING
                for f in record.files:
                    f.status = CompressionStatus.COMPRESSING
            self._table.update_record(record)

        self._compress_action.setEnabled(False)
        self._cancel_compress_action.setEnabled(True)

        self._worker = CompressionWorker(tasks, self._algo_selector.current_algorithm)
        self._worker.row_started.connect(self._on_compress_row_started)
        self._worker.progress.connect(self._on_compress_worker_progress)
        self._worker.finished_row.connect(self._on_compress_row_finished)
        self._worker.error.connect(self._table.mark_error_record)
        self._worker.finished.connect(self._on_compression_finished)

        file_count = sum(r.filenum if isinstance(r, FolderRecord) else 1 for r in records)
        self._statusbar.set_status_text(f"正在压缩 {file_count} 个文件...")
        self._statusbar.set_progress_value(0)
        self._worker.start()
        log_ui_flush("compress.worker_started", task_count=len(tasks), file_count=file_count)

    def _on_cancel_compress(self) -> None:
        if self._worker and self._worker.isRunning():
            log_ui_flush("compress.cancel_requested")
            logger.info("[compress] cancelling worker")
            self._worker.cancel()
            self._statusbar.set_status_text("正在取消压缩...")

    def _on_compress_row_started(self, record: object) -> None:
        try:
            if not isinstance(record, FileRecord):
                return
            tl_idx = self._table.top_level_index_for_record(record)
            if tl_idx is None:
                return
            record.status = CompressionStatus.COMPRESSING
            self._table.update_record(record)

            algo_info = ""
            if hasattr(record, 'algorithm') and record.algorithm:
                algo_name = record.algorithm.value if hasattr(record.algorithm, 'value') else str(record.algorithm)
                algo_info = f" [算法: {algo_name}]"

            decision_info = ""
            if hasattr(record, 'decision_result') and record.decision_result:
                confidence = record.decision_result.confidence * 100
                decision_info = f" (置信度: {confidence:.0f}%)"

            self._statusbar.set_status_text(
                f"正在压缩: {record.name}{algo_info}{decision_info} [{tl_idx + 1}/{self._table.rowCount()}]"
            )
        except Exception as e:
            logger.error("[_on_compress_row_started] CRASH: %s", e, exc_info=True)

    def _on_compress_worker_progress(self, percent: int, text: str) -> None:
        """Mid-job phase text from the worker thread (e.g. read / features / in-memory compress)."""
        if text:
            self._statusbar.set_status_text(text)

    def _on_compress_row_finished(self, record: object) -> None:
        try:
            item = self._table.row_for_record(record)
            if item is None:
                logger.warning("[compress] finished signal but row removed: %s", getattr(record, 'name', '?'))
                return
            logger.info("[compress] finished: name=%s, status=%s, ratio=%.4f",
                         getattr(record, 'name', '?'), getattr(record, 'status', '?'),
                         getattr(record, 'compression_ratio', 0))
            self._table.update_record(record)
            # Force-refresh parent folder row so aggregate stats update
            tl_idx = self._table.top_level_index_for_record(record)
            if tl_idx is not None:
                self._table.update_row(tl_idx)
            done = self._table.count_done_items()
            total = self._table.rowCount()
            progress = int(done / total * 100) if total else 0
            self._statusbar.set_progress_value(progress)
        except Exception as e:
            logger.error("[_on_compress_row_finished] CRASH: %s", e, exc_info=True)

    def _on_compression_finished(self) -> None:
        try:
            self._statusbar.set_progress_value(100)
            if self._worker and self._worker._is_cancelled:
                self._statusbar.set_status_text("压缩已取消")
            else:
                self._statusbar.set_status_text("压缩完成")

                try:
                    from gui.ade.training import get_training_store
                    store = get_training_store()
                    store.save(incremental=True)
                    stats = store.get_stats()
                    logger.info("[compress] training data saved: %d total samples", stats.get('total_samples', 0))
                except Exception as e:
                    logger.debug("[compress] training data save skipped: %s", e)
        except Exception as e:
            logger.error("[_on_compression_finished] CRASH: %s", e, exc_info=True)
        finally:
            self._compress_action.setEnabled(True)
            self._cancel_compress_action.setEnabled(False)
            w = self._worker
            cancelled = bool(w and getattr(w, "_is_cancelled", False))
            log_ui_flush("compress.finished_ui", cancelled=cancelled)

    def _on_browse_decompress_single(self, hdr) -> None:
        """Decompress a single browse-mode entry (right-click → 解压此文件)."""
        from gui.engine.file_protocol import CompressedFileHeader as CFH
        from gui.engine.compressor import CompressionEngine

        if not isinstance(hdr, CFH):
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择解压输出目录")
        if not export_dir:
            return

        browse_entries_data = getattr(self, '_browse_entries', None) or []
        browse_lookup: dict[str, tuple[CFH, bytes]] = {}
        for bh, bp in browse_entries_data:
            browse_lookup[bh.original_filename] = (bh, bp)

        target = browse_lookup.get(hdr.original_filename)
        if target is None:
            QMessageBox.warning(self, "失败", f"未找到文件: {hdr.original_filename}")
            return
        _hdr, wcx_blob = target

        engine = CompressionEngine()
        if not engine.available:
            QMessageBox.critical(self, "错误", "C++ 核心引擎不可用")
            return

        # wcx_blob is already a full WCX file — write directly, do NOT re-pack
        import tempfile
        with tempfile.NamedTemporaryFile(suffix='.wcx', delete=False) as tmp:
            tmp.write(wcx_blob)
            tmp_path = tmp.name
        try:
            output_path = os.path.join(export_dir, _hdr.original_filename)
            os.makedirs(os.path.dirname(output_path) or export_dir, exist_ok=True)
            result = engine.smart_decompress_file(
                tmp_path, output_path, _hdr.algorithm,
            )
            if not getattr(result, 'success', True):
                raise RuntimeError(
                    getattr(result, 'error_message', '') or '解压失败'
                )
            self._statusbar.set_status_text(f"已解压: {_hdr.original_filename}")
            logger.info("浏览模式右键解压成功: %s -> %s", _hdr.original_filename, output_path)
        except Exception as e:
            logger.error("浏览模式右键解压失败 %s: %s", _hdr.original_filename, e, exc_info=True)
            QMessageBox.warning(self, "解压失败", f"{_hdr.original_filename}:\n{e}")
        finally:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass

    # ========== 导出操作 ==========
    def _on_export(self) -> None:
        """Export compressed files.

        Compress mode: exports ALL DONE items (no selection needed).
        Browse mode: exports selected items.
        """
        from gui.engine.file_protocol import (
            pack_compressed_file, make_export_filename, pack_folder_archive,
            strip_wcx_if_present,
            CompressedFileHeader as CFH,
        )
        from pathlib import Path as P

        records: list[Record] = []

        if self._browse_mode:
            # Browse mode: selected items with CompressedFileHeader records
            browse_entries_data = getattr(self, '_browse_entries', None) or []
            browse_lookup: dict[str, tuple[CFH, bytes]] = {}
            for bh, bp in browse_entries_data:
                browse_lookup[bh.original_filename] = (bh, bp)
            for it in self._table.selected_items:
                rec = self._table.get_record(it)
                if isinstance(rec, CFH) and rec.original_filename in browse_lookup:
                    records.append(rec)
        else:
            # Compress mode: ALL DONE top-level items
            for i in range(self._table.topLevelItemCount()):
                rec = self._table.get_record(i)
                if isinstance(rec, FileRecord) and rec.status == CompressionStatus.DONE and (
                    rec.compressed_data or getattr(rec, 'compressed_path', None)
                ):
                    records.append(rec)
                elif isinstance(rec, FolderRecord) and rec.status == CompressionStatus.DONE and rec.success_count > 0:
                    records.append(rec)

        if not records:
            self._statusbar.set_status_text("没有可导出的文件")
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not export_dir:
            log_ui_flush("export.aborted", reason="no_export_dir")
            return

        log_ui_flush("export.begin", count=len(records), export_dir=export_dir)
        exported = 0
        for record in records:
            logger.info("[export] record=%s status=%s type=%s",
                         getattr(record, 'name', '?'),
                         getattr(record, 'status', '?'), type(record).__name__)
            if isinstance(record, FolderRecord):
                record.ensure_files_loaded()
                file_list = []
                folder_root = P(record.path)
                for f in record.files:
                    if f.status != CompressionStatus.DONE:
                        continue
                    blob = file_record_compression_blob(f)
                    if not blob:
                        continue
                    # blob is a full WCX container; strip to raw payload so
                    # pack_folder_archive doesn't double-wrap.
                    raw_payload = strip_wcx_if_present(blob)
                    try:
                        rel = str(P(f.path).relative_to(folder_root))
                    except ValueError:
                        rel = f.name
                    file_list.append((rel, raw_payload, f.algorithm, f.size))
                if not file_list:
                    continue
                archive_data = pack_folder_archive(record.name, file_list)
                export_name = make_export_filename(record.name)
                export_path = os.path.join(export_dir, export_name)
                with open(export_path, 'wb') as fh:
                    fh.write(archive_data)
                exported += 1
            elif isinstance(record, FileRecord) and record.compressed_path:
                import shutil
                export_name = make_export_filename(record.name, record.algorithm)
                export_path = os.path.join(export_dir, export_name)
                shutil.copy2(record.compressed_path, export_path)
                exported += 1
            elif isinstance(record, FileRecord) and record.compressed_data:
                export_name = make_export_filename(record.name, record.algorithm)
                export_path = os.path.join(export_dir, export_name)
                packed = pack_compressed_file(
                    compressed_data=record.compressed_data,
                    algorithm=record.algorithm,
                    original_size=record.size,
                    original_filename=record.name,
                )
                with open(export_path, 'wb') as fh:
                    fh.write(packed)
                exported += 1
            elif isinstance(record, CFH):
                # Browse mode: export compressed entry from archive
                target = browse_lookup.get(record.original_filename)
                if target is None:
                    continue
                hdr, payload = target
                export_name = make_export_filename(
                    P(hdr.original_filename).name, hdr.algorithm,
                )
                export_path = os.path.join(export_dir, export_name)
                packed = pack_compressed_file(
                    compressed_data=payload,
                    algorithm=hdr.algorithm,
                    original_size=hdr.original_size,
                    original_filename=hdr.original_filename,
                )
                with open(export_path, 'wb') as fh:
                    fh.write(packed)
                exported += 1

        logger.info("[export] done: exported=%d", exported)
        log_ui_flush("export.done", exported=exported, export_dir=export_dir if exported else "")
        if exported > 0:
            self._statusbar.set_status_text(f"已导出 {exported} 个文件到 {export_dir}")
        else:
            self._statusbar.set_status_text("没有可导出的文件")

    # ========== 解压操作 ==========
    def _on_decompress(self) -> None:
        """Decompress items from the tree.

        Compress mode: processes ALL DONE items (no selection needed).
        Browse mode: processes selected items from the archive.
        """
        from gui.engine.file_protocol import (
            unpack_compressed_file, CompressedFileHeader, UNIFIED_EXTENSION,
        )
        from gui.engine.compressor import CompressionEngine
        from gui.engine.decompress_log import log_decompress, summarize_result
        from gui.ade.explorer import SilentExplorer
        from pathlib import Path as P

        file_recs: list[FileRecord] = []
        folder_recs: list[FolderRecord] = []
        items: list = []  # browse-mode selected items

        if self._browse_mode:
            # ── Browse mode: toolbar decompresses ALL items ──
            from gui.engine.file_protocol import CompressedFileHeader as CFH
            items = list(self._table.selected_items)
            if not items:
                # No selection → decompress entire archive
                items = self._table._collect_all_browse_items()
            else:
                # Filter to keep only items with CompressedFileHeader records
                items = [it for it in items
                         if isinstance(self._table.get_record(it), CFH)]
            if not items:
                QMessageBox.warning(self, "提示", "没有可解压的文件")
                return
        else:
            # ── Compress mode: ALL DONE items ──
            for i in range(self._table.topLevelItemCount()):
                rec = self._table.get_record(i)
                if isinstance(rec, FileRecord):
                    if rec.status == CompressionStatus.DONE and (
                        rec.compressed_data or getattr(rec, 'compressed_path', None)
                    ):
                        file_recs.append(rec)
                    elif P(rec.path).suffix.lower() == UNIFIED_EXTENSION:
                        try:
                            rec.load_raw_data()
                            header, _ = unpack_compressed_file(rec.raw_data)
                            rec.algorithm = header.algorithm
                            file_recs.append(rec)
                        except Exception:
                            pass
                elif isinstance(rec, FolderRecord):
                    if rec.status == CompressionStatus.DONE and rec.filenum > 0:
                        folder_recs.append(rec)

        total = len(file_recs) + len(folder_recs) + len(items)
        if total == 0:
            QMessageBox.warning(self, "提示", "没有可解压的数据\n（需先压缩文件或打开 .wcx 压缩包）")
            return

        # ── Choose output directory ──
        export_dir = QFileDialog.getExistingDirectory(self, "选择解压输出目录")
        if not export_dir:
            return

        engine = CompressionEngine()
        if not engine.available:
            QMessageBox.critical(self, "错误", "C++ 核心引擎不可用")
            return

        log_ui_flush("decompress.batch_start", file_count=len(file_recs),
                     folder_count=len(folder_recs), browse_count=len(items),
                     export_dir=export_dir)
        log_decompress("gui_batch_begin", total=total, export_dir=export_dir)

        success = 0
        SilentExplorer.begin_user_operation()
        try:
            # ── 1. Single FileRecords ──
            for rec in file_recs:
                try:
                    self._statusbar.set_status_text(f"正在解压: {rec.name}")
                    log_decompress("gui_item_begin", task_type="file", name=rec.name)
                    header = CompressedFileHeader(
                        algorithm=rec.algorithm, original_size=rec.size,
                        compressed_size=_compressed_size(rec),
                        original_filename=rec.name, is_folder=False,
                    )
                    if getattr(rec, 'compressed_path', None) and os.path.isfile(rec.compressed_path):
                        output_path = os.path.join(export_dir, rec.name)
                        result = engine.smart_decompress_file(rec.compressed_path, output_path, rec.algorithm)
                        if not getattr(result, 'success', True):
                            raise RuntimeError(getattr(result, 'error_message', '') or '解压失败')
                        out_sz = os.path.getsize(output_path) if os.path.isfile(output_path) else -1
                        log_decompress("gui_file_done", **summarize_result(result),
                                       output_path=output_path, output_file_bytes=out_sz)
                    elif rec.compressed_data:
                        # Wrap raw compressed payload → temp WCX file → file-based decompress
                        from gui.engine.file_protocol import pack_compressed_file as _pcf
                        import tempfile
                        _wrapped = _pcf(
                            bytes(rec.compressed_data), rec.algorithm, rec.size,
                            rec.name, is_folder=False,
                        )
                        with tempfile.NamedTemporaryFile(suffix='.wcx', delete=False) as _tmp:
                            _tmp.write(_wrapped)
                            _tmp_path = _tmp.name
                        try:
                            output_path = os.path.join(export_dir, rec.name)
                            result = engine.smart_decompress_file(
                                _tmp_path, output_path, rec.algorithm,
                            )
                            if not getattr(result, 'success', True):
                                raise RuntimeError(getattr(result, 'error_message', '') or '解压失败')
                        finally:
                            try:
                                os.unlink(_tmp_path)
                            except OSError:
                                pass
                    else:
                        logger.warning("[decompress] no data for %s", rec.name)
                        continue
                    success += 1
                    logger.info("解压成功: %s -> %s", rec.name, output_path)
                except Exception as e:
                    log_decompress("gui_item_error", level=logging.ERROR, name=rec.name, err=str(e))
                    logger.error("解压失败 %s: %s", rec.name, e, exc_info=True)
                    QMessageBox.warning(self, "解压失败", f"{rec.name}:\n{e}")

            # ── 2. FolderRecords ──
            for rec in folder_recs:
                try:
                    self._statusbar.set_status_text(f"正在解压文件夹: {rec.name}")
                    rec.ensure_files_loaded()
                    folder_root = P(rec.path)
                    for f in rec.files:
                        if f.status != CompressionStatus.DONE:
                            continue
                        try:
                            rel = str(P(f.path).relative_to(folder_root))
                        except ValueError:
                            rel = f.name
                        output_path = os.path.join(export_dir, rel)
                        os.makedirs(os.path.dirname(output_path) or export_dir, exist_ok=True)
                        blob = file_record_compression_blob(f)
                        if not blob:
                            logger.warning("[decompress] folder child skip (no data): %s", f.name)
                            continue
                        # blob is already a full WCX file → temp → file-based decompress
                        import tempfile
                        with tempfile.NamedTemporaryFile(suffix='.wcx', delete=False) as _tmp:
                            _tmp.write(blob)
                            _tmp_path = _tmp.name
                        try:
                            result = engine.smart_decompress_file(
                                _tmp_path, output_path, f.algorithm,
                            )
                            if not getattr(result, 'success', True):
                                raise RuntimeError(
                                    getattr(result, 'error_message', '') or '解压失败'
                                )
                        finally:
                            try:
                                os.unlink(_tmp_path)
                            except OSError:
                                pass
                        success += 1
                    logger.info("文件夹解压完成: %s -> %s (%d 文件)", rec.name, export_dir, rec.filenum)
                except Exception as e:
                    log_decompress("gui_item_error", level=logging.ERROR, name=rec.name, err=str(e))
                    logger.error("解压文件夹失败 %s: %s", rec.name, e, exc_info=True)
                    QMessageBox.warning(self, "解压失败", f"文件夹 {rec.name}:\n{e}")

            # ── 3. Browse-mode entries ──
            from gui.engine.file_protocol import CompressedFileHeader as CFH, strip_wcx_if_present
            browse_entries_data = getattr(self, '_browse_entries', None) or []
            # Build lookup: filename → (header, raw_payload) from stored browse data.
            # raw_payload is the compressed bitstream WITHOUT a WCX header (it was
            # already unpacked by _open_archive / unpack_folder_archive).
            browse_lookup: dict[str, tuple[CFH, bytes]] = {}
            for bh, bp in browse_entries_data:
                browse_lookup[bh.original_filename] = (bh, bp)

            for it in items:
                rec = self._table.get_record(it)
                if not isinstance(rec, CFH):
                    continue
                try:
                    name = rec.original_filename
                    self._statusbar.set_status_text(f"正在解压: {name}")
                    log_decompress("gui_item_begin", task_type="browse", name=name)

                    target = browse_lookup.get(name)
                    if target is None:
                        logger.warning("[decompress] browse entry not found in stored data: %s", name)
                        continue
                    _hdr, raw_payload = target

                    output_path = os.path.join(export_dir, name)
                    os.makedirs(os.path.dirname(output_path) or export_dir, exist_ok=True)
                    # raw_payload may be double-wrapped (old folder archives) or
                    # raw compressed data.  strip_wcx_if_present handles both.
                    raw_payload = strip_wcx_if_present(raw_payload)
                    # Decompress in memory (avoids temp WCX file roundtrip).
                    result = engine.smart_decompress(raw_payload, _hdr.algorithm)
                    if not getattr(result, 'success', True):
                        raise RuntimeError(
                            getattr(result, 'error_message', '') or '解压失败'
                        )
                    part_path = output_path + '.part'
                    try:
                        with open(part_path, 'wb') as f:
                            if result.data:
                                f.write(result.data)
                        os.replace(part_path, output_path)
                    except OSError as e:
                        try:
                            os.unlink(part_path) if os.path.exists(part_path) else None
                        except OSError:
                            pass
                        raise RuntimeError(str(e))
                    success += 1
                    logger.info("浏览模式解压成功: %s -> %s", name, output_path)
                except Exception as e:
                    name = getattr(rec, 'original_filename', '?')
                    log_decompress("gui_item_error", level=logging.ERROR, name=name, err=str(e))
                    logger.error("浏览模式解压失败 %s: %s", name, e, exc_info=True)
                    QMessageBox.warning(self, "解压失败", f"{name}:\n{e}")
        finally:
            SilentExplorer.end_user_operation()

        if success > 0:
            log_decompress("gui_batch_end", success_count=success, export_dir=export_dir)
            log_ui_flush("decompress.batch_done", success_count=success, export_dir=export_dir)
            self._statusbar.set_status_text(f"已解压 {success} 个文件到 {export_dir}")
        else:
            log_ui_flush("decompress.batch_done", success_count=0, export_dir=export_dir)

    # ========== 关于 ==========
    def _on_about(self) -> None:
        QMessageBox.about(
            self,
            "关于 WebCompress Pro",
            "WebCompress Pro v1.0\n"
            "基于 Deflate/LZSS/LZDP/DPFlate 算法的压缩工具\n\n"
            "━━━ WCMP v2 文件协议 ━━━\n\n"
            "统一后缀 .wcx (单文件/文件夹通用)\n"
            "解压时自动从文件头判断类型和算法\n\n"
            "文件头固定部分 (18 字节, 小端序):\n"
            "  偏移  长度  类型    内容\n"
            "  0     4 B   char[4] 魔数 \"WCMP\"\n"
            "  4     1 B   uint8   协议版本 (2)\n"
            "  5     1 B   uint8   算法编码\n"
            "  6     4 B   uint32  原始大小\n"
            "  10    4 B   uint32  压缩大小\n"
            "  14    1 B   uint8   标志位 (bit0=文件夹)\n"
            "  15    2 B   uint16  文件名长度 N\n"
            "  17    1 B   uint8   对齐填充\n"
            "  18    N B   utf-8   文件名(文件夹)或相对路径(子文件)\n\n"
            "算法编码:\n"
            "  1 = Deflate  LZ77 + Huffman\n"
            "  2 = LZSS     LZ77 变体\n"
            "  3 = LZDP   KMP + DP 全局最优\n"
            "  4 = Huffman  纯 Huffman\n"
            "  5 = DPFlate  HashChain DP+Huffman\n"
            "  6 = Gzip     zlib 标准\n\n"
            "文件夹归档: 外层header(is_folder=1) + [file_count] + 内部文件列表\n"
            "每个内部文件保存相对路径，解压后自动还原目录结构\n\n"
            "© 2026 数据结构课程设计"
        )

    def _on_algo_config(self) -> None:
        dlg = AlgorithmConfigDialog(self)
        dlg.exec()

    def _on_de_manager(self) -> None:
        logger.info("[main_window] opening DecisionEngineManagerDialog")
        dlg = DecisionEngineManagerDialog(self)
        dlg.exec()
        logger.info("[main_window] DecisionEngineManagerDialog closed")

    def _on_theme_config(self) -> None:
        logger.info("[main_window] opening ThemeConfigDialog")
        dlg = ThemeConfigDialog(self)
        if dlg.exec() == QDialog.DialogCode.Accepted:
            self.refresh_theme()
        logger.info("[main_window] ThemeConfigDialog closed")

    # ========== 视图：网页可视化 ==========

    # 非右键入口（未接线；可视化仅表格右键 request_* → *_row）
    # def _get_selected_done_record(self) -> FileRecord | None:
    #     rows = self._table.selected_rows
    #     logger.info("[view] _get_selected_done_record called, selected_rows=%s", rows)
    #     if not rows:
    #         logger.warning("[view] no rows selected")
    #         QMessageBox.information(self, "提示", "请先勾选一个已压缩的文件（点击行左侧的☐）")
    #         return None
    #     record = self._table.get_record(rows[0])
    #     logger.info("[view] got record: type=%s, name=%s, status=%s",
    #                  type(record).__name__, getattr(record, 'name', '?'),
    #                  getattr(record, 'status', '?'))
    #     if not isinstance(record, FileRecord):
    #         logger.warning("[view] record is not FileRecord: %s", type(record).__name__)
    #         QMessageBox.information(self, "提示", "请选中一个文件（不支持文件夹）")
    #         return None
    #     if record.status != CompressionStatus.DONE:
    #         logger.warning("[view] record not done: status=%s", record.status.value)
    #         QMessageBox.information(self, "提示", f"该文件状态为 {record.status.value}，请先压缩")
    #         return None
    #     if not file_record_compression_blob(record):
    #         logger.warning("[view] record has no compressed artifact")
    #         QMessageBox.information(self, "提示", "该文件没有可用的压缩数据（内存或磁盘 .wcx）")
    #         return None
    #     logger.info("[view] valid record: name=%s, size=%d, compressed=%d, algo=%s",
    #                  record.name, record.size, _compressed_size(record), record.algorithm.value)
    #     return record

    def _on_view_heatmap_row(self, record: Record) -> None:
        logger.info("[view] heatmap from right-click, name=%s", getattr(record, 'name', '?'))
        logger.info("[view] record: type=%s, name=%s, status=%s",
                     type(record).__name__, getattr(record, 'name', '?'),
                     getattr(record, 'status', '?'))
        if not isinstance(record, FileRecord) or record.status != CompressionStatus.DONE:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法查看热力图")
            return

        # Prefer 3-tier entropy heatmap when .heat file is available
        heat_path = getattr(record, "heat_path", None)
        if heat_path and Path(heat_path).is_file():
            self._open_3tier_heatmap(record)
            return

        # Fallback: old token-based heatmap
        if not file_record_compression_blob(record):
            QMessageBox.information(self, "提示", "该文件没有可用的压缩数据，无法查看热力图")
            return
        self._open_heatmap(record)

    def _open_3tier_heatmap(self, record: FileRecord) -> None:
        """Open the 3-tier entropy heatmap dialog using .heat file + mmap."""
        heat_path = getattr(record, "heat_path", None)
        if not heat_path or not Path(heat_path).is_file():
            QMessageBox.warning(self, "热力图错误", "熵热力图文件 (.heat) 不存在")
            return

        from gui.ui.dialogs.three_tier_heatmap_dialog import ThreeTierHeatmapDialog
        source_path = getattr(record, "path", "")
        viz_path = getattr(record, "viz_path", "") or ""
        compressed_sz = _compressed_size(record)
        original_sz = record.size
        dlg = ThreeTierHeatmapDialog(
            heat_path,
            source_path=source_path,
            compressed_size=compressed_sz,
            original_size=original_sz,
            viz_path=viz_path,
            parent=self,
        )
        dlg.exec()

    def _on_view_folder_heatmap(self, record: FolderRecord) -> None:
        """Open 3-tier folder heatmap — Tier 1 shows all files, Tier 2/3 lazy-load."""
        logger.info("[view] folder heatmap from right-click, name=%s", getattr(record, 'name', '?'))
        record.ensure_files_loaded()
        record.ensure_tree_loaded()
        if not record.files:
            QMessageBox.information(self, "提示", "文件夹中没有文件")
            return

        from gui.ui.dialogs.three_tier_heatmap_dialog import ThreeTierHeatmapDialog
        dlg = ThreeTierHeatmapDialog.from_folder(record, parent=self)
        dlg.exec()

    # def _on_view_heatmap(self) -> None:
    #     logger.info("[view] heatmap from menu")
    #     record = self._get_selected_done_record()
    #     if record:
    #         self._open_heatmap(record)

    def _on_view_comparison_row(self, record: Record) -> None:
        logger.info("[view] comparison from right-click, name=%s", getattr(record, 'name', '?'))
        if isinstance(record, FolderRecord):
            self._run_folder_comparison(record)
        elif isinstance(record, FileRecord) and record.status == CompressionStatus.DONE:
            self._run_comparison(record)
        else:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法对比")

    def _on_view_network_row(self, record: Record) -> None:
        logger.info("[view] network sim from right-click, name=%s", getattr(record, 'name', '?'))
        if not isinstance(record, FileRecord) or record.status != CompressionStatus.DONE:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法模拟")
            return
        self._run_network_sim(record)

    def _on_view_viz_file(self, record: Record) -> None:
        """Open .viz file visualization for a compressed record."""
        logger.info("[view] viz file from right-click, name=%s", getattr(record, 'name', '?'))
        if not isinstance(record, FileRecord):
            return
        viz_path = getattr(record, "viz_path", None)
        if not viz_path or not Path(viz_path).is_file():
            QMessageBox.information(self, "提示", "该文件没有关联的 .viz 可视化数据")
            return
        from gui.ui.dialogs.viz_dialog import VizDialog
        dlg = VizDialog(viz_path, source_path=getattr(record, "path", ""), parent=self)
        dlg.exec()

    def _open_heatmap(self, record: FileRecord) -> None:
        logger.info("[view] opening heatmap for %s (%d bytes, algo=%s)",
                     record.name, record.size, record.algorithm.value)
        try:
            from gui.models import TEXT_EXTENSIONS, SCRIPT_EXTENSIONS
            from gui.engine.token_parser import Token, TokenType

            ext = Path(record.name).suffix.lower() if "." in record.name else ""
            is_text = ext in (TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"}))
            logger.info("[heatmap] ext=%s is_text=%s algo=%s", ext, is_text, record.algorithm.value)

            # Preferred path: derive tokens from .viz MatchEvent section (no re-compression).
            viz_path = getattr(record, "viz_path", None)
            if viz_path and Path(viz_path).is_file() and is_text:
                from gui.engine.viz_loader import VizLoader
                loader = VizLoader(viz_path)
                try:
                    if loader.match_count == 0:
                        logger.info("[heatmap] viz has no MatchEvents, falling back")
                    else:
                        tokens: list[Token] = []
                        for d in loader.iter_match_tokens():
                            t = VizLoader._match_dict_to_token(d)
                            # Estimate compressed size for heatmap ratio coloring.
                            if t.type == TokenType.MATCH:
                                t.compressed_size = 3.0
                            elif t.type == TokenType.LITERAL_RUN:
                                t.compressed_size = float(t.original_length)
                            else:
                                t.compressed_size = 1.0
                            tokens.append(t)
                        logger.info("[heatmap] tokens from viz: %d", len(tokens))

                        text, byte_to_char = _read_file_text_mmap(record.path)

                        from gui.ui.dialogs.heatmap_dialog import HeatmapDialog
                        stats = {
                            'original_size': record.size,
                            'compressed_size': _compressed_size(record),
                            'ratio': record.compression_ratio,
                            'token_count': len(tokens),
                            'match_count': sum(1 for t in tokens if t.type == TokenType.MATCH),
                            'time_ms': record.compression_time_ms,
                        }
                        dlg = HeatmapDialog(text, tokens, byte_to_char,
                                            record.name, record.algorithm.value, stats,
                                            parent=self)
                        dlg.exec()
                        logger.info("[view] heatmap from viz opened")
                        return
                finally:
                    loader.close()

            # Fallback: reverse-parse compressed bitstream (non-viz or binary file).
            container = file_record_compression_blob(record) or b""
            parse_payload = strip_wcx_if_present(container)

            from gui.engine.token_parser import can_parse, get_parser
            if can_parse(record.algorithm) and not getattr(record, 'is_stored', False):
                parser = get_parser(record.algorithm)
                _hm_params = getattr(record, 'compression_config_snapshot', None)
                if parser is not None:
                    if not container:
                        QMessageBox.warning(self, "热力图错误", "无法读取压缩数据（内存或磁盘 .wcx）")
                        return
                    # No parser consumes raw_data anymore; always pass None.
                    pr = parser.parse(parse_payload, None, compression_params=_hm_params)
                    logger.info("[heatmap] parse done, tokens=%d", len(pr.tokens))
                    if is_text:
                        text, byte_to_char = _read_file_text_mmap(record.path)

                        from gui.ui.dialogs.heatmap_dialog import HeatmapDialog
                        stats = {
                            'original_size': record.size,
                            'compressed_size': _compressed_size(record),
                            'ratio': record.compression_ratio,
                            'token_count': len(pr.tokens),
                            'match_count': sum(1 for t in pr.tokens if t.type.value == 'match'),
                            'time_ms': record.compression_time_ms,
                        }
                        huffman_trees = pr.huffman_trees if pr.huffman_trees else None
                        dlg = HeatmapDialog(text, pr.tokens, byte_to_char,
                                            record.name, record.algorithm.value, stats,
                                            huffman_trees=huffman_trees, parent=self)
                        dlg.exec()
                        logger.info("[view] Qt native heatmap opened")
                        return

                    from gui.ui.dialogs.block_heatmap_dialog import BlockHeatmapDialog
                    dlg = BlockHeatmapDialog(
                        raw_data=Path(record.path).read_bytes(),
                        compressed_data=parse_payload,
                        filename=record.name,
                        algorithm=record.algorithm.value,
                        original_size=record.size,
                        compressed_size=_compressed_size(record),
                        time_ms=record.compression_time_ms,
                        parent=self,
                    )
                    dlg.exec()
                    logger.info("[view] token block heatmap dialog closed")
                    return

            from gui.ui.dialogs.block_heatmap_dialog import BlockHeatmapDialog
            logger.info("[view] falling back to block-based heatmap (Qt native)")
            dlg = BlockHeatmapDialog(
                raw_data=Path(record.path).read_bytes(),
                compressed_data=parse_payload,
                filename=record.name,
                algorithm=record.algorithm.value,
                original_size=record.size,
                compressed_size=_compressed_size(record),
                time_ms=record.compression_time_ms,
                block_size=256,
                parent=self,
            )
            dlg.exec()
            logger.info("[view] block heatmap dialog closed")
        except Exception as e:
            logger.error("[view] heatmap failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "热力图错误", f"生成热力图失败:\n{e}")

    # 一下内容请不要删除
    # def _on_view_comparison(self) -> None:
    #     logger.info("[view] comparison from menu")
    #     rows = self._table.selected_rows
    #     if not rows:
    #         QMessageBox.information(self, "提示", "请先勾选一个已压缩的文件（点击行左侧的☐）")
    #         return
    #     record = self._table.get_record(rows[0])
    #     if isinstance(record, FolderRecord):
    #         self._run_folder_comparison(record)
    #         return
    #     if not isinstance(record, FileRecord):
    #         QMessageBox.information(self, "提示", "请选中一个文件或文件夹")
    #         return
    #     if record.status != CompressionStatus.DONE:
    #         QMessageBox.information(self, "提示", f"该文件状态为 {record.status.value}，请先压缩")
    #         return
    #     self._run_comparison(record)

    def _start_comparison_worker(self, record: Record, title: str) -> None:
        if self._comparison_worker and self._comparison_worker.isRunning():
            QMessageBox.information(self, "提示", "已有算法对比任务正在运行")
            return

        from PyQt6.QtWidgets import QProgressDialog

        progress = QProgressDialog("准备算法对比...", "取消", 0, 100, self)
        progress.setWindowTitle(title)
        progress.setWindowModality(Qt.WindowModality.WindowModal)
        progress.setMinimumDuration(0)
        progress.setAutoClose(False)
        progress.setAutoReset(False)
        progress.setValue(0)

        self._comparison_progress = progress
        self._comparison_worker = ComparisonWorker(record, COMPARISON_ALGORITHMS, self)
        self._comparison_worker.progress.connect(self._on_comparison_progress)
        self._comparison_worker.comparison_finished.connect(self._on_comparison_finished)
        self._comparison_worker.failed.connect(self._on_comparison_failed)
        self._comparison_worker.finished.connect(self._on_comparison_thread_finished)
        progress.canceled.connect(self._comparison_worker.cancel)

        self._statusbar.set_status_text("正在生成算法压缩对比...")
        self._statusbar.set_progress_value(0)
        self._comparison_worker.start()

    def _run_comparison(self, record: FileRecord) -> None:
        logger.info("[view] running comparison for %s (%d bytes)", record.name, record.size)
        self._start_comparison_worker(record, "算法压缩对比")

    def _run_folder_comparison(self, record: FolderRecord) -> None:
        logger.info("[view] running folder comparison for %s (%d files)", record.name, record.filenum)
        files = [f for f in record.files if f.size > 0]
        if not files:
            QMessageBox.information(self, "提示", "文件夹中没有可压缩的文件")
            return
        self._start_comparison_worker(record, "文件夹算法对比")

    def _on_comparison_progress(self, value: int, text: str) -> None:
        if self._comparison_progress:
            self._comparison_progress.setValue(value)
            self._comparison_progress.setLabelText(text)
        self._statusbar.set_progress_value(value)
        self._statusbar.set_status_text(text)

    def _on_comparison_finished(self, results: object, name: str, original_size: int) -> None:
        try:
            from gui.ui.dialogs.comparison_dialog import ComparisonDialog
            if self._comparison_progress:
                self._comparison_progress.setValue(100)
                self._comparison_progress.close()
                self._comparison_progress = None
            self._statusbar.set_progress_value(100)
            self._statusbar.set_status_text("算法对比完成")
            dlg = ComparisonDialog(list(results), name, original_size, parent=self)
            dlg.exec()
        except Exception as e:
            logger.error("[view] comparison result handling failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "对比错误", f"显示算法对比失败:\n{e}")

    def _on_comparison_failed(self, message: str) -> None:
        if self._comparison_progress:
            self._comparison_progress.close()
            self._comparison_progress = None
        self._statusbar.set_status_text("算法对比失败")
        self._statusbar.set_progress_value(0)
        QMessageBox.warning(self, "对比错误", f"生成算法对比失败:\n{message}")

    def _on_comparison_thread_finished(self) -> None:
        if self._comparison_worker and self._comparison_worker._is_cancelled:
            if self._comparison_progress:
                self._comparison_progress.close()
                self._comparison_progress = None
            self._statusbar.set_status_text("算法对比已取消")
            self._statusbar.set_progress_value(0)
        self._comparison_worker = None

    # 一下内容请不要删除
    # def _on_view_network(self) -> None:
    #     logger.info("[view] network sim from menu")
    #     record = self._get_selected_done_record()
    #     if record:
    #         self._run_network_sim(record)

    def _run_network_sim(self, record: FileRecord) -> None:
        logger.info("[view] running network sim for %s (orig=%d, comp=%d, time=%.1fms)",
                     record.name, record.size, _compressed_size(record), record.compression_time_ms)
        try:
            from gui.ui.dialogs.network_sim_dialog import NetworkSimDialog
            dlg = NetworkSimDialog(
                original_size=record.size,
                compressed_size=_compressed_size(record),
                compression_time_ms=record.compression_time_ms,
                filename=record.name,
                algorithm=record.algorithm.value,
                parent=self,
            )
            dlg.exec()
            logger.info("[view] network sim dialog closed")
        except Exception as e:
            logger.error("[view] network sim failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "网络模拟错误", f"生成网络传输模拟失败:\n{e}")

    def _on_webpage_heatmap(self, record: Record) -> None:
        logger.info("[view] webpage heatmap from right-click, name=%s", getattr(record, 'name', '?'))
        if not isinstance(record, FolderRecord):
            QMessageBox.information(self, "提示", "网页资源热力图仅支持文件夹")
            return
        record.ensure_files_loaded()
        try:
            html_file = None
            for f in record.files:
                ext = Path(f.name).suffix.lower()
                if ext in (".html", ".htm") and f.raw_data:
                    html_file = f
                    break
            if html_file is None:
                QMessageBox.information(self, "提示", "文件夹中未找到 HTML 文件")
                return

            html_content = html_file.raw_data.decode('utf-8', errors='replace')

            resource_ratios = {}
            for f in record.files:
                if f.status == CompressionStatus.DONE and compressed_payload_size(f) > 0:
                    resource_ratios[f.name] = f.compression_ratio

            from gui.windows.webpage_heatmap import generate_webpage_heatmap, open_webpage_heatmap
            result = generate_webpage_heatmap(
                html_content=html_content,
                resource_ratios=resource_ratios,
                folder_name=record.name,
                total_original=record.total_original,
                total_compressed=record.total_compressed,
            )
            open_webpage_heatmap(result)
            logger.info("[view] webpage heatmap opened")
        except Exception as e:
            logger.error("[view] webpage heatmap failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "热力图错误", f"生成网页资源热力图失败:\n{e}")

    def _on_view_decision_detail(self, record: Record) -> None:
        logger.info("[view] decision detail from right-click, name=%s", getattr(record, 'name', '?'))
        if not isinstance(record, FileRecord):
            QMessageBox.information(self, "提示", "仅支持文件记录")
            return
        try:
            dialog = DecisionDetailDialog(record, parent=self)
            dialog.exec()
            logger.info("[view] decision detail dialog closed")
        except Exception as e:
            logger.error("[view] decision detail failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "决策详情错误", f"显示决策详情失败:\n{e}")

    def _on_folder_summary(self, record: Record) -> None:
        logger.info("[view] folder summary from right-click, name=%s", getattr(record, 'name', '?'))
        if not isinstance(record, FolderRecord):
            QMessageBox.information(self, "提示", "仅支持文件夹")
            return
        try:
            dialog = QDialog(self)
            dialog.setWindowTitle(f"文件夹压缩报告 - {record.name}")
            dialog.setMinimumSize(900, 600)

            layout = QVBoxLayout(dialog)
            report_widget = FolderReportWidget(record, dialog)
            layout.addWidget(report_widget)

            button_box = QHBoxLayout()
            export_btn = QPushButton("导出HTML")
            export_btn.clicked.connect(lambda: self._export_folder_html(record))
            close_btn = QPushButton("关闭")
            close_btn.clicked.connect(dialog.close)

            button_box.addStretch()
            button_box.addWidget(export_btn)
            button_box.addWidget(close_btn)
            layout.addLayout(button_box)

            dialog.exec()
        except Exception as e:
            logger.error("[view] folder summary failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "报告错误", f"生成文件夹压缩报告失败:\n{e}")

    def _export_folder_html(self, record: FolderRecord):
        """导出文件夹压缩报告为HTML文件"""
        try:
            html = self._generate_folder_summary_html(record)
            file_path, _ = QFileDialog.getSaveFileName(
                self,
                "保存报告",
                f"{record.name}_report.html",
                "HTML文件 (*.html)"
            )
            if file_path:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(html)
                QMessageBox.information(self, "成功", f"报告已保存到:\n{file_path}")
        except Exception as e:
            logger.error("[view] export folder html failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "导出错误", f"导出报告失败:\n{e}")

    def _generate_folder_summary_html(self, record: FolderRecord) -> str:
        record.ensure_files_loaded()
        import json
        rows_html = ""
        for f in record.files:
            status_str = f.status.value
            size_str = formatted_size(f.size)
            ratio_str = f"{f.compression_ratio * 100:.1f}%({formatted_size(_compressed_size(f))}/{formatted_size(f.size)})" if (f.status == CompressionStatus.DONE and (f.compressed_data or f.compressed_path)) else "--"
            time_str = f"{f.compression_time_ms:.1f}ms" if f.status == CompressionStatus.DONE else "--"
            stored_str = " [stored]" if getattr(f, 'is_stored', False) else ""
            algo_str = f.algorithm.value if f.status == CompressionStatus.DONE else "--"
            type_str = f.type.value if hasattr(f, 'type') else "?"
            rows_html += f"<tr><td>{f.name}</td><td>{type_str}</td><td>{size_str}</td><td>{algo_str}{stored_str}</td><td>{ratio_str}</td><td>{time_str}</td><td>{status_str}</td></tr>\n"

        total_ratio = f"{record.compression_ratio * 100:.1f}%({formatted_size(record.total_compressed)}/{formatted_size(record.total_original)})"
        total_time = f"{record.total_time_ms:.1f}ms"
        success = record.success_count
        total = record.filenum
        stored = sum(1 for f in record.files if getattr(f, 'is_stored', False))

        return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>文件夹压缩报告 - {record.name}</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ font-family: "Microsoft YaHei", "Segoe UI", sans-serif; background: {ThemeManager.hex('bg_primary')}; color: {ThemeManager.hex('text_primary')}; padding: 24px; }}
h1 {{ font-size: 20px; margin-bottom: 8px; }}
.meta {{ color: {ThemeManager.hex('text_secondary')}; font-size: 13px; margin-bottom: 20px; }}
.stats {{ display: flex; gap: 16px; margin-bottom: 24px; flex-wrap: wrap; }}
.stat {{ background: {ThemeManager.hex('bg_surface')}; border-radius: 8px; padding: 14px 18px; min-width: 120px; }}
.stat .num {{ font-size: 22px; font-weight: 700; color: {ThemeManager.hex('text_primary')}; }}
.stat .desc {{ font-size: 12px; color: {ThemeManager.hex('text_secondary')}; margin-top: 4px; }}
table {{ width: 100%; border-collapse: collapse; font-size: 13px; }}
th {{ background: {ThemeManager.hex('bg_surface')}; color: {ThemeManager.hex('text_secondary')}; padding: 10px 12px; text-align: left; border-bottom: 1px solid {ThemeManager.hex('border_dark')}; }}
td {{ padding: 8px 12px; border-bottom: 1px solid {ThemeManager.hex('bg_surface')}; }}
tr:hover {{ background: {ThemeManager.hex('bg_surface')}; }}
.stored {{ color: {ThemeManager.hex('warning')}; }}
</style>
</head>
<body>
<h1>📋 文件夹压缩报告</h1>
<div class="meta">{record.name} &nbsp;|&nbsp; {record.filenum} 个文件 &nbsp;|&nbsp; 成功 {success}/{total} &nbsp;|&nbsp; stored {stored}</div>
<div class="stats">
  <div class="stat"><div class="num">{formatted_size(record.size)}</div><div class="desc">原始总大小</div></div>
  <div class="stat"><div class="num">{formatted_size(record.total_compressed)}</div><div class="desc">压缩后总大小</div></div>
  <div class="stat"><div class="num">{total_ratio}</div><div class="desc">总压缩率</div></div>
  <div class="stat"><div class="num">{total_time}</div><div class="desc">总耗时</div></div>
</div>
<table>
<thead><tr><th>文件名</th><th>类型</th><th>大小</th><th>算法</th><th>压缩率</th><th>耗时</th><th>状态</th></tr></thead>
<tbody>
{rows_html}
</tbody>
</table>
</body>
</html>"""


class FolderReportWidget(QWidget):
    """Qt原生实现的文件夹压缩报告组件，替代HTML渲染"""

    def __init__(self, record: FolderRecord, parent=None):
        super().__init__(parent)
        self._record = record
        self._setup_ui()
        self._populate_data()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(24, 24, 24, 24)
        main_layout.setSpacing(16)

        title = QLabel("📋 文件夹压缩报告")
        title.setStyleSheet(f"font-size: 20px; font-weight: bold; color: {ThemeManager.hex('text_primary')};")
        main_layout.addWidget(title)

        meta = self._create_meta_label()
        main_layout.addWidget(meta)

        stats_widget = self._create_stats_widget()
        main_layout.addWidget(stats_widget)

        table = self._create_table()
        main_layout.addWidget(table)

    def _create_meta_label(self) -> QLabel:
        record = self._record
        success = record.success_count
        total = record.filenum
        stored = sum(1 for f in record.files if getattr(f, 'is_stored', False))
        meta_text = f"{record.name}  |  {total} 个文件  |  成功 {success}/{total}  |  stored {stored}"
        label = QLabel(meta_text)
        label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 13px;")
        return label

    def _create_stats_widget(self) -> QWidget:
        widget = QWidget()
        layout = QHBoxLayout(widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(16)

        record = self._record
        total_ratio = f"{record.compression_ratio * 100:.1f}%({formatted_size(record.total_compressed)}/{formatted_size(record.total_original)})"
        total_time = f"{record.total_time_ms:.1f}ms"

        stats_data = [
            (formatted_size(record.size), "原始总大小"),
            (formatted_size(record.total_compressed), "压缩后总大小"),
            (total_ratio, "总压缩率"),
            (total_time, "总耗时")
        ]

        for num, desc in stats_data:
            stat_card = self._create_stat_card(num, desc)
            layout.addWidget(stat_card)

        layout.addStretch()
        return widget

    def _create_stat_card(self, number: str, description: str) -> QWidget:
        card = QWidget()
        card.setMinimumWidth(120)
        card.setStyleSheet(f"""
            background: {ThemeManager.hex('bg_surface')};
            border-radius: 8px;
            padding: 14px 18px;
        """)
        layout = QVBoxLayout(card)
        layout.setContentsMargins(18, 14, 18, 14)
        layout.setSpacing(4)

        num_label = QLabel(number)
        num_label.setStyleSheet(f"font-size: 22px; font-weight: 700; color: {ThemeManager.hex('text_primary')};")
        layout.addWidget(num_label)

        desc_label = QLabel(description)
        desc_label.setStyleSheet(f"font-size: 12px; color: {ThemeManager.hex('text_secondary')};")
        layout.addWidget(desc_label)

        return card

    def _create_table(self) -> QTableWidget:
        table = QTableWidget()
        table.setColumnCount(7)
        table.setHorizontalHeaderLabels(["文件名", "类型", "大小", "算法", "压缩率", "耗时", "状态"])
        table.horizontalHeader().setStretchLastSection(True)
        table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        table.setAlternatingRowColors(True)
        table.setStyleSheet(f"""
            QTableWidget {{
                gridline-color: {ThemeManager.hex('border_dark')};
                background: {ThemeManager.hex('bg_primary')};
                color: {ThemeManager.hex('text_primary')};
                font-size: 13px;
                border: 1px solid {ThemeManager.hex('border_dark')};
                border-radius: 4px;
                selection-background-color: {ThemeManager.hex('accent')};
                selection-color: {ThemeManager.hex('text_primary')};
                show-decoration-selected: 1;
            }}
            QTableWidget::item {{
                padding: 8px 12px;
                border-bottom: 1px solid {ThemeManager.hex('bg_surface')};
            }}
            QTableWidget::item:selected {{
                background: {ThemeManager.hex('accent')};
                color: {ThemeManager.hex('text_primary')};
            }}
            QHeaderView::section {{
                background: {ThemeManager.hex('bg_surface')};
                color: {ThemeManager.hex('text_secondary')};
                padding: 10px 12px;
                border: none;
                border-bottom: 2px solid {ThemeManager.hex('border_dark')};
                font-weight: bold;
            }}
        """)
        return table

    def _populate_data(self):
        table = self.findChild(QTableWidget)
        if not table:
            return

        record = self._record
        table.setRowCount(len(record.files))

        for row, f in enumerate(record.files):
            status_str = f.status.value
            size_str = formatted_size(f.size)
            ratio_str = f"{f.compression_ratio * 100:.1f}%({formatted_size(_compressed_size(f))}/{formatted_size(f.size)})" if (f.status == CompressionStatus.DONE and (f.compressed_data or f.compressed_path)) else "--"
            time_str = f"{f.compression_time_ms:.1f}ms" if f.status == CompressionStatus.DONE else "--"
            stored_str = " [stored]" if getattr(f, 'is_stored', False) else ""
            algo_str = f.algorithm.value if f.status == CompressionStatus.DONE else "--"
            type_str = f.type.value if hasattr(f, 'type') else "?"

            name_item = QTableWidgetItem(f.name)
            table.setItem(row, 0, name_item)

            type_item = QTableWidgetItem(type_str)
            table.setItem(row, 1, type_item)

            size_item = QTableWidgetItem(size_str)
            table.setItem(row, 2, size_item)

            algo_item = QTableWidgetItem(f"{algo_str}{stored_str}")
            if stored_str:
                algo_item.setForeground(QColor(ThemeManager.hex('warning')))
            table.setItem(row, 3, algo_item)

            ratio_item = QTableWidgetItem(ratio_str)
            table.setItem(row, 4, ratio_item)

            time_item = QTableWidgetItem(time_str)
            table.setItem(row, 5, time_item)

            status_item = QTableWidgetItem(status_str)
            table.setItem(row, 6, status_item)

        table.resizeColumnsToContents()


class DecisionDetailDialog(QDialog):
    """右键「查看决策详情」：展示基础特征与 ADE 决策；避免 Unicode 下标等字符在部分字体下显示为方块。"""

    def __init__(self, record: FileRecord, parent=None):
        super().__init__(parent)
        self.record = record
        self.setWindowTitle(f"决策详情 - {record.name}")
        self.setMinimumSize(600, 500)
        # 主窗口样式含 QWidget 背景，会级联到子对话框；若不套 full_dialog_sheet，QLabel 可能
        # 无明确前景色，在深色主题下特征行会像「乱码/碎点」。与其它对话框一致。
        self.setStyleSheet(ThemeManager.full_dialog_sheet())
        self._setup_ui()

    @staticmethod
    def _scalar_from_base_features(bf, field_key: str, default: float = 0.0) -> float:
        if bf is None:
            return default
        if isinstance(bf, dict):
            raw = bf.get(field_key, default)
        else:
            raw = getattr(bf, field_key, default)
        try:
            v = float(raw)
        except (TypeError, ValueError):
            return default
        if math.isnan(v) or math.isinf(v):
            return default
        return v

    @staticmethod
    def _format_feature_text(val: float, unit: str) -> str:
        if unit == "%":
            return f"{val:.4f}" if abs(val) < 10 else f"{val:.2%}"
        if unit == "bits/byte":
            return f"{val:.4f}"
        return f"{val:.4f}"

    @staticmethod
    def _label_text(s) -> str:
        if s is None:
            return ""
        if isinstance(s, (bytes, bytearray)):
            return bytes(s).decode("utf-8", errors="replace")
        return str(s)

    def _params_display_for_decision(self, algorithm: AlgorithmType, params: dict | None) -> str:
        """与「高级设置 → 算法配置」同一套标签、顺序、枚举文案与单位后缀。"""
        from gui.engine.compressor import CompressionEngine

        base = CompressionEngine.get_config().get(algorithm, {})
        merged = merge_decision_overrides_into_algo_config(algorithm, base, params)
        return "\n".join(format_algorithm_config_param_lines(algorithm, merged))

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(8, 8, 8, 8)

        from gui.ade.engine import DecisionEngine

        file_info_group = QGroupBox("文件信息")
        file_form = QFormLayout(file_info_group)
        file_form.addRow("文件名:", QLabel(self.record.name))
        file_form.addRow("完整路径:", QLabel(str(Path(self.record.path).resolve())))
        file_form.addRow("文件大小:", QLabel(formatted_size(self.record.size)))
        file_form.addRow("资源类型:", QLabel(self.record.type.value))
        if self.record.extension:
            file_form.addRow("扩展名:", QLabel(self.record.extension))
        detected_ft = getattr(self.record, 'detected_file_type', None)
        if detected_ft is not None:
            ft_name = getattr(detected_ft, 'name', str(detected_ft))
            file_form.addRow("检测类型 (Magic):", QLabel(ft_name))
        content_layout.addWidget(file_info_group)

        features_group = QGroupBox("特征分析 (20维基础向量)")
        features_form = QFormLayout(features_group)
        
        bf = getattr(self.record, 'base_features', None)
        if bf is not None:
            feature_labels = [
                ("file_size_log2", "文件大小 (log2)", ""),
                ("magic_confidence", "魔数置信度", ""),
                ("printable_ratio", "可打印ASCII占比", "%"),
                ("shannon_entropy", "香农熵", "bits/byte"),
                ("min_entropy", "最小熵", "bits/byte"),
                ("unique_byte_ratio", "唯一字节比率", "%"),
                ("mean_byte_normalized", "字节均值 (归一化)", "/255"),
                ("std_byte_normalized", "字节标准差", "/115"),
                ("longest_run_log2", "最长连续字节 (log2)", ""),
                ("zero_byte_ratio", "零字节占比", "%"),
                ("high_bit_ratio", "高位字节占比 (>=128)", "%"),
                ("header_entropy", "头部熵 (1KB)", "bits/byte"),
                ("local_entropy_variance", "局部熵方差", ""),
                ("block_boundary_density", "块边界密度", ""),
                ("skewness", "偏度", ""),
                ("kurtosis", "超额峰度", ""),
                ("unique_bigram_ratio", "唯一二元组比率", "%"),
                ("bigram_topk_concentration", "二元组Top-10集中度", "%"),
                ("rle_potential", "RLE压缩潜力", ""),
                ("dict_potential", "字典/LZ77潜力", ""),
            ]
            for field_key, label, unit in feature_labels:
                val = self._scalar_from_base_features(bf, field_key)
                text = self._format_feature_text(val, unit)
                features_form.addRow(label + ":", QLabel(text))
        else:
            entropy_val = getattr(self.record, 'content_entropy', 0.0)
            repetition_val = getattr(self.record, 'repetition_ratio', 0.0)
            features_form.addRow("香农熵:", QLabel(f"{entropy_val:.4f}" if entropy_val > 0 else "未计算"))
            features_form.addRow("重复率:", QLabel(f"{repetition_val:.2%}" if repetition_val > 0 else "未计算"))
            features_form.addRow("基础特征:", QLabel("未提取 (请先执行压缩)"))
        content_layout.addWidget(features_group)

        decision_group = QGroupBox("ADE 决策详情")
        decision_form = QFormLayout(decision_group)
        
        decision_result = getattr(self.record, 'decision_result', None)
        if decision_result:
            algo_name = decision_result.algorithm.value if hasattr(decision_result.algorithm, 'value') else str(decision_result.algorithm)
            confidence_pct = decision_result.confidence * 100
            decision_form.addRow("推荐算法:", QLabel(algo_name))
            decision_form.addRow("置信度:", QLabel(f"{confidence_pct:.1f}%"))
            decision_form.addRow("决策原因:", QLabel(self._label_text(decision_result.reason) or "无"))
            params_lbl = QLabel(
                self._params_display_for_decision(decision_result.algorithm, decision_result.params)
            )
            params_lbl.setWordWrap(True)
            params_lbl.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            decision_form.addRow("使用参数:", params_lbl)
        else:
            de = DecisionEngine.get()
            try:
                new_decision = de.decide(self.record) if de._ade else None
                if new_decision:
                    self.record.decision_result = new_decision
                    algo_name = new_decision.algorithm.value if hasattr(new_decision.algorithm, 'value') else str(new_decision.algorithm)
                    confidence_pct = new_decision.confidence * 100
                    decision_form.addRow("推荐算法:", QLabel(algo_name))
                    decision_form.addRow("置信度:", QLabel(f"{confidence_pct:.1f}%"))
                    decision_form.addRow("决策原因:", QLabel(self._label_text(new_decision.reason) or "无"))
                    np_lbl = QLabel(self._params_display_for_decision(new_decision.algorithm, new_decision.params))
                    np_lbl.setWordWrap(True)
                    np_lbl.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
                    decision_form.addRow("使用参数:", np_lbl)
                else:
                    decision_form.addRow("状态:", QLabel("ADE 未初始化或无法决策"))
            except Exception as e:
                decision_form.addRow("状态:", QLabel(f"重新分析失败: {e}"))

        content_layout.addWidget(decision_group)

        if self.record.status == CompressionStatus.DONE:
            results_group = QGroupBox("实际压缩结果")
            results_form = QFormLayout(results_group)
            
            actual_algo = self.record.algorithm.value if hasattr(self.record.algorithm, 'value') else str(self.record.algorithm)
            results_form.addRow("使用算法:", QLabel(actual_algo))
            results_form.addRow("压缩率:", QLabel(f"{self.record.compression_ratio * 100:.2f}%"))
            results_form.addRow("压缩耗时:", QLabel(f"{self.record.compression_time_ms:.1f} ms"))
            
            comp_sz = _compressed_size(self.record)
            savings = self.record.size - comp_sz
            savings_pct = (savings / self.record.size * 100) if self.record.size > 0 else 0
            results_form.addRow("原始大小:", QLabel(formatted_size(self.record.size)))
            results_form.addRow("压缩后大小:", QLabel(formatted_size(comp_sz)))
            results_form.addRow("节省空间:", QLabel(f"{formatted_size(savings)} ({savings_pct:.1f}%)"))
            
            if decision_result and decision_result.algorithm != self.record.algorithm:
                results_form.addRow("算法变更:", QLabel(
                    f"{decision_result.algorithm.value} → {actual_algo}"
                ))

            content_layout.addWidget(results_group)

        scroll.setWidget(content)
        layout.addWidget(scroll)
        
        btn_layout = QHBoxLayout()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addStretch()
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)

