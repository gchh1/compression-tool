"""Per-block compression ratio heatmap dialog."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRectF, QSize, pyqtSignal
from PyQt6.QtGui import QPainter, QColor, QFont, QPen, QBrush
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QDialog,
    QScrollArea, QGroupBox,
)

from gui.models import AlgorithmType
from gui.config.theme import ThemeManager
from gui.engine.compressor import CompressionEngine
from gui.ui.views.visualizers.token_heatmap import ratio_to_qcolor as _ratio_to_qcolor


class BlockHeatmapCanvas(QWidget):
    _block_hovered = pyqtSignal(int)

    def __init__(self, blocks: list[dict], parent=None):
        super().__init__(parent)
        self._blocks = blocks
        self._hover_idx = -1
        self._cell_size = 20
        self._gap = 2
        self.setMouseTracking(True)
        self.setMinimumSize(400, 300)
        t = ThemeManager.get()
        self._bg = QColor(t.bg_primary)
        self._tooltip_bg = QColor(t.bg_elevated)
        self._tooltip_border = QColor(t.border_dark)
        self._tooltip_text = QColor(t.text_primary)
        self._tooltip_label = QColor(t.text_secondary)

    def sizeHint(self) -> QSize:
        cols = max(1, int((self.width() - 40) / (self._cell_size + self._gap)))
        rows = (len(self._blocks) + cols - 1) // cols if cols > 0 else len(self._blocks)
        return QSize(600, max(300, rows * (self._cell_size + self._gap) + 60))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w = self.width() - 40
        cols = max(1, int(w / (self._cell_size + self._gap)))
        cw = self._cell_size + self._gap

        for i, b in enumerate(self._blocks):
            col = i % cols
            row = i // cols
            x = 20 + col * cw
            y = 30 + row * cw
            color = QColor(b["color"])
            if i == self._hover_idx:
                rect = QRectF(x - 2, y - 2, self._cell_size + 4, self._cell_size + 4)
                painter.setBrush(QBrush(color))
                pen = QPen(QColor(255, 255, 255, 180), 2)
                painter.setPen(pen)
                painter.drawRoundedRect(rect, 4, 4)
            else:
                rect = QRectF(x, y, self._cell_size, self._cell_size)
                painter.setBrush(QBrush(color))
                painter.setPen(Qt.PenStyle.NoPen)
                painter.drawRoundedRect(rect, 3, 3)

        if self._hover_idx >= 0 and self._hover_idx < len(self._blocks):
            b = self._blocks[self._hover_idx]
            mx = self.mapFromGlobal(self.cursor().pos()).x() + 16
            my = self.mapFromGlobal(self.cursor().pos()).y() + 16
            lines = [
                f"偏移: 0x{b['offset']:X}",
                f"块大小: {b['size']} B",
                f"压缩后: {b['compressed_size']} B",
                f"压缩率: {b['ratio'] * 100:.1f}%",
            ]
            tw = max(painter.fontMetrics().horizontalAdvance(l) for l in lines) + 28
            th = len(lines) * 18 + 16
            mx = min(mx, self.width() - tw - 8)
            my = min(my, self.height() - th - 8)
            tip_rect = QRectF(mx, my, tw, th)
            painter.setBrush(QBrush(self._tooltip_bg))
            painter.setPen(QPen(self._tooltip_border, 1))
            painter.drawRoundedRect(tip_rect, 6, 6)
            painter.setPen(self._tooltip_label)
            for j, line in enumerate(lines):
                painter.drawText(int(mx + 14), int(my + 16 + j * 18), line)

        painter.end()

    def mouseMoveEvent(self, event):
        pos = event.position().toPoint()
        w = self.width() - 40
        cols = max(1, int(w / (self._cell_size + self._gap)))
        cw = self._cell_size + self._gap
        col = max(0, min(cols - 1, int((pos.x() - 20) / cw)))
        row = max(0, int((pos.y() - 30) / cw))
        idx = row * cols + col
        if idx != self._hover_idx and 0 <= idx < len(self._blocks):
            self._hover_idx = idx
            self.update()
        elif idx >= len(self._blocks):
            if self._hover_idx != -1:
                self._hover_idx = -1
                self.update()

    def leaveEvent(self, event):
        self._hover_idx = -1
        self.update()


class BlockHeatmapDialog(QDialog):
    def __init__(self, raw_data: bytes, compressed_data: bytes,
                 filename: str, algorithm: str,
                 original_size: int, compressed_size: int,
                 time_ms: float, block_size: int = 256, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"压缩热力图 - {filename}")
        self.setMinimumSize(750, 550)
        t = ThemeManager.get()
        self.setStyleSheet(
            f"QDialog {{ background: {t.bg_primary}; }}\n"
            f"QLabel {{ color: {t.text_primary}; background: transparent; }}\n"
            f"QGroupBox {{ color: {t.text_primary}; font-weight: 600; border: 1px solid {t.border}; "
            f"border-radius: 8px; margin-top: 8px; padding-top: 8px; }}\n"
            f"QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 4px; }}\n"
            f"QPushButton {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 6px; padding: 6px 20px; }}\n"
            f"QPushButton:hover {{ background: {t.bg_hover}; }}\n"
        )
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(12)

        header = QLabel(f"\U0001f4ca 压缩热力图")
        header.setStyleSheet(f"font-size: 18px; font-weight: 700; color: {t.text_primary}; background: transparent;")
        layout.addWidget(header)

        meta = QLabel(
            f"文件: {filename}  |  算法: {algorithm}  |  "
            f"原始: {original_size:,} B  |  压缩后: {compressed_size:,} B  |  耗时: {time_ms:.1f} ms"
        )
        meta.setStyleSheet(f"font-size: 12px; color: {t.text_secondary}; background: transparent;")
        layout.addWidget(meta)

        from gui.engine.compressor import CompressionEngine
        from gui.ade.explorer import SilentExplorer

        algo_map = {e.value: e for e in AlgorithmType}
        algo = algo_map.get(algorithm, AlgorithmType.DEFLATE)
        engine = CompressionEngine()
        n_blocks = max(1, (len(raw_data) + block_size - 1) // block_size)
        blocks = []
        for i in range(n_blocks):
            start = i * block_size
            end = min(start + block_size, len(raw_data))
            block_raw = raw_data[start:end]
            with SilentExplorer.user_compression_priority():
                r = engine.compress(block_raw, algo)
            ratio = r.compressed_size / len(block_raw) if len(block_raw) > 0 else 1.0
            blocks.append({
                "index": i, "offset": start, "size": end - start,
                "compressed_size": r.compressed_size, "ratio": round(ratio, 4),
                "color": _ratio_to_qcolor(ratio).name(),
            })

        legend_layout = QHBoxLayout()
        legend_layout.addWidget(QLabel("高压缩"))
        grad_w = QWidget()
        grad_w.setFixedWidth(200)
        grad_w.setFixedHeight(12)
        grad_w.setStyleSheet("background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #22c55e,stop:0.5 #eab308,stop:1 #ef4444); border-radius: 6px;")
        legend_layout.addWidget(grad_w)
        legend_layout.addWidget(QLabel("低压缩"))
        legend_layout.addWidget(QLabel(f"  |  块大小: {block_size} B"))
        legend_layout.addStretch()
        layout.addLayout(legend_layout)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll.setStyleSheet(f"QScrollArea {{ border: none; background: transparent; }} "
                           f"QScrollArea > QWidget > QWidget {{ background: {t.bg_primary}; }}")
        canvas = BlockHeatmapCanvas(blocks)
        scroll.setWidget(canvas)
        layout.addWidget(scroll, stretch=1)

        stats_group = QGroupBox("统计")
        stats_layout = QHBoxLayout(stats_group)
        ratio_pct = f"{compressed_size / original_size * 100:.1f}" if original_size else "0"
        for label_text, value in [("原始大小", f"{original_size:,} B"),
                                   ("压缩后", f"{compressed_size:,} B"),
                                   ("压缩率", f"{ratio_pct}%"),
                                   ("耗时", f"{time_ms:.1f} ms")]:
            card = QWidget()
            card_l = QVBoxLayout(card)
            card_l.setContentsMargins(12, 10, 12, 10)
            val_lbl = QLabel(value)
            val_lbl.setStyleSheet(f"font-size: 22px; font-weight: 700; color: {t.accent}; background: transparent;")
            desc_lbl = QLabel(label_text)
            desc_lbl.setStyleSheet(f"font-size: 11px; color: {t.text_secondary}; background: transparent;")
            card_l.addWidget(val_lbl)
            card_l.addWidget(desc_lbl)
            stats_layout.addWidget(card)
        layout.addWidget(stats_group)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)
