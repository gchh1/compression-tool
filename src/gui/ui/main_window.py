from __future__ import annotations

import logging
import os
from pathlib import Path

from PyQt6.QtCore import Qt, QMimeData, QUrl, QThread, pyqtSignal
from PyQt6.QtGui import QAction, QDragEnterEvent, QDropEvent, QBrush, QColor
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QMessageBox, QToolBar, QWidget,
    QStatusBar, QProgressBar, QLabel, QVBoxLayout, QHBoxLayout,
    QTableWidget, QTableWidgetItem, QComboBox, QMenu, QDialog, QPushButton,
    QTabWidget, QFormLayout, QSpinBox, QGroupBox, QScrollArea, QColorDialog,
    QCheckBox,
)

logger = logging.getLogger("gui.main_window")

from gui.models import Record, FileRecord, FolderRecord, CompressionStatus, AlgorithmType, ResourceType, formatted_size, ALGORITHM_PARAMS, get_default_config, LZDP_DP_VIZ_MAX_SIZE, STREAMING_CHUNK_SIZE_KB
from gui.config.theme import ThemeManager
from gui.ui.table import FileTableWidget
from gui.ui.worker import CompressionWorker, ComparisonWorker, COMPARISON_ALGORITHMS


def _compressed_size(record: Record) -> int:
    """Get compressed size, supporting both in-memory and file-path streaming modes."""
    if hasattr(record, 'compressed_data') and record.compressed_data is not None:
        return len(record.compressed_data)
    if hasattr(record, 'compressed_path') and record.compressed_path and Path(record.compressed_path).exists():
        return Path(record.compressed_path).stat().st_size
    if hasattr(record, 'size'):
        return record.size
    return 0


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
        ("LZDP (KMP+DP)", AlgorithmType.LZDP),
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
                f"border: 1px solid {t.border}; border-radius: 4px; gridline-color: {t.border}; alternate-background-color: {t.bg_hover}; }}\n"
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
        except Exception as explorer:
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


