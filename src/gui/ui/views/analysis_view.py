"""Analysis view — block profiling, Huffman tree, compression heatmap."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRectF
from PyQt6.QtGui import QPainter, QColor, QPen, QFont
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox, QScrollArea,
    QFrame, QSplitter,
)

from gui.ui.views.visualizers.block import BlockProfilerPanel
from gui.ui.views.visualizers.huffman import HuffmanTreePanel
from gui.models import FileRecord, FolderRecord


class _HeatmapWidget(QWidget):
    """Compression ratio heatmap — row of colored rectangles per block."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._ratios: list[float] = []
        self.setMinimumHeight(60)
        self.setMaximumHeight(80)

    def set_ratios(self, ratios: list[float]) -> None:
        self._ratios = ratios
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        if not self._ratios:
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                             "无热力图数据 (选择已压缩文件)")
            painter.end()
            return

        w = self.width() - 20
        h = self.height() - 30
        n = len(self._ratios)
        bar_w = max(2, w // n - 1)
        max_r = max(self._ratios) or 1
        min_r = min(self._ratios)

        font = QFont("Sans", 9)
        painter.setFont(font)

        for i, ratio in enumerate(self._ratios):
            x = 10 + i * (w // n)
            # Normalize: 0 (best) → green, 1+ (worst) → red
            norm = ratio / max(max_r, 1)
            r = int(min(255, 255 * norm))
            g = int(max(0, 255 * (1 - norm)))
            color = QColor(r, g, 50)
            painter.fillRect(QRectF(x, 10, bar_w, h - 10), color)

        # Labels
        painter.setPen(QColor("#555"))
        painter.drawText(5, h + 15, "好 →")
        painter.drawText(w - 30, h + 15, "→ 差")

        # Tooltip hint
        painter.drawText(self.rect(), Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignRight,
                         "鼠标悬停柱状图查看详情")

        painter.end()


class AnalysisView(QWidget):
    """View 3: Per-file compression analysis."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._records: list[FileRecord] = []
        self._current_profile: dict | None = None
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)

        # Header with file selector
        header = QHBoxLayout()
        title = QLabel("压缩分析")
        title.setStyleSheet("font-size: 18px; font-weight: bold;")
        header.addWidget(title)
        header.addStretch()
        header.addWidget(QLabel("选择文件:"))
        self._file_selector = QComboBox()
        self._file_selector.setMinimumWidth(250)
        self._file_selector.currentIndexChanged.connect(self._on_file_selected)
        header.addWidget(self._file_selector)
        layout.addLayout(header)

        # Content: scrollable area
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)

        content = QWidget()
        content_layout = QVBoxLayout(content)

        # Block profiling
        self._block_panel = BlockProfilerPanel()
        content_layout.addWidget(self._block_panel)

        # Heatmap
        heatmap_label = QLabel("压缩热力图 (每 block 压缩比)")
        heatmap_label.setStyleSheet("font-weight: bold; padding: 8px 0 4px 0; font-size: 13px;")
        content_layout.addWidget(heatmap_label)
        self._heatmap = _HeatmapWidget()
        content_layout.addWidget(self._heatmap)

        # Huffman tree
        self._huffman_panel = HuffmanTreePanel()
        content_layout.addWidget(self._huffman_panel)

        content_layout.addStretch()
        scroll.setWidget(content)
        layout.addWidget(scroll)

    def set_records(self, records: list) -> None:
        """Populate file selector with compressed records."""
        self._records = records
        self._file_selector.blockSignals(True)
        self._file_selector.clear()

        all_files: list[FileRecord] = []
        for rec in records:
            if isinstance(rec, FolderRecord):
                all_files.extend(rec.files)
            else:
                all_files.append(rec)

        for f in all_files:
            if hasattr(f, 'block_profile') and f.block_profile:
                self._file_selector.addItem(f"✅ {f.name}", f)
            elif f.status.value == "done":
                self._file_selector.addItem(f"⚠ {f.name} (无 profile)", f)
            else:
                self._file_selector.addItem(f"⏳ {f.name}", f)

        self._file_selector.blockSignals(False)
        if self._file_selector.count() > 0:
            self._on_file_selected(0)

    def _on_file_selected(self, index: int) -> None:
        if index < 0:
            return
        rec = self._file_selector.itemData(index)
        if rec is None:
            return

        bp = getattr(rec, 'block_profile', None)
        if bp and bp.get('blocks'):
            self._current_profile = bp
            blocks = bp['blocks']
            self._block_panel.set_blocks(blocks)
            if blocks:
                self._huffman_panel.set_block(blocks[0])

            # Heatmap ratios
            ratios = []
            for b in blocks:
                raw_bits = max(b.get('output_bytes', 1), 1) * 8
                cb = b.get('ll_tree_bits', 0) + b.get('dist_tree_bits', 0)
                cb += b.get('literal_count', 0) * 8 + b.get('match_count', 0) * 16
                ratios.append(cb / raw_bits)
            self._heatmap.set_ratios(ratios)
        else:
            self._block_panel.set_blocks([])
            self._huffman_panel.set_block({})
            self._heatmap.set_ratios([])

    def update_from_profile(self, rec: FileRecord) -> None:
        """Update directly after compression completes."""
        self.set_records(self._records)

