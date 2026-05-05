"""Compression view — strategy selection, file table, action buttons."""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox, QPushButton,
    QTableWidget, QTableWidgetItem, QHeaderView, QFrame, QAbstractItemView,
)

from gui.core.models import (
    FileRecord, FolderRecord, AlgorithmType, CompressionStatus, formatted_size,
)


class CompressionView(QWidget):
    """View 2: Compression configuration and execution."""

    compress_all = pyqtSignal()
    incremental_compress = pyqtSignal()
    pack_requested = pyqtSignal()

    COL_NAME = 0
    COL_TYPE = 1
    COL_SIZE = 2
    COL_ALGO = 3
    COL_STATUS = 4
    COL_RATIO = 5

    def __init__(self, parent=None):
        super().__init__(parent)
        self._records: list = []
        self._setup_ui()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)

        # Header
        title = QLabel("压缩配置与执行")
        title.setStyleSheet("font-size: 18px; font-weight: bold; padding: 0 0 8px 0;")
        layout.addWidget(title)

        # Controls row
        controls = QHBoxLayout()

        controls.addWidget(QLabel("全局策略:"))
        self._global_algo = QComboBox()
        self._global_algo.addItem("Auto (自动)", AlgorithmType.AUTO)
        self._global_algo.addItem("Deflate (通用)", AlgorithmType.DEFLATE)
        self._global_algo.setToolTip("选择全局压缩策略，文件级可覆盖")
        controls.addWidget(self._global_algo)

        controls.addSpacing(16)

        controls.addWidget(QLabel("图片质量:"))
        self._quality = QComboBox()
        self._quality.addItem("无损", "lossless")
        self._quality.addItem("高质量 (90%)", 90)
        self._quality.addItem("中等 (75%)", 75)
        self._quality.addItem("低质量 (50%)", 50)
        controls.addWidget(self._quality)

        controls.addStretch()

        layout.addLayout(controls)

        # File table
        self._table = QTableWidget()
        self._table.setColumnCount(6)
        self._table.setHorizontalHeaderLabels(
            ["文件名", "类型", "大小", "策略", "状态", "压缩率"])
        self._table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self._table.setColumnWidth(1, 60)
        self._table.setColumnWidth(2, 90)
        self._table.setColumnWidth(3, 80)
        self._table.setColumnWidth(4, 100)
        self._table.setColumnWidth(5, 80)
        self._table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._table.setAlternatingRowColors(True)
        self._table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        layout.addWidget(self._table)

        # Action buttons
        btn_layout = QHBoxLayout()
        self._compress_btn = QPushButton("▶ 开始压缩")
        self._compress_btn.setStyleSheet(
            "QPushButton { background: #4a90d9; color: white; font-weight: bold; "
            "padding: 8px 24px; border-radius: 6px; font-size: 14px; }"
            "QPushButton:hover { background: #357abd; }"
            "QPushButton:disabled { background: #ccc; }"
        )
        self._compress_btn.clicked.connect(self.compress_all.emit)
        btn_layout.addWidget(self._compress_btn)

        self._incremental_btn = QPushButton("🔄 增量压缩")
        self._incremental_btn.setToolTip("仅压缩已修改的文件")
        self._incremental_btn.clicked.connect(self.incremental_compress.emit)
        btn_layout.addWidget(self._incremental_btn)

        self._pack_btn = QPushButton("📦 打包导出")
        self._pack_btn.setToolTip("将所有文件打包为一个 .compressed 归档")
        self._pack_btn.clicked.connect(self.pack_requested.emit)
        btn_layout.addWidget(self._pack_btn)

        btn_layout.addStretch()
        layout.addLayout(btn_layout)

    def load_records(self, records: list) -> None:
        """Populate table from records."""
        self._records = records
        all_files: list[tuple[int, FileRecord]] = []
        for rec in records:
            if isinstance(rec, FolderRecord):
                for f in rec.files:
                    all_files.append((0, f))
            else:
                all_files.append((0, rec))  # row will be set later

        self._table.setRowCount(len(all_files))
        for i, (_, f) in enumerate(all_files):
            self._table.setItem(i, self.COL_NAME, QTableWidgetItem(f.name))
            self._table.setItem(i, self.COL_TYPE, QTableWidgetItem(f.type.value))
            self._table.setItem(i, self.COL_SIZE, QTableWidgetItem(formatted_size(f.size)))

            algo_cb = QComboBox()
            algo_cb.addItem("Auto", AlgorithmType.AUTO)
            algo_cb.addItem("Deflate", AlgorithmType.DEFLATE)
            algo_cb.addItem("None", AlgorithmType.NONE)
            algo_cb.setCurrentText(f.algorithm.value.capitalize() if f.algorithm != AlgorithmType.AUTO else "Auto")
            self._table.setCellWidget(i, self.COL_ALGO, algo_cb)

            status_text = f.status.value
            self._table.setItem(i, self.COL_STATUS, QTableWidgetItem(status_text))

            ratio_text = "--"
            if f.status == CompressionStatus.DONE and f.size > 0:
                ratio_text = f"{f.compression_ratio * 100:.1f}%"
            self._table.setItem(i, self.COL_RATIO, QTableWidgetItem(ratio_text))

    def update_row(self, rec: FileRecord) -> None:
        """Update a single row after compression."""
        for i in range(self._table.rowCount()):
            item = self._table.item(i, self.COL_NAME)
            if item and item.text() == rec.name:
                status_text = rec.status.value
                self._table.setItem(i, self.COL_STATUS, QTableWidgetItem(status_text))
                ratio_text = "--"
                if rec.status == CompressionStatus.DONE and rec.size > 0:
                    ratio_text = f"{rec.compression_ratio * 100:.1f}%"
                self._table.setItem(i, self.COL_RATIO, QTableWidgetItem(ratio_text))
                break
