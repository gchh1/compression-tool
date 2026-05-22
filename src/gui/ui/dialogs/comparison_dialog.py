"""Multi-algorithm compression comparison dialog."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QDialog,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QGroupBox,
    QProgressBar,
    QTableWidget,
    QTableWidgetItem,
    QAbstractItemView,
    QHeaderView,
)

from gui.config.theme import ThemeManager
from gui.ui.views.visualizers.token_heatmap import ratio_to_qcolor as _ratio_to_qcolor


def _apply_comparison_table_layout(table: QTableWidget) -> None:
    """列宽：第 0 列占剩余；1–3 固定；最后一列随 stretchLastSection 拉伸。"""
    hdr = table.horizontalHeader()
    hdr.setStretchLastSection(True)
    for col in (1, 2, 3):
        hdr.setSectionResizeMode(col, QHeaderView.ResizeMode.Fixed)
    hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    table.setColumnWidth(1, 130)
    table.setColumnWidth(2, 100)
    table.setColumnWidth(3, 110)


class ComparisonDialog(QDialog):
    def __init__(
        self,
        results: list[dict],
        filename: str = "unknown",
        original_size: int = 0,
        parent=None,
    ):
        super().__init__(parent)
        self.setWindowTitle(f"算法压缩对比 - {filename}")
        self.resize(800, 600)
        self._setup_ui(results, filename, original_size)

    def _setup_ui(self, results: list[dict], filename: str, original_size: int) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)

        header = QLabel(
            f"算法压缩对比\n文件: {filename}  |  原始大小: {original_size:,} B"
        )
        header.setWordWrap(True)
        layout.addWidget(header)

        legend = QLabel(
            "柱状长度 = 压缩后/原始大小（绝对值，满条为 100%；超过 100% 表示膨胀仍显示满条）。"
            "颜色与热力图一致，按同一压缩率映射红黄绿渐变。"
        )
        legend.setWordWrap(True)
        legend.setStyleSheet(f"color: {ThemeManager.hex('text_muted')}; font-size: 12px;")
        layout.addWidget(legend)

        bar_group = QGroupBox("压缩率对比")
        bar_layout = QVBoxLayout(bar_group)

        for r in results:
            row = QHBoxLayout()
            label = QLabel(r["name"])
            label.setFixedWidth(100)
            label.setStyleSheet(
                f"color: {ThemeManager.hex('text_secondary')}; font-size: 13px;"
            )
            row.addWidget(label)
            bar = QProgressBar()
            bar.setMinimum(0)
            bar.setMaximum(1000)
            # 按「压缩后/原始」绝对比例占轨宽度，便于算法间横向对比（与颜色映射一致）。
            pct = int(min(1000, max(0, r["ratio"] * 1000)))
            bar.setValue(pct)
            bar.setFormat(f"{r['ratio'] * 100:.1f}%")

            c = _ratio_to_qcolor(r["ratio"])
            bar.setStyleSheet(
                f"QProgressBar {{ border: 1px solid {ThemeManager.hex('border_dark')}; "
                f"border-radius: 6px; background: {ThemeManager.hex('bg_surface')}; "
                f"text-align: center; color: {ThemeManager.hex('bg_primary')}; "
                f"font-weight: 600; font-size: 12px; min-height: 24px; }}"
                f"QProgressBar::chunk {{ border-radius: 5px; background: {c.name()}; }}"
            )
            row.addWidget(bar, stretch=1)

            time_label = QLabel(f"{r['time_ms']:.1f} ms")
            time_label.setFixedWidth(80)
            time_label.setAlignment(
                Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter
            )
            time_label.setStyleSheet(
                f"color: {ThemeManager.hex('text_secondary')}; font-size: 12px;"
            )
            row.addWidget(time_label)

            bar_layout.addLayout(row)

        layout.addWidget(bar_group)

        table_group = QGroupBox("详细数据")
        table_layout = QVBoxLayout(table_group)
        table = QTableWidget()
        table.setColumnCount(5)
        table.setHorizontalHeaderLabels(["算法", "压缩后", "压缩率", "耗时", "节省"])
        table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        table.setAlternatingRowColors(True)
        table.setStyleSheet(
            table.styleSheet()
            + f"QTableWidget {{ alternate-background-color: {ThemeManager.hex('bg_hover')}; }}"
        )
        table.setRowCount(len(results))

        best_ratio = min(r["ratio"] for r in results)
        for i, r in enumerate(results):
            name_item = QTableWidgetItem(r["name"])
            name_item.setForeground(ThemeManager.color("text_primary"))
            table.setItem(i, 0, name_item)

            size_item = QTableWidgetItem(f"{r['compressed_size']:,} B")
            size_item.setForeground(ThemeManager.color("text_primary"))
            table.setItem(i, 1, size_item)

            ratio_item = QTableWidgetItem(f"{r['ratio'] * 100:.2f}%")
            ratio_item.setForeground(
                ThemeManager.color("text_secondary")
                if r["ratio"] == best_ratio
                else ThemeManager.color("text_primary")
            )
            table.setItem(i, 2, ratio_item)

            time_item = QTableWidgetItem(f"{r['time_ms']:.1f} ms")
            time_item.setForeground(ThemeManager.color("text_primary"))
            table.setItem(i, 3, time_item)

            saving = original_size - r["compressed_size"]
            saving_text = f"-{saving:,} B" if saving > 0 else "+"
            saving_item = QTableWidgetItem(saving_text)
            saving_item.setForeground(ThemeManager.color("text_secondary"))
            table.setItem(i, 4, saving_item)

        _apply_comparison_table_layout(table)
        table_layout.addWidget(table)
        layout.addWidget(table_group, stretch=1)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.setStyleSheet(
            f"background:{ThemeManager.hex('border_dark')}; "
            f"color:{ThemeManager.hex('text_primary')}; "
            f"padding:6px 20px; border-radius:6px;"
        )
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)
