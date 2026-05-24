"""Compression token heatmap dialog."""

from __future__ import annotations

import logging

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QSplitter, QWidget, QTabWidget,
    QScrollArea,
)

from gui.config.theme import ThemeManager
from gui.engine.token_parser import Token
from gui.ui.helpers import create_styled_label
from gui.ui.views.visualizers.token_heatmap import TokenHeatmapWidget, TokenInfoPanel, BitstreamWidget

logger = logging.getLogger(__name__)


class HeatmapDialog(QDialog):
    def __init__(self, text: str, tokens: list[Token], byte_to_char: list[int],
                 filename: str, algorithm: str, stats: dict,
                 huffman_trees=None, bitstream_bytes: bytes | None = None,
                 bitstream_tokens: list[Token] | None = None,
                 parent=None):
        super().__init__(parent)
        self._huffman_trees = huffman_trees
        self._bitstream_bytes = bitstream_bytes
        self._bitstream_tokens = bitstream_tokens or []
        self.setWindowTitle(f"压缩热力图 - {filename}")
        self.resize(1200, 800)
        self._setup_ui(text, tokens, byte_to_char, filename, algorithm, stats)

    def _setup_ui(self, text, tokens, byte_to_char, filename, algorithm, stats) -> None:
        try:
            self.setStyleSheet(ThemeManager.full_dialog_sheet())

            layout = QVBoxLayout(self)

            header = QLabel(f"📊 压缩热力图文件: {filename} | 算法: {algorithm}")
            header.setTextFormat(Qt.TextFormat.RichText)
            layout.addWidget(header)

            stats_layout = QHBoxLayout()
            stat_items = [
                ("原始大小", f"{stats.get('original_size', 0):,} B"),
                ("压缩后", f"{stats.get('compressed_size', 0):,} B"),
                ("压缩率", f"{stats.get('ratio', 0) * 100:.1f}%"),
                ("Token数", f"{stats.get('token_count', 0)}"),
                ("匹配数", f"{stats.get('match_count', 0)}"),
                ("耗时", f"{stats.get('time_ms', 0):.1f}ms"),
            ]
            for label, value in stat_items:
                w = QWidget()
                vl = QVBoxLayout(w)
                vl.setContentsMargins(8, 4, 8, 4)
                v = create_styled_label(value, bold=True)
                v.setTextFormat(Qt.TextFormat.RichText)
                v.setAlignment(Qt.AlignmentFlag.AlignCenter)
                l = create_styled_label(label, 'text_secondary', 11)
                l.setTextFormat(Qt.TextFormat.RichText)
                l.setAlignment(Qt.AlignmentFlag.AlignCenter)
                vl.addWidget(v)
                vl.addWidget(l)
                w.setStyleSheet(f"background: {ThemeManager.hex('bg_surface')}; border-radius: 8px;")
                stats_layout.addWidget(w)
            layout.addLayout(stats_layout)

            legend = QLabel(
                ""
                "压缩率: "
                "● 低  → "
                "● 中  → "
                "● 高   "
                "| 悬停查看详情"
            )
            legend.setTextFormat(Qt.TextFormat.RichText)
            layout.addWidget(legend)

            has_bitstream = self._bitstream_bytes is not None and len(self._bitstream_bytes) > 0

            if has_bitstream:
                tabs = QTabWidget()
                lz_container = QWidget()
                lz_lo = QVBoxLayout(lz_container)
                lz_lo.setContentsMargins(0, 0, 0, 0)

            splitter = QSplitter(Qt.Orientation.Horizontal)
            self._heatmap = TokenHeatmapWidget()
            self._heatmap.set_data(text, tokens, byte_to_char)
            splitter.addWidget(self._heatmap)
            self._info = TokenInfoPanel()
            self._info.setMinimumWidth(200)
            self._info.setMaximumWidth(260)
            splitter.addWidget(self._info)
            splitter.setSizes([900, 260])
            self._heatmap.token_hovered.connect(self._on_token_hovered)

            if has_bitstream:
                lz_lo.addWidget(splitter)
                tabs.addTab(lz_container, "第一层: LZ 编码")

                bs_container = QWidget()
                bs_lo = QVBoxLayout(bs_container)
                bs_lo.setContentsMargins(0, 0, 0, 0)
                bs_label = QLabel("Huffman 编码比特流（每8位=1字节，空格分隔）")
                bs_label.setTextFormat(Qt.TextFormat.RichText)
                bs_lo.addWidget(bs_label)
                bs_scroll = QScrollArea()
                bs_scroll.setWidgetResizable(True)
                self._bitstream_widget = BitstreamWidget()
                self._bitstream_widget.set_data(self._bitstream_bytes, self._bitstream_tokens)
                bs_scroll.setWidget(self._bitstream_widget)
                bs_lo.addWidget(bs_scroll)
                tabs.addTab(bs_container, "第二层: Huffman 比特流")

                layout.addWidget(tabs, stretch=1)
            else:
                layout.addWidget(splitter, stretch=1)

            btn_layout = QHBoxLayout()
            btn_layout.addStretch()
            close_btn = QPushButton("关闭")
            close_btn.clicked.connect(self.accept)
            btn_layout.addWidget(close_btn)
            layout.addLayout(btn_layout)

        except Exception as e:
            logger.error("[HeatmapDialog] _setup_ui failed: %s", e, exc_info=True)
            error_layout = QVBoxLayout(self)
            error_label = QLabel(f"初始化热力图对话框失败:\n{e}")
            error_label.setStyleSheet("color: red; padding: 20px;")
            error_layout.addWidget(error_label)

    def _on_token_hovered(self, idx: int) -> None:
        if 0 <= idx < len(self._heatmap._tokens):
            self._info.show_token(self._heatmap._tokens[idx], idx)
        else:
            self._info.show_token(None)
