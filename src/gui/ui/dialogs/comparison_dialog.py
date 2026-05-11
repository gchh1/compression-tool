"""Multi-algorithm compression comparison dialog."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QGroupBox,
    QProgressBar, QTableWidget, QTableWidgetItem, QAbstractItemView,
)

from gui.config.theme import ThemeManager
from gui.ui.views.visualizers.token_heatmap import ratio_to_qcolor as _ratio_to_qcolor


class ComparisonDialog(QDialog):
    def __init__(self, results: list[dict], filename: str = "unknown",
                 original_size: int = 0, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"算法压缩对比 - {filename}")
        self.resize(800, 600)
        self._setup_ui(results, filename, original_size)

    def _setup_ui(self, results: list[dict], filename: str, original_size: int) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)

        header = QLabel(
            f"📊 算法压缩对比"
            f"文件: {filename}  |  原始大小: {original_size:,} B"
        )
        header.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(header)

        legend = QLabel(
            f""
            "压缩率: "
            f"● 低(好)  → "
            f"● 中  → "
            f"● 高(差)   "
            f"(色彩连续渐变)"
        )
        legend.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(legend)

        bar_group = QGroupBox("压缩率对比")
        bar_layout = QVBoxLayout(bar_group)

        max_ratio = max((r["ratio"] for r in results), default=0.01)
        for r in results:
            row = QHBoxLayout()
            label = QLabel(r["name"])
            label.setFixedWidth(100)
            label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 13px;")
            row.addWidget(label)
            bar = QProgressBar()
            bar.setMinimum(0)
            bar.setMaximum(1000)
            pct = int((r["ratio"] / max_ratio) * 1000) if max_ratio > 0 else 0
            bar.setValue(pct)
            bar.setFormat(f"{r['ratio'] * 100:.1f}%")

            c = _ratio_to_qcolor(r["ratio"])
            bar.setStyleSheet(
                f"QProgressBar {{ border: 1px solid {ThemeManager.hex('border_dark')}; border-radius: 6px; background: {ThemeManager.hex('bg_surface')}; text-align: center; color: {ThemeManager.hex('bg_primary')}; font-weight: 600; font-size: 12px; min-height: 24px; }}"
                f"QProgressBar::chunk {{ border-radius: 5px; background: {c.name()}; }}"
            )
            row.addWidget(bar, stretch=1)

            time_label = QLabel(f"{r['time_ms']:.1f} ms")
            time_label.setFixedWidth(80)
            time_label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            time_label.setStyleSheet(f"color: {ThemeManager.hex('text_secondary')}; font-size: 12px;")
            row.addWidget(time_label)

            bar_layout.addLayout(row)

        layout.addWidget(bar_group)

        table_group = QGroupBox("详细数据")
        table_layout = QVBoxLayout(table_group)
        table = QTableWidget()
        table.setColumnCount(5)
        table.setHorizontalHeaderLabels(["算法", "压缩后", "压缩率", "耗时", "节省"])
        table.horizontalHeader().setStretchLastSection(True)
        table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        table.setAlternatingRowColors(True)
        table.setStyleSheet(table.styleSheet() + f"QTableWidget {{ alternate-background-color: {ThemeManager.hex('bg_hover')}; }}")
        table.setRowCount(len(results))

        best_ratio = min(r["ratio"] for r in results)
        for i, r in enumerate(results):
            name_item = QTableWidgetItem(r["name"])
            name_item.setForeground(ThemeManager.color('text_primary'))
            table.setItem(i, 0, name_item)

            size_item = QTableWidgetItem(f"{r['compressed_size']:,} B")
            size_item.setForeground(ThemeManager.color('text_primary'))
            table.setItem(i, 1, size_item)

            ratio_item = QTableWidgetItem(f"{r['ratio'] * 100:.2f}%")
            ratio_item.setForeground(ThemeManager.color('text_secondary') if r[f"ratio"] == best_ratio else ThemeManager.color('text_primary'))
            table.setItem(i, 2, ratio_item)

            time_item = QTableWidgetItem(f"{r['time_ms']:.1f} ms")
            time_item.setForeground(ThemeManager.color('text_primary'))
            table.setItem(i, 3, time_item)

            saving = original_size - r["compressed_size"]
            saving_text = f"-{saving:,} B" if saving > 0 else "+"
            saving_item = QTableWidgetItem(saving_text)
            saving_item.setForeground(ThemeManager.color('text_secondary'))
            table.setItem(i, 4, saving_item)

        table.resizeColumnsToContents()
        table_layout.addWidget(table)
        layout.addWidget(table_group, stretch=1)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.setStyleSheet(f"background:{ThemeManager.hex('border_dark')}; color:{ThemeManager.hex('text_primary')}; padding:6px 20px; border-radius:6px;")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)
