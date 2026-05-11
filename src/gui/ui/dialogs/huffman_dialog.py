"""Standalone Huffman tree + frequency + code table dialog."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QSplitter,
    QScrollArea, QGroupBox, QTableWidget, QTableWidgetItem, QAbstractItemView,
    QComboBox, QWidget,
)

from gui.config.theme import ThemeManager
from gui.ui.views.visualizers.huffman import (
    HuffmanTreeCanvas,
    HuffmanFreqChart,
    code_length_to_color,
)


class HuffmanTreeDialog(QDialog):
    def __init__(self, huffman_trees, filename: str = "", parent=None):
        super().__init__(parent)
        self._trees = huffman_trees
        self._current_idx = 0
        self.setWindowTitle(f"Huffman 可视化 - {filename}")
        self.resize(1200, 800)
        self._setup_ui()

    def _setup_ui(self):
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)

        header = QLabel("🌳 Huffman 编码可视化")
        header.setTextFormat(Qt.TextFormat.RichText)
        layout.addWidget(header)

        if len(self._trees) > 1:
            tree_select_layout = QHBoxLayout()
            tree_select_layout.addWidget(QLabel("选择 Huffman 树:"))

            self._tree_combo = QComboBox()
            for i, td in enumerate(self._trees):
                self._tree_combo.addItem(f"#{i + 1} {td.tree_type}")
            self._tree_combo.currentIndexChanged.connect(self._on_tree_changed)
            tree_select_layout.addWidget(self._tree_combo)
            tree_select_layout.addStretch()
            layout.addLayout(tree_select_layout)

        splitter = QSplitter(Qt.Orientation.Horizontal)

        tree_group = QGroupBox("Huffman 树")
        tree_layout = QVBoxLayout(tree_group)
        self._tree_scroll = QScrollArea()
        self._tree_scroll.setWidgetResizable(True)
        self._tree_canvas = HuffmanTreeCanvas(self._trees[0])
        self._tree_scroll.setWidget(self._tree_canvas)
        tree_layout.addWidget(self._tree_scroll)
        splitter.addWidget(tree_group)

        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        right_layout.setContentsMargins(0, 0, 0, 0)

        freq_group = QGroupBox("频率分布")
        self._freq_layout = QVBoxLayout(freq_group)
        self._freq_chart = HuffmanFreqChart(self._trees[0].codes)
        self._freq_layout.addWidget(self._freq_chart)
        right_layout.addWidget(freq_group, stretch=1)

        code_group = QGroupBox("编码表")
        code_layout = QVBoxLayout(code_group)
        self._code_table = QTableWidget()
        self._code_table.setColumnCount(4)
        self._code_table.setHorizontalHeaderLabels(["符号", "频率", "编码", "码长"])
        self._code_table.horizontalHeader().setStretchLastSection(True)
        self._code_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._code_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._code_table.setAlternatingRowColors(True)
        self._code_table.setStyleSheet(
            self._code_table.styleSheet() + f"QTableWidget {{ alternate-background-color: {ThemeManager.hex('bg_hover')}; }}")
        self._populate_code_table(self._trees[0].codes)
        code_layout.addWidget(self._code_table)
        right_layout.addWidget(code_group, stretch=1)

        splitter.addWidget(right_widget)
        splitter.setSizes([600, 600])
        layout.addWidget(splitter, stretch=1)

        stats_layout = QHBoxLayout()
        self._stats_label = QLabel()
        self._update_stats(self._trees[0])
        stats_layout.addWidget(self._stats_label)
        stats_layout.addStretch()

        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        stats_layout.addWidget(close_btn)
        layout.addLayout(stats_layout)

    def _on_tree_changed(self, idx):
        if 0 <= idx < len(self._trees):
            self._current_idx = idx
            td = self._trees[idx]
            self._tree_canvas = HuffmanTreeCanvas(td)
            self._tree_scroll.setWidget(self._tree_canvas)
            old_chart = self._freq_chart
            self._freq_chart = HuffmanFreqChart(td.codes)
            self._freq_layout.removeWidget(old_chart)
            old_chart.deleteLater()
            self._freq_layout.insertWidget(0, self._freq_chart)
            self._populate_code_table(td.codes)
            self._update_stats(td)

    def _populate_code_table(self, codes):
        self._code_table.setRowCount(len(codes))
        for i, entry in enumerate(codes):
            sym_text = self._format_symbol(entry.symbol)
            sym_item = QTableWidgetItem(sym_text)
            sym_item.setForeground(ThemeManager.color('text_primary'))
            self._code_table.setItem(i, 0, sym_item)

            freq_item = QTableWidgetItem(str(entry.frequency))
            freq_item.setForeground(ThemeManager.color('text_primary'))
            self._code_table.setItem(i, 1, freq_item)

            code_item = QTableWidgetItem(entry.code)
            code_item.setForeground(ThemeManager.color('text_secondary'))
            code_item.setFont(QFont("Consolas", 9))
            self._code_table.setItem(i, 2, code_item)

            len_item = QTableWidgetItem(str(entry.code_length))
            len_item.setForeground(code_length_to_color(entry.code_length))
            self._code_table.setItem(i, 3, len_item)

        self._code_table.resizeColumnsToContents()

    def _format_symbol(self, symbol: int) -> str:
        if symbol < 256:
            if 32 <= symbol < 127:
                return f"'{chr(symbol)}' ({symbol})"
            return str(symbol)
        if symbol == 256:
            return "EOF (256)"
        if 257 <= symbol <= 285:
            return f"LEN_{symbol - 257} ({symbol})"
        return str(symbol)

    def _update_stats(self, td):
        n_symbols = len(td.codes)
        avg_len = sum(e.code_length for e in td.codes) / n_symbols if n_symbols else 0
        total_freq = sum(e.frequency for e in td.codes)
        self._stats_label.setText(
            f"符号数: {n_symbols}  |  "
            f"平均码长: {avg_len:.2f} bit  |  "
            f"总频率: {total_freq}  |  "
            f"树类型: {td.tree_type}  |  "
            f"树序列化: {td.total_bits} bit"
        )
        self._stats_label.setTextFormat(Qt.TextFormat.RichText)