class MinMatchWidget(QWidget):
    def __init__(self, p, current_val, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.spin = QSpinBox()
        self.spin.setMinimum(max(1, p.min_val))
        self.spin.setMaximum(p.max_val)
        self.spin.setSingleStep(p.step)
        
        self.auto_btn = QCheckBox("Auto计算")
        
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
        self._streaming_threshold_spin: QSpinBox | None = None
        self._streaming_chunk_spin: QSpinBox | None = None
        self._setup_ui()

    def _setup_ui(self) -> None:
        root = QVBoxLayout(self)
        body = QWidget()
        root.addWidget(body, 1)
        layout = QVBoxLayout(body)
        try:
            from gui.engine.compressor import CompressionEngine
            from gui.models import (
                STREAMING_THRESHOLD_MB, STREAMING_CHUNK_SIZE_KB,
            )
            from gui.config.theme import ThemeManager

            current_config = CompressionEngine.get_config()

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

                if algo in (
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

            self._streaming_threshold_spin = QSpinBox()
            self._streaming_threshold_spin.setMinimum(1)
            self._streaming_threshold_spin.setMaximum(10240)
            self._streaming_threshold_spin.setSingleStep(1)
            self._streaming_threshold_spin.setValue(int(cur_threshold))
            self._streaming_threshold_spin.setSuffix(" MB")
            self._streaming_threshold_spin.setToolTip(
                "当文件大小超过此阈值时，自动切换到流式分块压缩/解压模式\n"
                "流式模式内存占用恒定，适合处理超大文件"
            )
            stream_form.addRow("使用流式的大小阈值:", self._streaming_threshold_spin)

            self._streaming_chunk_spin = QSpinBox()
            self._streaming_chunk_spin.setMinimum(64)
            self._streaming_chunk_spin.setMaximum(64 * 1024)
            self._streaming_chunk_spin.setSingleStep(64)
            self._streaming_chunk_spin.setValue(STREAMING_CHUNK_SIZE_KB)
            self._streaming_chunk_spin.setSuffix(" KB")
            self._streaming_chunk_spin.setToolTip(
                "流式模式下每个数据块的大小\n"
                "较大的块提高压缩率，较小的块降低内存占用"
            )
            stream_form.addRow("流式分块大小:", self._streaming_chunk_spin)

            desc_label = QLabel(
                "流式模式说明：文件超过阈值时自动启用，\n"
                "采用 terminator 格式 ([4B长度][数据]...[4B 0]) 分块处理，\n"
                "内存占用与文件大小无关，仅取决于分块大小。"
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

    def _refresh_encoding_preview(self, algo: AlgorithmType) -> None:
        lbl = self._encoding_preview_labels.get(algo)
        if lbl is None:
            return
        from gui.ui.helpers import (
            format_lzdp_preview,
            format_lzss_preview,
            format_dpflate_lz_reference_preview,
        )
        try:
            if algo == AlgorithmType.LZDP:
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
            self._streaming_threshold_spin.setValue(int(CompressionEngine.get_streaming_threshold()))
        if self._streaming_chunk_spin:
            self._streaming_chunk_spin.setValue(STREAMING_CHUNK_SIZE_KB)
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

        if self._record.compressed_data:
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

        self.setStyleSheet(ThemeManager.main_window_sheet())

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
        paths, _ = QFileDialog.getOpenFileNames(self, "选择文件", "", "所有文件 (*)")
        if paths:
            logger.info("[add] adding %d files", len(paths))
            self._add_paths(paths)

    def _on_add_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if folder:
            logger.info("[add] adding folder: %s", folder)
            self._add_paths([folder])

    def _add_paths(self, paths: list[str]) -> None:
        files, dirs = self._table.add_paths(paths)
        logger.info("[add] added %d files, %d dirs from %d paths", files, dirs, len(paths))
        parts = []
        if files > 0:
            parts.append(f"{files} 个文件")
        if dirs > 0:
            parts.append(f"{dirs} 个文件夹")
        if parts:
            self._statusbar.set_status_text(f"已添加 {', '.join(parts)}")

    def _on_clear(self) -> None:
        self._table.clear_all()
        self._statusbar.set_status_text("已清空列表")
        self._update_select_button()

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
            self._add_paths(paths)

    # ========== 压缩操作 ==========
    def _on_compress(self) -> None:
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

        for row, record in tasks:
            if isinstance(record, FileRecord):
                record.status = CompressionStatus.COMPRESSING
            elif isinstance(record, FolderRecord):
                record.status = CompressionStatus.COMPRESSING
                for f in record.files:
                    f.status = CompressionStatus.COMPRESSING
            self._table.update_row(row)

        self._compress_action.setEnabled(False)
        self._cancel_compress_action.setEnabled(True)

        self._worker = CompressionWorker(tasks, self._algo_selector.current_algorithm)
        self._worker.row_started.connect(self._on_row_started)
        self._worker.finished_row.connect(self._on_row_finished)
        self._worker.error.connect(self._table.mark_error)
        self._worker.finished.connect(self._on_compression_finished)

        file_count = sum(len(r.files) if isinstance(r, FolderRecord) else 1 for r in records)
        self._statusbar.set_status_text(f"正在压缩 {file_count} 个文件...")
        self._statusbar.set_progress_value(0)
        self._worker.start()

    def _on_cancel_compress(self) -> None:
        if self._worker and self._worker.isRunning():
            logger.info("[compress] cancelling worker")
            self._worker.cancel()
            self._statusbar.set_status_text("正在取消压缩...")

    def _on_row_started(self, row: int) -> None:
        try:
            record = self._table.get_record(row)
            if record and isinstance(record, FileRecord):
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
            logger.error("[_on_row_started] CRASH row=%d: %s", row, e, exc_info=True)

    def _on_row_finished(self, row: int) -> None:
        try:
            record = self._table.get_record(row)
            logger.info("[compress] row %d finished: name=%s, status=%s, ratio=%.4f",
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
            logger.error("[_on_row_finished] CRASH row=%d: %s", row, e, exc_info=True)

    def _on_compression_finished(self) -> None:
        try:
            self._compress_action.setEnabled(True)
            self._cancel_compress_action.setEnabled(False)
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
            self._compress_action.setEnabled(True)
            self._cancel_compress_action.setEnabled(False)

    # ========== 压缩演示 ==========
    def _on_compress_demo(self, row: int) -> None:
        logger.info("[demo] START row=%d", row)
        record = self._table.get_record(row)
        if not isinstance(record, FileRecord):
            QMessageBox.information(self, "提示", "压缩演示仅支持单文件")
            return
        from gui.engine.token_parser import can_parse, get_parser
        from gui.models import TEXT_EXTENSIONS, SCRIPT_EXTENSIONS

        ext = Path(record.name).suffix.lower() if "." in record.name else ""
        is_text = ext in (TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"}))
        logger.info("[demo] name=%s algo=%s is_text=%s ext=%s", record.name, record.algorithm.value, is_text, ext)

        if can_parse(record.algorithm) and not getattr(record, 'is_stored', False):
            logger.info("[demo] can_parse=True, getting parser")
            parser = get_parser(record.algorithm)
            _demo_params = getattr(record, 'compression_config_snapshot', None)
            if parser is not None:
                logger.info("[demo] parsing compressed_data=%d raw_data=%d", len(record.compressed_data), len(record.raw_data) if record.raw_data else 0)
                pr = parser.parse(
                    record.compressed_data, record.raw_data, compression_params=_demo_params
                )
                logger.info("[demo] parse done, tokens=%d", len(pr.tokens))
                if is_text and len(pr.tokens) > 0:
                    text = record.raw_data.decode('utf-8', errors='replace')
                    byte_to_char = []
                    char_idx = 0
                    for ch in text:
                        byte_len = len(ch.encode('utf-8'))
                        for _ in range(byte_len):
                            byte_to_char.append(char_idx)
                        char_idx += 1
                    logger.info("[demo] text len=%d byte_to_char len=%d", len(text), len(byte_to_char))

                    _LZ_ALGOS = {AlgorithmType.LZSS, AlgorithmType.LZDP}
                    if record.algorithm in _LZ_ALGOS:
                        if record.algorithm == AlgorithmType.LZDP:
                            logger.info("[demo] LZDP path, getting dp_viz")
                            dp_viz = None
                            if len(record.raw_data) <= LZDP_DP_VIZ_MAX_SIZE:
                                try:
                                    from gui.engine.compressor import CompressionEngine
                                    engine = CompressionEngine()
                                    comp = engine.create_compressor_for_visualization(
                                        AlgorithmType.LZDP, _demo_params
                                    )
                                    dp_viz = comp.get_dp_visualization(list(record.raw_data), 0)
                                    logger.info("[demo] dp_viz OK, steps=%d path=%d", len(dp_viz.steps), len(dp_viz.optimal_path))
                                except Exception as e:
                                    logger.warning("[demo] get_dp_visualization failed: %s", e, exc_info=True)
                                    dp_viz = None
                            else:
                                logger.info("[demo] raw_data too large (%d) for dp_viz, skipping", len(record.raw_data))
                            logger.info("[demo] creating LZDPDPDialog")
                            from gui.ui.dialogs.lz_demo_dialog import LZDPDPDialog
                            dlg = LZDPDPDialog(text, pr.tokens, byte_to_char,
                                                 dp_viz, record.name, record.algorithm.value, parent=self)
                            logger.info("[demo] LZDPDPDialog created, calling exec")
                            dlg.exec()
                            logger.info("[demo] LZDPDPDialog closed")
                        else:
                            logger.info("[demo] LZSS path, creating LZSliderDialog")
                            from gui.ui.dialogs.lz_demo_dialog import LZSliderDialog
                            dlg = LZSliderDialog(text, pr.tokens, byte_to_char,
                                                 record.name, record.algorithm.value, parent=self)
                            logger.info("[demo] LZSliderDialog created, calling exec")
                            dlg.exec()
                            logger.info("[demo] LZSliderDialog closed")
                        return

                    _FLATE_ALGOS = {AlgorithmType.DPFLATE, AlgorithmType.DEFLATE}
                    if record.algorithm in _FLATE_ALGOS:
                        logger.info("[demo] Flate path, creating FlateDemoDialog")
                        from gui.ui.dialogs.flate_demo_dialog import FlateDemoDialog
                        huffman_trees = pr.huffman_trees if pr.huffman_trees else None
                        
                        dp_viz = None
                        if record.algorithm == AlgorithmType.DPFLATE:
                            if len(record.raw_data) <= LZDP_DP_VIZ_MAX_SIZE:
                                try:
                                    from gui.engine.compressor import CompressionEngine
                                    engine = CompressionEngine()
                                    comp = engine.create_compressor_for_visualization(
                                        AlgorithmType.DPFLATE, _demo_params
                                    )
                                    lzdp_comp = engine.create_compressor_for_visualization(
                                        AlgorithmType.LZDP, _demo_params
                                    )
                                    lzdp_comp.set_min_match(comp.get_min_match())
                                    lzdp_comp.set_match_engine(comp.get_match_engine())
                                    dp_viz = lzdp_comp.get_dp_visualization(list(record.raw_data), 0)
                                except Exception as e:
                                    logger.warning("[demo] DPFlate get_dp_visualization failed: %s", e)

                        dlg = FlateDemoDialog(text, pr.tokens, byte_to_char,
                                              record.name, record.algorithm.value,
                                              huffman_trees=huffman_trees, dp_viz=dp_viz, parent=self)
                        logger.info("[demo] FlateDemoDialog created, calling exec")
                        dlg.exec()
                        logger.info("[demo] FlateDemoDialog closed")
                        return

                    logger.info("[demo] non-LZ/non-Flate path (e.g. pure Huffman), showing info")
                    QMessageBox.information(
                        self, "提示",
                        f"算法「{record.algorithm.value}」暂不支持交互式演示。\n\n"
                        f"请使用「📊 压缩热力图」查看该算法的压缩可视化。"
                    )
                    return

        dlg = CompressDemoDialog(record, self._algo_selector.current_algorithm, parent=self)
        dlg.exec()

    def _decompress_payload(self, engine, header, payload) -> bytes:
        if header.algorithm == AlgorithmType.NONE:
            return payload
        result = engine.smart_decompress(payload, header.algorithm)
        return bytes(result.data)

    # ========== 导出操作 ==========
    def _on_export(self) -> None:
        from gui.engine.file_protocol import pack_compressed_file, make_export_filename, pack_folder_archive
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
            if isinstance(record, FileRecord) and record.status == CompressionStatus.DONE and record.compressed_data:
                done_count += 1
            elif isinstance(record, FolderRecord) and record.status == CompressionStatus.DONE and record.success_count > 0:
                done_count += 1

        if done_count == 0:
            self._statusbar.set_status_text("选中的文件中没有已完成压缩的文件")
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not export_dir:
            return

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
                logger.info("[export] FolderRecord: %d files, %d done",
                             len(record.files), record.success_count)
                file_list = []
                folder_root = P(record.path)
                for f in record.files:
                    if f.status != CompressionStatus.DONE or not f.compressed_data:
                        logger.warning("[export] SKIP file %s: status=%s has_data=%s",
                                          f.name, f.status, bool(f.compressed_data))
                        continue
                    try:
                        rel = str(P(f.path).relative_to(folder_root))
                    except ValueError:
                        rel = f.name
                    file_list.append((rel, f.compressed_data, f.algorithm, f.size))
                if not file_list:
                    continue
                archive_data = pack_folder_archive(record.name, file_list)
                export_name = make_export_filename(record.name)
                export_path = os.path.join(export_dir, export_name)
                with open(export_path, 'wb') as file:
                    file.write(archive_data)
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
                with open(export_path, 'wb') as f:
                    f.write(packed)
                exported += 1

        logger.info("[export] done: exported=%d", exported)
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

        decompress_tasks: list[tuple[str, int, Record]] = []
        for row in selected:
            record = self._table.get_record(row)
            if not record:
                continue
            if isinstance(record, FileRecord):
                if record.status == CompressionStatus.DONE and record.compressed_data:
                    decompress_tasks.append(('session', row, record))
                elif P(record.path).suffix.lower() == UNIFIED_EXTENSION:
                    try:
                        from gui.engine.file_protocol import unpack_compressed_file
                        record.load_raw_data()
                        header, _ = unpack_compressed_file(record.raw_data)
                        decompress_tasks.append(('disk', row, record))
                    except Exception:
                        pass
            elif isinstance(record, FolderRecord) and record.status == CompressionStatus.DONE and record.success_count > 0:
                decompress_tasks.append(('folder_session', row, record))

        if not decompress_tasks:
            QMessageBox.warning(self, "提示", "选中的文件中没有可解压的压缩文件\n（需为已压缩文件或 .wcx 格式）")
            return

        from gui.engine.compressor import CompressionEngine
        engine = CompressionEngine()
        if not engine.available:
            QMessageBox.critical(self, "错误", "C++ 核心引擎不可用")
            return

        export_dir = QFileDialog.getExistingDirectory(self, "选择解压输出目录")
        if not export_dir:
            return

        success = 0
        for task_type, row, record in decompress_tasks:
            try:
                if task_type == 'folder_session':
                    folder_root = P(record.path)
                    file_list = []
                    for f in record.files:
                        if f.status != CompressionStatus.DONE or not f.compressed_data:
                            continue
                        try:
                            rel = str(P(f.path).relative_to(folder_root))
                        except ValueError:
                            rel = f.name
                        file_list.append((rel, f.compressed_data, f.algorithm, f.size))
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
                logger.error("解压失败 %s: %s", record.name, e, exc_info=True)
                QMessageBox.warning(self, "解压失败", f"文件 {record.name} 解压失败:\n{e}")

        if success > 0:
            self._statusbar.set_status_text(f"已解压 {success} 个文件到 {export_dir}")

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

    def _get_selected_done_record(self) -> FileRecord | None:
        rows = self._table.selected_rows
        logger.info("[view] _get_selected_done_record called, selected_rows=%s", rows)
        if not rows:
            logger.warning("[view] no rows selected")
            QMessageBox.information(self, "提示", "请先勾选一个已压缩的文件（点击行左侧的☐）")
            return None
        record = self._table.get_record(rows[0])
        logger.info("[view] got record: type=%s, name=%s, status=%s",
                     type(record).__name__, getattr(record, 'name', '?'),
                     getattr(record, 'status', '?'))
        if not isinstance(record, FileRecord):
            logger.warning("[view] record is not FileRecord: %s", type(record).__name__)
            QMessageBox.information(self, "提示", "请选中一个文件（不支持文件夹）")
            return None
        if record.status != CompressionStatus.DONE:
            logger.warning("[view] record not done: status=%s", record.status.value)
            QMessageBox.information(self, "提示", f"该文件状态为 {record.status.value}，请先压缩")
            return None
        if not record.compressed_data:
            logger.warning("[view] record has no compressed_data")
            QMessageBox.information(self, "提示", "该文件没有压缩数据")
            return None
        logger.info("[view] valid record: name=%s, size=%d, compressed=%d, algo=%s",
                     record.name, record.size, _compressed_size(record), record.algorithm.value)
        return record

    def _on_view_heatmap_row(self, row: int) -> None:
        logger.info("[view] heatmap from right-click, row=%d", row)
        record = self._table.get_record(row)
        logger.info("[view] record: type=%s, name=%s, status=%s, has_data=%s",
                     type(record).__name__, getattr(record, 'name', '?'),
                     getattr(record, 'status', '?'),
                     bool(getattr(record, 'compressed_data', None)))
        if not isinstance(record, FileRecord) or record.status != CompressionStatus.DONE or not record.compressed_data:
            QMessageBox.information(self, "提示", "该文件尚未压缩完成，无法查看热力图")
            return
        self._open_heatmap(record)

    def _on_view_heatmap(self) -> None:
        logger.info("[view] heatmap from menu")
        record = self._get_selected_done_record()
        if record:
            self._open_heatmap(record)

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
            from gui.engine.token_parser import can_parse, get_parser
            from gui.models import TEXT_EXTENSIONS, SCRIPT_EXTENSIONS

            ext = Path(record.name).suffix.lower() if "." in record.name else ""
            is_text = ext in (TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"}))
            logger.info("[heatmap] ext=%s is_text=%s can_parse=%s", ext, is_text, can_parse(record.algorithm))

            if can_parse(record.algorithm) and not getattr(record, 'is_stored', False):
                parser = get_parser(record.algorithm)
                _hm_params = getattr(record, 'compression_config_snapshot', None)
                logger.info("[heatmap] parser=%s, calling parse", type(parser).__name__)
                if parser is not None:
                    pr = parser.parse(
                        record.compressed_data, record.raw_data, compression_params=_hm_params
                    )
                    logger.info("[heatmap] parse done, tokens=%d", len(pr.tokens))
                    if is_text:
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
                        compressed_data=record.compressed_data,
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
                compressed_data=record.compressed_data,
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

    def _on_view_comparison(self) -> None:
        logger.info("[view] comparison from menu")
        rows = self._table.selected_rows
        if not rows:
            QMessageBox.information(self, "提示", "请先勾选一个已压缩的文件（点击行左侧的☐）")
            return
        record = self._table.get_record(rows[0])
        if isinstance(record, FolderRecord):
            self._run_folder_comparison(record)
            return
        if not isinstance(record, FileRecord):
            QMessageBox.information(self, "提示", "请选中一个文件或文件夹")
            return
        if record.status != CompressionStatus.DONE:
            QMessageBox.information(self, "提示", f"该文件状态为 {record.status.value}，请先压缩")
            return
        self._run_comparison(record)

    def _start_comparison_worker(self, record: Record, title: str) -> None:
        if self._comparison_worker and self._comparison_worker.isRunning():
            QMessageBox.information(self, "提示", "已有算法对比任务正在运行")
            return

        from PyQt6.QtWidgets import QProgressDialog

        chunk_size_kb = STREAMING_CHUNK_SIZE_KB
        progress = QProgressDialog("准备算法对比...", "取消", 0, 100, self)
        progress.setWindowTitle(title)
        progress.setWindowModality(Qt.WindowModality.WindowModal)
        progress.setMinimumDuration(0)
        progress.setAutoClose(False)
        progress.setAutoReset(False)
        progress.setValue(0)

        self._comparison_progress = progress
        self._comparison_worker = ComparisonWorker(record, COMPARISON_ALGORITHMS, chunk_size_kb, self)
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

    def _on_view_network(self) -> None:
        logger.info("[view] network sim from menu")
        record = self._get_selected_done_record()
        if record:
            self._run_network_sim(record)

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
                if f.status == CompressionStatus.DONE and f.compressed_data:
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
    def __init__(self, record: FileRecord, parent=None):
        super().__init__(parent)
        self.record = record
        self.setWindowTitle(f"决策详情 - {record.name}")
        self.setMinimumSize(600, 500)
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        content = QWidget()
        form = QFormLayout(content)
        
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
        layout.addWidget(file_info_group)
        
        features_group = QGroupBox("特征分析 (20维基础向量)")
        features_form = QFormLayout(features_group)
        
        bf = getattr(self.record, 'base_features', None)
        if bf is not None:
            feature_labels = [
                ("file_size_log2", "文件大小 (log₂)", ""),
                ("magic_confidence", "魔数置信度", ""),
                ("printable_ratio", "可打印ASCII占比", "%"),
                ("shannon_entropy", "香农熵", "bits/byte"),
                ("min_entropy", "最小熵", "bits/byte"),
                ("unique_byte_ratio", "唯一字节比率", "%"),
                ("mean_byte_normalized", "字节均值 (归一化)", "/255"),
                ("std_byte_normalized", "字节标准差", "/115"),
                ("longest_run_log2", "最长连续字节 (log₂)", ""),
                ("zero_byte_ratio", "零字节占比", "%"),
                ("high_bit_ratio", "高位字节占比 (≥128)", "%"),
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
                val = getattr(bf, field_key, 0.0)
                if unit == "%":
                    text = f"{val:.4f}" if abs(val) < 10 else f"{val:.2%}"
                elif unit == "bits/byte":
                    text = f"{val:.4f}"
                else:
                    text = f"{val:.4f}"
                features_form.addRow(label + ":", QLabel(text))
        else:
            entropy_val = getattr(self.record, 'content_entropy', 0.0)
            repetition_val = getattr(self.record, 'repetition_ratio', 0.0)
            features_form.addRow("香农熵:", QLabel(f"{entropy_val:.4f}" if entropy_val > 0 else "未计算"))
            features_form.addRow("重复率:", QLabel(f"{repetition_val:.2%}" if repetition_val > 0 else "未计算"))
            features_form.addRow("基础特征:", QLabel("未提取 (请先执行压缩)"))
        layout.addWidget(features_group)
        
        decision_group = QGroupBox("ADE 决策详情")
        decision_form = QFormLayout(decision_group)
        
        decision_result = getattr(self.record, 'decision_result', None)
        if decision_result:
            algo_name = decision_result.algorithm.value if hasattr(decision_result.algorithm, 'value') else str(decision_result.algorithm)
            confidence_pct = decision_result.confidence * 100
            decision_form.addRow("推荐算法:", QLabel(algo_name))
            decision_form.addRow("置信度:", QLabel(f"{confidence_pct:.1f}%"))
            decision_form.addRow("决策原因:", QLabel(decision_result.reason or "无"))
            
            if decision_result.params:
                params_text = "\n".join([f"  - {k}: {v}" for k, v in decision_result.params.items()])
                decision_form.addRow("使用参数:", QLabel(params_text))
            else:
                decision_form.addRow("使用参数:", QLabel("默认参数"))
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
                    decision_form.addRow("决策原因:", QLabel(new_decision.reason or "无"))
                    if new_decision.params:
                        params_text = "\n".join([f"  - {k}: {v}" for k, v in new_decision.params.items()])
                        decision_form.addRow("使用参数:", QLabel(params_text))
                    else:
                        decision_form.addRow("使用参数:", QLabel("默认参数"))
                else:
                    decision_form.addRow("状态:", QLabel("ADE 未初始化或无法决策"))
            except Exception as e:
                decision_form.addRow("状态:", QLabel(f"重新分析失败: {e}"))
        
        layout.addWidget(decision_group)
        
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
            
            layout.addWidget(results_group)
        
        scroll.setWidget(content)
        layout.addWidget(scroll)
        
        btn_layout = QHBoxLayout()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addStretch()
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)

