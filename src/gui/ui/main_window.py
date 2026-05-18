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
    LZDP_DP_VIZ_MAX_SIZE,
    STREAMING_CHUNK_SIZE_KB,
    STREAMING_THRESHOLD_MB,
)
from gui.engine.file_protocol import (
    compression_blob_for_visualization,
    dpflate_format_byte,
    file_record_compression_blob,
    prepare_token_parse_payload,
    strip_wcx_if_present,
)
from gui.config.settings import load_config, get_defaults
from gui.config.theme import ThemeManager
from gui.ui.table import FileTableWidget
from gui.ui.worker import CompressionWorker, ComparisonWorker, COMPARISON_ALGORITHMS
from gui.utils.interaction_log import log_ui, log_ui_flush, preview_paths
from gui.utils.logging import flush_logging


def _compressed_size(record: Record) -> int:
    """Stored compressed artifact size (memory or ``compressed_path``)."""
    return compressed_payload_size(record)


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
        apply_cfg_btn = QPushButton("应用配置")
        apply_cfg_btn.setToolTip("保存「配置」页中的数值到 webcompress_settings.json 并生效")
        apply_cfg_btn.clicked.connect(self._on_apply_de_config)
        btn_layout.addWidget(apply_cfg_btn)
        restore_cfg_btn = QPushButton("恢复默认")
        restore_cfg_btn.clicked.connect(self._on_de_cfg_restore_defaults)
        btn_layout.addWidget(restore_cfg_btn)
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

        try:
            from gui.ade.training import get_training_store
            ts = get_training_store().get_stats()
            jsonl_total = int(ts.get("total_samples", 0) or 0)
            jsonl_explore = int(ts.get("exploration_samples", 0) or 0)
        except Exception:
            jsonl_total = 0
            jsonl_explore = 0

        info_group = QGroupBox("探索器概览")
        info_form = QFormLayout(info_group)

        enabled = stats.get('enabled', False)
        self._expl_enabled_val = QLabel("✅ 启用" if enabled else "⏸ 已禁用")
        info_form.addRow("探索状态:", self._expl_enabled_val)

        self._expl_jsonl_total_val = QLabel(f"{jsonl_total}")
        self._expl_jsonl_total_val.setToolTip("ade_training_v3.jsonl 中全部训练行数")
        info_form.addRow("JSONL 训练样本:", self._expl_jsonl_total_val)

        self._expl_jsonl_explore_val = QLabel(f"{jsonl_explore}")
        self._expl_jsonl_explore_val.setToolTip("JSONL 中 is_exploration=true 的行数（持久化探索样本）")
        info_form.addRow("JSONL 探索样本:", self._expl_jsonl_explore_val)

        total = stats.get('total_samples', 0)
        explore = stats.get('explore_count', 0)
        discovery = stats.get('discovery_count', 0)
        self._expl_total_val = QLabel(f"{total}")
        self._expl_total_val.setToolTip("本次运行内 bandit 臂更新次数（内存，重启清零）")
        info_form.addRow("本次运行臂统计:", self._expl_total_val)
        self._expl_explore_val = QLabel(f"{explore} ({explore/max(1,total)*100:.1f}%)")
        self._expl_explore_val.setToolTip("本次运行内成功完成的静默探索次数")
        info_form.addRow("本次探索次数:", self._expl_explore_val)
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
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll_body = QWidget()
        scroll.setWidget(scroll_body)
        scroll_layout = QVBoxLayout(scroll_body)

        from gui.config.settings import get_silent_explore_enabled, load_config as _de_cfg_load

        config_group = QGroupBox("探索器与 ADE（可编辑，点「应用配置」保存）")
        config_form = QFormLayout(config_group)

        self._cfg_enabled_btn = _streaming_follow_toggle_button(
            "启用静默探索（后台对比压缩）",
            checked=get_silent_explore_enabled(_de_cfg_load()),
        )
        self._cfg_enabled_btn.setToolTip(
            "关闭：不启动后台静默压缩采样。\n"
            "开启：在部分压缩完成后可能异步试运行其他算法；"
            "正在压缩/解压时不会新起静默任务。"
        )
        config_form.addRow("", self._cfg_enabled_btn)

        self._cfg_epsilon_spin = QDoubleSpinBox()
        self._cfg_epsilon_spin.setRange(0.02, 0.60)
        self._cfg_epsilon_spin.setDecimals(3)
        self._cfg_epsilon_spin.setSingleStep(0.01)
        config_form.addRow("基础探索率 (ε):", self._cfg_epsilon_spin)

        self._cfg_alpha_spin = QDoubleSpinBox()
        self._cfg_alpha_spin.setRange(0.1, 5.0)
        self._cfg_alpha_spin.setDecimals(2)
        self._cfg_alpha_spin.setSingleStep(0.05)
        config_form.addRow("UCB 系数 (α):", self._cfg_alpha_spin)

        self._cfg_max_concurrent_spin = QSpinBox()
        self._cfg_max_concurrent_spin.setRange(1, 8)
        config_form.addRow("探索排队线程上限:", self._cfg_max_concurrent_spin)

        self._cfg_timeout_spin = QDoubleSpinBox()
        self._cfg_timeout_spin.setRange(1.0, 600.0)
        self._cfg_timeout_spin.setDecimals(0)
        self._cfg_timeout_spin.setSuffix(" s")
        config_form.addRow("单次探索超时:", self._cfg_timeout_spin)

        self._cfg_budget_spin = QSpinBox()
        self._cfg_budget_spin.setRange(5, 100)
        self._cfg_budget_spin.setSuffix(" %")
        self._cfg_budget_spin.setToolTip("探索累计耗时上限 = 主压缩累计耗时 × 该百分比")
        config_form.addRow("探索时间预算:", self._cfg_budget_spin)

        self._cfg_min_file_spin = QSpinBox()
        self._cfg_min_file_spin.setRange(0, 10 * 1024 * 1024)
        self._cfg_min_file_spin.setSuffix(" B")
        config_form.addRow("最小探索文件:", self._cfg_min_file_spin)

        self._cfg_max_l2_spin = QSpinBox()
        self._cfg_max_l2_spin.setRange(1, 4096)
        self._cfg_max_l2_spin.setSuffix(" MB")
        config_form.addRow("L2 参数探索上限:", self._cfg_max_l2_spin)

        self._cfg_warmup_spin = QSpinBox()
        self._cfg_warmup_spin.setRange(1, 10000)
        config_form.addRow("Warmup 样本数:", self._cfg_warmup_spin)

        self._cfg_ucb_gap_spin = QDoubleSpinBox()
        self._cfg_ucb_gap_spin.setRange(0.0, 1.0)
        self._cfg_ucb_gap_spin.setDecimals(3)
        self._cfg_ucb_gap_spin.setSingleStep(0.01)
        config_form.addRow("UCB 差距阈值:", self._cfg_ucb_gap_spin)

        self._cfg_l1_spin = QSpinBox()
        self._cfg_l1_spin.setRange(1, 100)
        config_form.addRow("L1 最少样本:", self._cfg_l1_spin)

        self._cfg_l2_spin = QSpinBox()
        self._cfg_l2_spin.setRange(1, 100)
        config_form.addRow("L2 最少样本:", self._cfg_l2_spin)

        io_gb = QGroupBox("静默探索 · 流式 I/O")
        io_form = QFormLayout(io_gb)
        self._cfg_explore_thr_spin = QDoubleSpinBox()
        self._cfg_explore_thr_spin.setRange(0.0, 4096.0)
        self._cfg_explore_thr_spin.setDecimals(1)
        self._cfg_explore_thr_spin.setSuffix(" MB")
        self._cfg_explore_thr_spin.setToolTip("≤ 阈值：内存压缩；> 阈值：按分块大小流式读盘")
        io_form.addRow("流式分块阈值:", self._cfg_explore_thr_spin)

        self._cfg_explore_chunk_spin = QSpinBox()
        self._cfg_explore_chunk_spin.setRange(64, 65536)
        self._cfg_explore_chunk_spin.setSingleStep(64)
        self._cfg_explore_chunk_spin.setSuffix(" KB")
        io_form.addRow("流式分块大小:", self._cfg_explore_chunk_spin)

        self._cfg_retrain_spin = QSpinBox()
        self._cfg_retrain_spin.setRange(1, 100000)
        self._cfg_retrain_spin.setToolTip("JSONL 新增样本达到该数后触发参数回归补充训练")
        io_form.addRow("补充训练阈值:", self._cfg_retrain_spin)

        scroll_layout.addWidget(config_group)
        scroll_layout.addWidget(io_gb)

        stages = [
            ("冷启动 (0-50)", 0.40),
            ("学习期 (51-500)", 0.25),
            ("成熟期 (501-2000)", 0.15),
            ("稳定期 (2000+)", 0.08),
        ]
        stage_text = "\n".join(f"  {s[0]}: ε={s[1]}" for s in stages)
        self._cfg_stages_val = QLabel(stage_text)
        self._cfg_stages_val.setStyleSheet("color: #888; font-size: 11px;")
        scroll_layout.addWidget(self._cfg_stages_val)

        scroll_layout.addStretch()
        layout.addWidget(scroll, 1)

        self._de_cfg_load_from_disk()

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

        self._train_rf_btn = QPushButton("训练 RF 模型")
        self._train_rf_btn.setToolTip(
            "从 ade_training_v3.jsonl 重新训练随机森林（default_model.bin），"
            "训练完成后自动加载到决策引擎"
        )
        self._train_rf_btn.clicked.connect(self._on_train_rf_model)
        action_layout.addWidget(self._train_rf_btn)

        self._rf_train_hint = QLabel("")
        self._rf_train_hint.setStyleSheet("color: #888; font-size: 12px; padding: 4px 0;")
        self._rf_train_hint.setWordWrap(True)
        action_layout.addWidget(self._rf_train_hint)

        self._rf_train_worker = None
        layout.addWidget(action_group)

    def _de_cfg_load_from_disk(self) -> None:
        """Populate config-tab spinboxes from persisted settings + live explorer."""
        try:
            from gui.config.settings import (
                get_ade_explore_streaming_chunk_kb,
                get_ade_explore_streaming_threshold_mb,
                get_ade_explorer_tunables,
                get_ade_retrain_min_new_samples,
                get_silent_explore_enabled,
                load_config,
            )
            cfg = load_config()
            t = get_ade_explorer_tunables(cfg)
            self._cfg_enabled_btn.setChecked(get_silent_explore_enabled(cfg))
            self._cfg_epsilon_spin.setValue(float(t["epsilon_base"]))
            self._cfg_alpha_spin.setValue(float(t["alpha_ucb"]))
            self._cfg_max_concurrent_spin.setValue(int(t["max_concurrent"]))
            self._cfg_timeout_spin.setValue(float(t["timeout_seconds"]))
            self._cfg_budget_spin.setValue(int(round(float(t["budget_ratio"]) * 100)))
            self._cfg_min_file_spin.setValue(int(t["min_file_size_bytes"]))
            self._cfg_max_l2_spin.setValue(
                max(1, int(t["max_file_size_for_l2"]) // (1024 * 1024))
            )
            self._cfg_warmup_spin.setValue(int(t["warmup_samples"]))
            self._cfg_ucb_gap_spin.setValue(float(t["ucb_gap_threshold"]))
            self._cfg_l1_spin.setValue(int(t["l1_min_samples"]))
            self._cfg_l2_spin.setValue(int(t["l2_min_samples"]))
            self._cfg_explore_thr_spin.setValue(
                float(get_ade_explore_streaming_threshold_mb(cfg))
            )
            self._cfg_explore_chunk_spin.setValue(
                int(get_ade_explore_streaming_chunk_kb(cfg))
            )
            self._cfg_retrain_spin.setValue(int(get_ade_retrain_min_new_samples(cfg)))
        except Exception as e:
            logger.debug("[de_manager] _de_cfg_load_from_disk: %s", e)

    def _on_apply_de_config(self) -> None:
        from gui.config.settings import load_config, save_config
        from gui.ade.explorer import SilentExplorer

        full = load_config()
        full.setdefault("ade", {})
        full["ade"]["silent_explore_enabled"] = bool(self._cfg_enabled_btn.isChecked())
        full["ade"]["explore_streaming_threshold_mb"] = float(
            self._cfg_explore_thr_spin.value()
        )
        full["ade"]["explore_streaming_chunk_size_kb"] = int(
            self._cfg_explore_chunk_spin.value()
        )
        full["ade"]["retrain_min_new_samples"] = int(self._cfg_retrain_spin.value())
        full["ade"]["explorer"] = {
            "epsilon_base": float(self._cfg_epsilon_spin.value()),
            "alpha_ucb": float(self._cfg_alpha_spin.value()),
            "max_concurrent": int(self._cfg_max_concurrent_spin.value()),
            "timeout_seconds": float(self._cfg_timeout_spin.value()),
            "budget_ratio": float(self._cfg_budget_spin.value()) / 100.0,
            "min_file_size_bytes": int(self._cfg_min_file_spin.value()),
            "max_file_size_for_l2": int(self._cfg_max_l2_spin.value()) * 1024 * 1024,
            "warmup_samples": int(self._cfg_warmup_spin.value()),
            "ucb_gap_threshold": float(self._cfg_ucb_gap_spin.value()),
            "l1_min_samples": int(self._cfg_l1_spin.value()),
            "l2_min_samples": int(self._cfg_l2_spin.value()),
        }
        save_config(full)
        SilentExplorer.apply_tunables_from_settings()
        self._refresh()
        QMessageBox.information(self, "已保存", "决策引擎 / 探索器配置已写入配置文件并生效。")

    def _on_de_cfg_restore_defaults(self) -> None:
        from gui.config.settings import get_defaults

        ade = get_defaults().get("ade", {})
        explorer = ade.get("explorer", {})
        self._cfg_enabled_btn.setChecked(bool(ade.get("silent_explore_enabled", False)))
        self._cfg_epsilon_spin.setValue(float(explorer.get("epsilon_base", 0.25)))
        self._cfg_alpha_spin.setValue(float(explorer.get("alpha_ucb", 1.41)))
        self._cfg_max_concurrent_spin.setValue(int(explorer.get("max_concurrent", 2)))
        self._cfg_timeout_spin.setValue(float(explorer.get("timeout_seconds", 30)))
        self._cfg_budget_spin.setValue(int(round(float(explorer.get("budget_ratio", 0.3)) * 100)))
        self._cfg_min_file_spin.setValue(int(explorer.get("min_file_size_bytes", 1024)))
        self._cfg_max_l2_spin.setValue(
            max(1, int(explorer.get("max_file_size_for_l2", 100 * 1024 * 1024)) // (1024 * 1024))
        )
        self._cfg_warmup_spin.setValue(int(explorer.get("warmup_samples", 50)))
        self._cfg_ucb_gap_spin.setValue(float(explorer.get("ucb_gap_threshold", 0.05)))
        self._cfg_l1_spin.setValue(int(explorer.get("l1_min_samples", 3)))
        self._cfg_l2_spin.setValue(int(explorer.get("l2_min_samples", 5)))
        self._cfg_explore_thr_spin.setValue(
            float(ade.get("explore_streaming_threshold_mb", STREAMING_THRESHOLD_MB))
        )
        self._cfg_explore_chunk_spin.setValue(
            int(ade.get("explore_streaming_chunk_size_kb", STREAMING_CHUNK_SIZE_KB))
        )
        self._cfg_retrain_spin.setValue(int(ade.get("retrain_min_new_samples", 50)))

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

    def _on_train_rf_model(self) -> None:
        from PyQt6.QtWidgets import QProgressDialog

        from gui.ade.rf_train import MIN_RF_SAMPLES, RFTrainWorker, count_rf_ready_samples
        from gui.engine.compressor import CompressionEngine

        if self._rf_train_worker is not None and self._rf_train_worker.isRunning():
            QMessageBox.information(self, "训练中", "RF 训练正在进行，请稍候。")
            return

        if not CompressionEngine().available:
            QMessageBox.warning(self, "无法训练", "压缩引擎（core_engine）不可用。")
            return

        ready = count_rf_ready_samples()
        if ready < MIN_RF_SAMPLES:
            QMessageBox.warning(
                self,
                "样本不足",
                f"当前可用于 RF 的样本约 {ready} 条，至少需要 {MIN_RF_SAMPLES} 条。\n"
                "请继续压缩文件并开启静默探索以收集带算法标签的样本。",
            )
            return

        reply = QMessageBox.question(
            self,
            "训练 RF 模型",
            f"将使用 JSONL 中约 {ready} 条有效样本训练随机森林，并覆盖\n"
            f"{get_training_store_path_hint()}\n"
            "下的 default_model.bin（旧文件会自动备份）。\n\n是否继续？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        self._train_rf_btn.setEnabled(False)
        progress = QProgressDialog("正在训练 RF 模型…", None, 0, 0, self)
        progress.setWindowTitle("ADE 训练")
        progress.setWindowModality(Qt.WindowModality.WindowModal)
        progress.setMinimumDuration(0)
        progress.setCancelButton(None)
        progress.show()

        worker = RFTrainWorker(self)
        self._rf_train_worker = worker

        def on_progress(msg: str) -> None:
            progress.setLabelText(msg)

        def on_done(result) -> None:
            progress.close()
            self._train_rf_btn.setEnabled(True)
            self._rf_train_worker = None
            self._refresh()
            if result.ok:
                QMessageBox.information(self, "训练完成", result.message)
            else:
                QMessageBox.warning(self, "训练失败", result.message)

        worker.progress.connect(on_progress)
        worker.finished_with_result.connect(on_done)
        worker.start()

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
            try:
                from gui.ade.training import get_training_store
                ts = get_training_store().get_stats()
                if hasattr(self, "_expl_jsonl_total_val"):
                    self._expl_jsonl_total_val.setText(f"{ts.get('total_samples', 0)}")
                if hasattr(self, "_expl_jsonl_explore_val"):
                    self._expl_jsonl_explore_val.setText(
                        f"{ts.get('exploration_samples', 0)}"
                    )
            except Exception:
                pass
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

        if hasattr(self, "_cfg_epsilon_spin"):
            self._de_cfg_load_from_disk()

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

        try:
            from gui.ade.rf_train import MIN_RF_SAMPLES, count_rf_ready_samples
            from gui.engine.compressor import CompressionEngine

            ready = count_rf_ready_samples()
            eng_ok = CompressionEngine().available
            if hasattr(self, "_train_rf_btn"):
                self._train_rf_btn.setEnabled(eng_ok and ready >= MIN_RF_SAMPLES)
            if hasattr(self, "_rf_train_hint"):
                if not eng_ok:
                    hint = "core_engine 不可用，无法训练 RF。"
                elif ready < MIN_RF_SAMPLES:
                    hint = f"RF 可用样本约 {ready}/{MIN_RF_SAMPLES}，继续压缩以收集数据。"
                else:
                    hint = f"可训练 RF（约 {ready} 条有效样本）。"
                self._rf_train_hint.setText(hint)
        except Exception:
            pass


def get_training_store_path_hint() -> str:
    try:
        from gui.ade.training import get_training_store

        return str(get_training_store()._base_dir)
    except Exception:
        return "ade/"


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
        from gui.engine.compressor import CompressionEngine

        CompressionEngine.reload_from_file()
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

        ade_note = QLabel(
            "静默探索、探索流式 I/O 与 AC-UCB 参数请在「决策引擎 (D)」→「配置」页设置。"
        )
        ade_note.setWordWrap(True)
        ade_note.setStyleSheet(
            f"color: {ThemeManager.hex('text_muted')}; font-size: 11px; padding: 4px 0 8px 0;"
        )
        layout.addWidget(ade_note)

        try:
            from gui.engine.compressor import CompressionEngine

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
            }

            for algo in [AlgorithmType.DEFLATE, AlgorithmType.LZDP, AlgorithmType.LZSS, AlgorithmType.DPFLATE, AlgorithmType.GZIP, AlgorithmType.BROTLI, AlgorithmType.ZSTD]:
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
                "LZDP 文件管线与内存压缩共用同一套 ``LZDPCompressor`` 语义（整文件明文缓冲后单次 compress / compress_dp；见 docs/design/lzdp-file-pipeline-design.md）。"
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

        patch: dict[AlgorithmType, dict[str, int]] = {}
        for algo, widgets in self._spinboxes.items():
            patch[algo] = {}
            for key, widget in widgets.items():
                if isinstance(widget, QComboBox):
                    patch[algo][key] = widget.currentData()
                else:
                    patch[algo][key] = widget.value()

        CompressionEngine.set_config(patch)

        if self._streaming_threshold_spin:
            CompressionEngine.set_streaming_threshold(
                float(self._streaming_threshold_spin.value())
            )

        from gui.config.settings import load_config, save_config

        full = load_config()
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

        self.accept()


class CompressDemoDialog(QDialog):
    def __init__(self, record: FileRecord, algorithm: AlgorithmType, parent=None):
        super().__init__(parent)
        self._record = record
        self._algorithm = algorithm
        self.setWindowTitle(f"压缩演示 - {record.name}")
        self.setMinimumWidth(500)
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        info_layout = QVBoxLayout()
        info_layout.addWidget(QLabel(f"文件: {self._record.path}"))
        info_layout.addWidget(QLabel(f"大小: {formatted_size(self._record.size)}"))
        info_layout.addWidget(QLabel(f"类型: {self._record.type.value}"))
        info_layout.addWidget(QLabel(f"算法: {self._algorithm.value}"))

        status_text = self._record.status.value
        info_layout.addWidget(QLabel(f"状态: {status_text}"))

        if compressed_payload_size(self._record) > 0:
            stored_tag = " [stored]" if getattr(self._record, 'is_stored', False) else ""
            info_layout.addWidget(QLabel(f"压缩后: {formatted_size(_compressed_size(self._record))}{stored_tag}"))
            info_layout.addWidget(QLabel(f"压缩率: {self._record.compression_ratio * 100:.1f}%"))
            info_layout.addWidget(QLabel(f"耗时: {self._record.compression_time_ms:.1f}ms"))

        layout.addLayout(info_layout)
        layout.addSpacing(16)

        btn_layout = QHBoxLayout()
        close_btn = QPushButton("Close")
        close_btn.clicked.connect(self.accept)
        btn_layout.addStretch()
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


# ============================================================
#  主窗口
# ============================================================

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("WebCompress")
        self.resize(WINDOW_WIDTH, WINDOW_HEIGHT)

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

        self._table.request_demo.connect(self._on_compress_demo)
        self._table.request_heatmap.connect(self._on_view_heatmap_row)
        self._table.request_comparison.connect(self._on_view_comparison_row)
        self._table.request_network.connect(self._on_view_network_row)
        self._table.request_webpage_heatmap.connect(self._on_webpage_heatmap)
        self._table.request_folder_summary.connect(self._on_folder_summary)
        self._table.request_decision_detail.connect(self._on_view_decision_detail)

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

        add_file_action = QAction("添加文件 (&F)", self)
        add_file_action.setShortcut("Ctrl+F")
        add_file_action.triggered.connect(self._on_add_files)
        file_menu.addAction(add_file_action)

        add_folder_action = QAction("添加文件夹 (&D)", self)
        add_folder_action.setShortcut("Ctrl+D")
        add_folder_action.triggered.connect(self._on_add_folder)
        file_menu.addAction(add_folder_action)

        file_menu.addSeparator()

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
        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)

        self._select_all_action = QAction("☐ 全选", self)
        self._select_all_action.setToolTip("全选/取消全选")
        self._select_all_action.triggered.connect(self._on_toggle_select_all)
        self._table.selection_changed.connect(self._update_select_button)
        toolbar.addAction(self._select_all_action)

        toolbar.addSeparator()

        toolbar.addWidget(self._algo_selector)
        toolbar.addSeparator()

        self._compress_action = QAction("▶ 开始压缩", self)
        self._compress_action.setToolTip("开始压缩选中的文件")
        self._compress_action.triggered.connect(self._on_compress)
        toolbar.addAction(self._compress_action)

        self._cancel_compress_action = QAction("⏹ 取消", self)
        self._cancel_compress_action.setToolTip("取消正在进行的压缩")
        self._cancel_compress_action.triggered.connect(self._on_cancel_compress)
        self._cancel_compress_action.setEnabled(False)
        toolbar.addAction(self._cancel_compress_action)

        toolbar.addSeparator()

        self._decompress_action = QAction("🔓 解压", self)
        self._decompress_action.setToolTip("解压选中的压缩文件")
        self._decompress_action.triggered.connect(self._on_decompress)
        toolbar.addAction(self._decompress_action)

        toolbar.addSeparator()

        export_action = QAction("💾 导出结果", self)
        export_action.setToolTip("导出压缩后的文件")
        export_action.triggered.connect(self._on_export)
        toolbar.addAction(export_action)

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
        self._update_select_button()
        log_ui_flush("table.clear_done", rows_after=self._table.rowCount())

    def _on_toggle_select_all(self) -> None:
        self._table.select_all(checked=not self._table.is_all_selected)

    def _update_select_button(self) -> None:
        if self._table.is_all_selected:
            self._select_all_action.setText("☑ 取消全选")
        else:
            self._select_all_action.setText("☐ 全选")

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

        selected = self._table.selected_rows
        logger.info("[compress] selected_rows=%s", selected)
        if not selected:
            QMessageBox.warning(self, "提示", "请先选中文件！")
            return

        records = self._table.get_records(selected)
        algo = self._algo_selector.current_algorithm
        logger.info("[compress] algorithm=%s, records=%d", algo.value, len(records))
        for i, r in enumerate(records):
            logger.info("[compress]   task[%d]: type=%s, name=%s, status=%s",
                         i, type(r).__name__, getattr(r, 'name', '?'), getattr(r, 'status', '?'))
        tasks = list(zip(selected, records))

        log_ui_flush(
            "compress.dispatch",
            algo=algo.value,
            task_count=len(tasks),
            rows=str(selected)[:200],
        )
        for row, record in tasks:
            if isinstance(record, FileRecord):
                record.status = CompressionStatus.COMPRESSING
            elif isinstance(record, FolderRecord):
                record.ensure_files_loaded()
                record.status = CompressionStatus.COMPRESSING
                for f in record.files:
                    f.status = CompressionStatus.COMPRESSING
            self._table.update_row(row)

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
            row = self._table.row_for_record(record)
            if row is None:
                return
            record.status = CompressionStatus.COMPRESSING
            self._table.update_row(row)

            algo_info = ""
            if hasattr(record, 'algorithm') and record.algorithm:
                algo_name = record.algorithm.value if hasattr(record.algorithm, 'value') else str(record.algorithm)
                algo_info = f" [算法: {algo_name}]"

            decision_info = ""
            if hasattr(record, 'decision_result') and record.decision_result:
                confidence = record.decision_result.confidence * 100
                decision_info = f" (置信度: {confidence:.0f}%)"

            self._statusbar.set_status_text(
                f"正在压缩: {record.name}{algo_info}{decision_info} [{row + 1}/{self._table.rowCount()}]"
            )
        except Exception as e:
            logger.error("[_on_compress_row_started] CRASH: %s", e, exc_info=True)

    def _on_compress_worker_progress(self, percent: int, text: str) -> None:
        """Mid-job phase text from the worker thread (e.g. read / features / in-memory compress)."""
        if text:
            self._statusbar.set_status_text(text)

    def _on_compress_row_finished(self, record: object) -> None:
        try:
            row = self._table.row_for_record(record)
            if row is None:
                logger.warning("[compress] finished signal but row removed: %s", getattr(record, 'name', '?'))
                return
            logger.info("[compress] row %s finished: name=%s, status=%s, ratio=%.4f",
                         row, getattr(record, 'name', '?'), getattr(record, 'status', '?'),
                         getattr(record, 'compression_ratio', 0))
            self._table.update_row(row)
            done = sum(
                1 for r in range(self._table.rowCount())
                if (item := self._table.item(r, self._table.COL_STATUS))
                and (item.text().startswith(CompressionStatus.DONE.value) or item.text() == CompressionStatus.FAILED.value)
            )
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

    # ========== 压缩演示 ==========
    def _on_compress_demo(self, row: int) -> None:
        logger.info("[demo] START row=%d", row)
        record = self._table.get_record(row)
        if not isinstance(record, FileRecord):
            QMessageBox.information(self, "提示", "压缩演示仅支持单文件")
            return
        if record.status != CompressionStatus.DONE:
            QMessageBox.information(self, "提示", "请先完成压缩后再使用压缩演示")
            return
        if getattr(record, "is_stored", False):
            QMessageBox.information(self, "提示", "原样存储的文件无压缩演示")
            return

        from gui.engine.token_parser import (
            can_parse,
            fetch_demo_dp_visualization,
            parse_for_demo,
        )
        from gui.models import TEXT_EXTENSIONS, SCRIPT_EXTENSIONS

        if not can_parse(record.algorithm):
            CompressDemoDialog(record, self._algo_selector.current_algorithm, parent=self).exec()
            return

        try:
            record.load_raw_data()
        except Exception as e:
            QMessageBox.warning(self, "压缩演示", f"无法读取原文件:\n{e}")
            return
        if not record.raw_data:
            QMessageBox.warning(self, "压缩演示", "原文件为空或无法读取")
            return
        if len(record.raw_data) > LZDP_DP_VIZ_MAX_SIZE:
            QMessageBox.information(
                self,
                "提示",
                f"文件超过演示大小上限 ({LZDP_DP_VIZ_MAX_SIZE // 1024} KiB)，请使用较小语料。",
            )
            return

        ext = Path(record.name).suffix.lower() if "." in record.name else ""
        is_text = ext in (
            TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"})
        )
        _demo_params = getattr(record, "compression_config_snapshot", None)
        logger.info(
            "[demo] memory demo path algo=%s is_text=%s raw=%d",
            record.algorithm.value,
            is_text,
            len(record.raw_data),
        )

        try:
            pr = parse_for_demo(record.algorithm, record.raw_data, _demo_params)
        except Exception as e:
            logger.warning("[demo] parse_for_demo failed: %s", e, exc_info=True)
            QMessageBox.warning(self, "压缩演示", f"演示数据生成失败:\n{e}")
            return

        logger.info("[demo] parse_for_demo tokens=%d", len(pr.tokens))

        if not is_text:
            QMessageBox.information(
                self,
                "提示",
                "当前文件类型不支持文本交互演示，请使用文本/脚本类语料。",
            )
            return

        dp_viz = None
        if record.algorithm in (AlgorithmType.LZDP, AlgorithmType.DPFLATE):
            dp_viz = fetch_demo_dp_visualization(
                record.algorithm, record.raw_data, _demo_params
            )

        has_lz_dp = record.algorithm == AlgorithmType.LZDP and (
            pr.tokens or dp_viz is not None
        )
        has_lzss = record.algorithm == AlgorithmType.LZSS and pr.tokens
        has_flate = record.algorithm in (AlgorithmType.DPFLATE, AlgorithmType.DEFLATE) and (
            pr.tokens or (record.algorithm == AlgorithmType.DPFLATE and dp_viz is not None)
        )

        if not (has_lz_dp or has_lzss or has_flate):
            QMessageBox.warning(
                self,
                "压缩演示",
                "未能生成演示数据（请确认 core_engine 已构建且算法参数有效）。",
            )
            return

        text = record.raw_data.decode("utf-8", errors="replace")
        byte_to_char = []
        char_idx = 0
        for ch in text:
            byte_len = len(ch.encode("utf-8"))
            for _ in range(byte_len):
                byte_to_char.append(char_idx)
            char_idx += 1

        if record.algorithm == AlgorithmType.LZDP:
            from gui.ui.dialogs.lz_demo_dialog import LZDPDPDialog

            LZDPDPDialog(
                text,
                pr.tokens,
                byte_to_char,
                dp_viz,
                record.name,
                record.algorithm.value,
                parent=self,
            ).exec()
            return

        if record.algorithm == AlgorithmType.LZSS:
            from gui.ui.dialogs.lz_demo_dialog import LZSliderDialog

            LZSliderDialog(
                text, pr.tokens, byte_to_char, record.name, record.algorithm.value, parent=self
            ).exec()
            return

        if record.algorithm in (AlgorithmType.DPFLATE, AlgorithmType.DEFLATE):
            from gui.ui.dialogs.flate_demo_dialog import FlateDemoDialog

            FlateDemoDialog(
                text,
                pr.tokens,
                byte_to_char,
                record.name,
                record.algorithm.value,
                huffman_trees=pr.huffman_trees if pr.huffman_trees else None,
                dp_viz=dp_viz,
                parent=self,
            ).exec()
            return

        CompressDemoDialog(record, self._algo_selector.current_algorithm, parent=self).exec()

    def _decompress_payload(self, engine, header, payload) -> bytes:
        from gui.engine.decompress_log import log_decompress, summarize_result

        if header.algorithm == AlgorithmType.NONE:
            log_decompress(
                "gui_payload_skip",
                reason="stored_none",
                payload_bytes=len(payload),
            )
            return payload
        log_decompress(
            "gui_payload_begin",
            algo=header.algorithm.value,
            header_original=header.original_size,
            header_compressed=header.compressed_size,
            payload_bytes=len(payload),
        )
        from gui.ade.explorer import SilentExplorer

        with SilentExplorer.user_compression_priority():
            result = engine.smart_decompress(payload, header.algorithm)
        fields = summarize_result(result)
        log_decompress("gui_payload_engine", **fields)
        if not getattr(result, "success", True):
            em = (getattr(result, "error_message", None) or "").strip() or "解压失败"
            log_decompress(
                "gui_payload_fail",
                level=logging.ERROR,
                err=em,
                **fields,
            )
            raise RuntimeError(em)
        out = bytes(result.data)
        if header.original_size and len(out) != int(header.original_size):
            log_decompress(
                "gui_payload_size_mismatch",
                level=logging.WARNING,
                expected=int(header.original_size),
                actual=len(out),
                algo=header.algorithm.value,
            )
        return out

    # ========== 导出操作 ==========
    def _on_export(self) -> None:
        from gui.engine.file_protocol import (
            make_export_filename,
            pack_folder_archive,
            wcx_bytes_for_file_record,
        )
        from pathlib import Path as P

        selected = self._table.selected_rows
        if not selected:
            cur = self._table.currentRow()
            if cur >= 0:
                selected = [cur]
            else:
                self._statusbar.set_status_text("没有选中的文件")
                return

        done_count = 0
        for row in selected:
            record = self._table.get_record(row)
            if not record:
                continue
            if isinstance(record, FileRecord) and record.status == CompressionStatus.DONE and (
                record.compressed_data or record.compressed_path
            ):
                done_count += 1
            elif isinstance(record, FolderRecord) and record.status == CompressionStatus.DONE and record.success_count > 0:
                done_count += 1

        if done_count == 0:
            self._statusbar.set_status_text("选中的文件中没有已完成压缩的文件")
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not export_dir:
            log_ui_flush("export.aborted", reason="no_export_dir")
            return

        log_ui_flush("export.begin", rows=str(selected), export_dir=export_dir)
        logger.info("[export] rows=%s", selected)
        exported = 0
        for row in selected:
            record = self._table.get_record(row)
            logger.info("[export] row=%d record=%s status=%s type=%s",
                         row, getattr(record, 'name', '?'),
                         getattr(record, 'status', '?'), type(record).__name__)
            if not record or record.status != CompressionStatus.DONE:
                logger.warning("[export] SKIP row=%d: status=%s (need DONE)", row, getattr(record, 'status', None))
                continue
            if isinstance(record, FolderRecord):
                record.ensure_files_loaded()
                logger.info("[export] FolderRecord: %d files, %d done",
                             len(record.files), record.success_count)
                file_list = []
                folder_root = P(record.path)
                for f in record.files:
                    if f.status != CompressionStatus.DONE:
                        continue
                    blob = file_record_compression_blob(f)
                    if not blob:
                        logger.warning("[export] SKIP file %s: status=%s has_data=%s",
                                       f.name, f.status, bool(f.compressed_data or f.compressed_path))
                        continue
                    try:
                        rel = str(P(f.path).relative_to(folder_root))
                    except ValueError:
                        rel = f.name
                    file_list.append((rel, blob, f.algorithm, f.size))
                if not file_list:
                    continue
                archive_data = pack_folder_archive(record.name, file_list)
                export_name = make_export_filename(record.name)
                export_path = os.path.join(export_dir, export_name)
                with open(export_path, 'wb') as file:
                    file.write(archive_data)
                exported += 1
            elif isinstance(record, FileRecord):
                try:
                    packed = wcx_bytes_for_file_record(record)
                except Exception as e:
                    logger.error(
                        "[export] pack failed row=%d file=%s: %s",
                        row,
                        getattr(record, "name", "?"),
                        e,
                        exc_info=True,
                    )
                    continue
                if not packed:
                    logger.warning(
                        "[export] SKIP row=%d: no compression artifact", row
                    )
                    continue
                export_name = make_export_filename(record.name, record.algorithm)
                export_path = os.path.join(export_dir, export_name)
                with open(export_path, "wb") as f:
                    f.write(packed)
                exported += 1

        logger.info("[export] done: exported=%d", exported)
        log_ui_flush("export.done", exported=exported, export_dir=export_dir if exported else "")
        if exported > 0:
            self._statusbar.set_status_text(f"已导出 {exported} 个文件到 {export_dir}")
        else:
            self._statusbar.set_status_text("没有可导出的文件")

    # ========== 解压操作 ==========
    def _on_decompress(self) -> None:
        from gui.engine.file_protocol import (
            unpack_compressed_file, detect_algorithm_from_file,
            unpack_folder_archive, pack_folder_archive,
            CompressedFileHeader,
            UNIFIED_EXTENSION,
        )
        from pathlib import Path as P

        selected = self._table.selected_rows
        if not selected:
            cur = self._table.currentRow()
            if cur >= 0:
                selected = [cur]
            else:
                QMessageBox.warning(self, "提示", "请先选中要解压的文件！")
                return

        log_ui_flush("decompress.selection", rows=str(selected))

        decompress_tasks: list[tuple[str, int, Record]] = []
        for row in selected:
            record = self._table.get_record(row)
            if not record:
                continue
            if isinstance(record, FileRecord):
                if record.status == CompressionStatus.DONE and (
                    record.compressed_data or getattr(record, "compressed_path", None)
                ):
                    decompress_tasks.append(('session', row, record))
                elif P(record.path).suffix.lower() == UNIFIED_EXTENSION:
                    try:
                        from gui.engine.file_protocol import unpack_compressed_file

                        record.load_raw_data()
                        header, _ = unpack_compressed_file(record.raw_data)
                        record.algorithm = header.algorithm
                        decompress_tasks.append(('disk', row, record))
                    except Exception as ex:
                        logger.warning(
                            "[decompress] skip row %s (%s): not a valid WCX — %s",
                            row,
                            record.path,
                            ex,
                        )
            elif isinstance(record, FolderRecord) and record.status == CompressionStatus.DONE and record.success_count > 0:
                decompress_tasks.append(('folder_session', row, record))

        if not decompress_tasks:
            log_ui_flush("decompress.aborted", reason="no_tasks", rows=str(selected))
            QMessageBox.warning(self, "提示", "选中的文件中没有可解压的压缩文件\n（需为已压缩文件或 .wcx 格式）")
            return

        from gui.engine.compressor import CompressionEngine
        engine = CompressionEngine()
        if not engine.available:
            QMessageBox.critical(self, "错误", "C++ 核心引擎不可用")
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择解压输出目录")
        if not export_dir:
            log_ui_flush("decompress.aborted", reason="no_export_dir")
            return

        from gui.engine.decompress_log import log_decompress

        log_ui_flush(
            "decompress.batch_start",
            task_count=len(decompress_tasks),
            export_dir=export_dir,
        )
        log_decompress("gui_batch_begin", task_count=len(decompress_tasks), export_dir=export_dir)

        from gui.ade.explorer import SilentExplorer

        success = 0
        SilentExplorer.begin_user_operation()
        try:
            for task_type, row, record in decompress_tasks:
                try:
                    log_decompress(
                        "gui_item_begin",
                        task_type=task_type,
                        name=getattr(record, "name", "?"),
                        row=row,
                    )
                    if task_type == 'folder_session':
                        record.ensure_files_loaded()
                        folder_root = P(record.path)
                        file_list = []
                        for f in record.files:
                            if f.status != CompressionStatus.DONE:
                                continue
                            blob = file_record_compression_blob(f)
                            if not blob:
                                logger.warning("[decompress] folder child skip (no data): %s", f.name)
                                continue
                            try:
                                rel = str(P(f.path).relative_to(folder_root))
                            except ValueError:
                                rel = f.name
                            file_list.append((rel, blob, f.algorithm, f.size))
                        archive_data = pack_folder_archive(record.name, file_list)
                        inner_files = unpack_folder_archive(archive_data)
                        for inner_hdr, inner_payload in inner_files:
                            output_data = self._decompress_payload(engine, inner_hdr, inner_payload)
                            rel_path = inner_hdr.original_filename or f"unnamed_{success}"
                            out_path = os.path.join(export_dir, rel_path)
                            out_parent = os.path.dirname(out_path)
                            if out_parent:
                                os.makedirs(out_parent, exist_ok=True)
                            with open(out_path, 'wb') as f:
                                f.write(output_data)
                            success += 1
                        continue

                    if task_type == 'session':
                        header = CompressedFileHeader(
                            algorithm=record.algorithm,
                            original_size=record.size,
                            compressed_size=_compressed_size(record),
                            original_filename=record.name,
                            is_folder=False,
                        )

                        if hasattr(record, 'compressed_path') and record.compressed_path:
                            original_name = header.original_filename or record.name
                            output_path = os.path.join(export_dir, original_name)
                            logger.info("[decompress] STREAMING file-to-file: %s -> %s", record.compressed_path, output_path)
                            result = engine.smart_decompress_file(record.compressed_path, output_path, record.algorithm)
                            from gui.engine.decompress_log import summarize_result

                            out_sz = os.path.getsize(output_path) if os.path.isfile(output_path) else -1
                            log_decompress(
                                "gui_file_session_verify",
                                **summarize_result(result),
                                output_path=output_path,
                                output_file_bytes=out_sz,
                                header_declared_original=header.original_size,
                            )
                            if not getattr(result, "success", True):
                                em = (getattr(result, "error_message", None) or "").strip() or "解压失败"
                                raise RuntimeError(em)
                            if (
                                header.original_size
                                and out_sz >= 0
                                and int(header.original_size) != out_sz
                            ):
                                log_decompress(
                                    "gui_size_mismatch_file",
                                    level=logging.WARNING,
                                    expected=int(header.original_size),
                                    got=out_sz,
                                    name=record.name,
                                    path=output_path,
                                )
                            success += 1
                            continue

                        payload = record.compressed_data or b''
                    else:
                        record.load_raw_data()
                        header, payload = unpack_compressed_file(record.raw_data)

                    if header.is_folder:
                        folder_out = os.path.join(export_dir, header.original_filename or f"folder_{success}")
                        inner_files = unpack_folder_archive(record.raw_data)
                        for inner_hdr, inner_payload in inner_files:
                            output_data = self._decompress_payload(engine, inner_hdr, inner_payload)
                            rel_path = inner_hdr.original_filename or f"unnamed_{success}"
                            out_path = os.path.join(folder_out, rel_path)
                            out_parent = os.path.dirname(out_path)
                            if out_parent:
                                os.makedirs(out_parent, exist_ok=True)
                            with open(out_path, 'wb') as f:
                                f.write(output_data)
                            success += 1
                        continue

                    output_data = self._decompress_payload(engine, header, payload)
                    original_name = header.original_filename or record.name
                    output_path = os.path.join(export_dir, original_name)
                    with open(output_path, 'wb') as f:
                        f.write(output_data)
                    success += 1
                    logger.info("解压成功: %s -> %s", record.name, output_path)

                except Exception as e:
                    from gui.engine.decompress_log import log_decompress as _ld

                    _ld(
                        "gui_item_error",
                        level=logging.ERROR,
                        name=getattr(record, "name", "?"),
                        err=str(e),
                    )
                    logger.error("解压失败 %s: %s", record.name, e, exc_info=True)
                    QMessageBox.warning(self, "解压失败", f"文件 {record.name} 解压失败:\n{e}")
        finally:
            SilentExplorer.end_user_operation()

        if success > 0:
            from gui.engine.decompress_log import log_decompress as _ld2

            _ld2("gui_batch_end", success_count=success, export_dir=export_dir)
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

    def _on_view_heatmap_row(self, row: int) -> None:
        logger.info("[view] heatmap from right-click, row=%d", row)
        record = self._table.get_record(row)
        logger.info("[view] record: type=%s, name=%s, status=%s, has_artifact=%s",
                     type(record).__name__, getattr(record, 'name', '?'),
                     getattr(record, 'status', '?'),
                     bool(file_record_compression_blob(record)))
        if not isinstance(record, FileRecord) or record.status != CompressionStatus.DONE:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法查看热力图")
            return
        if not file_record_compression_blob(record):
            QMessageBox.information(self, "提示", "该文件没有可用的压缩数据，无法查看热力图")
            return
        self._open_heatmap(record)

    # def _on_view_heatmap(self) -> None:
    #     logger.info("[view] heatmap from menu")
    #     record = self._get_selected_done_record()
    #     if record:
    #         self._open_heatmap(record)

    def _on_view_comparison_row(self, row: int) -> None:
        logger.info("[view] comparison from right-click, row=%d", row)
        record = self._table.get_record(row)
        if isinstance(record, FolderRecord):
            self._run_folder_comparison(record)
        elif isinstance(record, FileRecord) and record.status == CompressionStatus.DONE:
            self._run_comparison(record)
        else:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法对比")

    def _on_view_network_row(self, row: int) -> None:
        logger.info("[view] network sim from right-click, row=%d", row)
        record = self._table.get_record(row)
        if not isinstance(record, FileRecord) or record.status != CompressionStatus.DONE:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法模拟")
            return
        self._run_network_sim(record)

    def _open_heatmap(self, record: FileRecord) -> None:
        logger.info("[view] opening heatmap for %s (%d bytes, algo=%s)",
                     record.name, record.size, record.algorithm.value)
        try:
            record.load_raw_data()
            container = file_record_compression_blob(record) or b""
            inner_wcx, parse_payload = compression_blob_for_visualization(record)

            from gui.engine.token_parser import can_parse, get_parser
            from gui.models import TEXT_EXTENSIONS, SCRIPT_EXTENSIONS

            ext = Path(record.name).suffix.lower() if "." in record.name else ""
            is_text = ext in (TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"}))
            use_3hm = dpflate_format_byte(inner_wcx) == 0x33
            logger.info(
                "[heatmap] ext=%s is_text=%s can_parse=%s inner=%d parse=%d 3hm=%s",
                ext,
                is_text,
                can_parse(record.algorithm),
                len(inner_wcx),
                len(parse_payload),
                use_3hm,
            )

            if (
                use_3hm
                and record.algorithm == AlgorithmType.DPFLATE
                and not getattr(record, "is_stored", False)
            ):
                from gui.ui.dialogs.block_heatmap_dialog import BlockHeatmapDialog

                dlg = BlockHeatmapDialog(
                    raw_data=record.raw_data,
                    compressed_data=inner_wcx,
                    filename=record.name,
                    algorithm=record.algorithm.value + " (3HfMT)",
                    original_size=record.size,
                    compressed_size=_compressed_size(record),
                    time_ms=record.compression_time_ms,
                    parent=self,
                )
                dlg.exec()
                return

            if can_parse(record.algorithm) and not getattr(record, 'is_stored', False):
                parser = get_parser(record.algorithm)
                _hm_params = getattr(record, 'compression_config_snapshot', None)
                logger.info("[heatmap] parser=%s, calling parse", type(parser).__name__)
                if parser is not None:
                    if not container:
                        QMessageBox.warning(self, "热力图错误", "无法读取压缩数据（内存或磁盘 .wcx）")
                        return
                    pr = parser.parse(
                        parse_payload, record.raw_data, compression_params=_hm_params
                    )
                    logger.info("[heatmap] parse done, tokens=%d", len(pr.tokens))
                    if is_text and len(pr.tokens) > 0:
                        text = record.raw_data.decode('utf-8', errors='replace')
                        byte_to_char = []
                        char_idx = 0
                        for ch in text:
                            byte_len = len(ch.encode('utf-8'))
                            for _ in range(byte_len):
                                byte_to_char.append(char_idx)
                            char_idx += 1

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
                        raw_data=record.raw_data,
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
                raw_data=record.raw_data,
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

    def _on_webpage_heatmap(self, row: int) -> None:
        logger.info("[view] webpage heatmap from right-click, row=%d", row)
        record = self._table.get_record(row)
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

    def _on_view_decision_detail(self, row: int) -> None:
        logger.info("[view] decision detail from right-click, row=%d", row)
        record = self._table.get_record(row)
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

    def _on_folder_summary(self, row: int) -> None:
        logger.info("[view] folder summary from right-click, row=%d", row)
        record = self._table.get_record(row)
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

