from __future__ import annotations

import logging

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QWidget,
    QTableWidget, QTableWidgetItem, QHeaderView,
)

from gui.config.theme import ThemeManager

logger = logging.getLogger(__name__)


def _bar_color(ratio: float) -> str:
    t = max(0.0, min(1.0, ratio))
    r = int(0x22 + t * (0xef - 0x22))
    g = int(0xc5 * (1.0 - t) + 0x44 * t)
    b = int(0x5e * (1.0 - t) + 0x44 * t)
    return f"#{r:02x}{g:02x}{b:02x}"


class ComparisonDialog(QDialog):
    def __init__(self, results: list[dict], filename: str, original_size: int, parent=None):
        super().__init__(parent)
        self._results = results
        self._filename = filename
        self._original_size = original_size
        self.setWindowTitle(f"算法对比 - {filename}")
        self.setMinimumWidth(640)
        self.setMinimumHeight(400)
        self.resize(720, 520)
        self._setup_ui()

    def _setup_ui(self) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(12)

        header = QLabel(f"算法压缩对比")
        header_font = QFont()
        header_font.setPointSize(16)
        header_font.setBold(True)
        header.setFont(header_font)
        layout.addWidget(header)

        meta = QLabel(f"文件: {self._filename}  |  原始大小: {self._original_size:,} B")
        meta.setStyleSheet("color: #94a3b8; font-size: 13px;")
        layout.addWidget(meta)

        layout.addSpacing(8)

        chart_widget = QWidget()
        chart_layout = QVBoxLayout(chart_widget)
        chart_layout.setContentsMargins(0, 0, 0, 0)
        chart_layout.setSpacing(8)

        for idx, r in enumerate(self._results):
            row = QWidget()
            row_layout = QHBoxLayout(row)
            row_layout.setContentsMargins(0, 0, 0, 0)
            row_layout.setSpacing(8)

            label = QLabel(r.get("name", f"Algo {idx}"))
            label.setFixedWidth(130)
            label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            label.setStyleSheet("color: #cbd5e1; font-size: 13px;")
            row_layout.addWidget(label)

            pct = min(100.0, max(0.0, r.get("ratio", 0) * 100))
            color = _bar_color(r.get("ratio", 0))

            track = QWidget()
            track.setFixedHeight(26)
            track.setStyleSheet(f"""
                QWidget {{
                    background: #1e293b;
                    border-radius: 6px;
                }}
            """)
            track_layout = QHBoxLayout(track)
            track_layout.setContentsMargins(0, 0, 0, 0)

            fill = QLabel(f" {pct:.1f}%")
            fill.setStyleSheet(f"""
                QLabel {{
                    background: {color};
                    color: #0f172a;
                    font-size: 12px;
                    font-weight: bold;
                    border-radius: 6px;
                    padding: 2px 0 0 8px;
                }}
            """)
            fill.setFixedHeight(26)
            fill_stretch = max(1, int(pct))
            empty_stretch = max(1, int(100 - pct))
            track_layout.addWidget(fill, fill_stretch)
            track_layout.addStretch(empty_stretch)

            row_layout.addWidget(track, 1)

            time_label = QLabel(f"{r.get('time_ms', 0):.1f} ms")
            time_label.setFixedWidth(72)
            time_label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            time_label.setStyleSheet("color: #94a3b8; font-size: 12px;")
            row_layout.addWidget(time_label)

            chart_layout.addWidget(row)

        layout.addWidget(chart_widget)

        legend = QWidget()
        legend_layout = QHBoxLayout(legend)
        legend_layout.setContentsMargins(0, 0, 0, 0)
        legend_layout.setSpacing(16)
        for color, text in [("#22c55e", "0%"), ("#eab308", "50%"), ("#ef4444", "100%")]:
            item = QWidget()
            item_layout = QHBoxLayout(item)
            item_layout.setContentsMargins(0, 0, 0, 0)
            item_layout.setSpacing(4)
            dot = QLabel("●")
            dot.setStyleSheet(f"color: {color}; font-size: 14px;")
            item_layout.addWidget(dot)
            txt = QLabel(text)
            txt.setStyleSheet("color: #94a3b8; font-size: 12px;")
            item_layout.addWidget(txt)
            legend_layout.addWidget(item)
        legend_layout.addStretch()
        layout.addWidget(legend)

        layout.addSpacing(8)

        table = QTableWidget(len(self._results), 5)
        table.setHorizontalHeaderLabels(["算法", "压缩后", "压缩率", "耗时", "节省"])
        table.verticalHeader().setVisible(False)
        table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        table.horizontalHeader().setStretchLastSection(True)
        table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        table.setStyleSheet(ThemeManager.table_sheet())

        for row_idx, r in enumerate(self._results):
            algo_item = QTableWidgetItem(r.get("name", f"Algo {row_idx}"))
            table.setItem(row_idx, 0, algo_item)

            comp_item = QTableWidgetItem(f"{r.get('compressed_size', 0):,} B")
            comp_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            table.setItem(row_idx, 1, comp_item)

            ratio_item = QTableWidgetItem(f"{r.get('ratio', 0) * 100:.2f}%")
            ratio_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            table.setItem(row_idx, 2, ratio_item)

            time_item = QTableWidgetItem(f"{r.get('time_ms', 0):.1f} ms")
            time_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            table.setItem(row_idx, 3, time_item)

            saving = self._original_size - r.get('compressed_size', 0)
            save_text = f"-{saving:,} B" if saving > 0 else "+"
            save_item = QTableWidgetItem(save_text)
            save_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            save_item.setForeground(QColor("#38bdf8"))
            font = save_item.font()
            font.setBold(True)
            save_item.setFont(font)
            table.setItem(row_idx, 4, save_item)

        layout.addWidget(table)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        close_btn.setStyleSheet(ThemeManager.button_sheet())
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


def open_comparison_html(html: str) -> None:
    import tempfile
    import webbrowser
    from pathlib import Path

    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".html",
        delete=False,
        encoding="utf-8",
    ) as tmp_f:
        tmp_f.write(html)
        tmp_path = tmp_f.name
    tmp = Path(tmp_path)
    logger.info("[comparison] wrote HTML to %s, opening in browser", tmp)
    webbrowser.open(tmp.as_uri())


def generate_comparison(
    results: list[dict],
    filename: str = "unknown",
    original_size: int = 0,
) -> str:
    import json
    logger.info("[comparison] generating for %s, original=%d, %d algorithms",
                 filename, original_size, len(results))
    results_json = json.dumps(results, ensure_ascii=False)

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>算法对比 - {filename}</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ font-family: "Microsoft YaHei", "Segoe UI", sans-serif; background: #0f172a; color: #e2e8f0; padding: 24px; }}
h1 {{ font-size: 20px; margin-bottom: 8px; }}
.meta {{ color: #94a3b8; font-size: 13px; margin-bottom: 24px; }}
.chart {{ max-width: 700px; }}
.bar-group {{ display: flex; align-items: center; margin-bottom: 12px; }}
.bar-label {{ width: 120px; font-size: 13px; text-align: right; padding-right: 12px; color: #cbd5e1; flex-shrink: 0; }}
.bar-track {{ flex: 1; height: 28px; background: #1e293b; border-radius: 6px; overflow: hidden; position: relative; }}
.bar-fill {{ height: 100%; border-radius: 6px; transition: width 0.6s ease; display: flex; align-items: center; padding-left: 8px; font-size: 12px; font-weight: 600; color: #0f172a; min-width: 40px; }}
.bar-time {{ width: 80px; font-size: 12px; text-align: right; color: #94a3b8; flex-shrink: 0; padding-left: 8px; }}
.legend {{ display: flex; gap: 20px; margin-top: 20px; font-size: 12px; color: #94a3b8; }}
.legend-item {{ display: flex; align-items: center; gap: 6px; }}
.legend-dot {{ width: 10px; height: 10px; border-radius: 50%; }}
.table-wrap {{ margin-top: 28px; }}
table {{ border-collapse: collapse; width: 100%; max-width: 700px; font-size: 13px; }}
th {{ background: #1e293b; color: #94a3b8; padding: 10px 14px; text-align: left; font-weight: 500; }}
td {{ padding: 10px 14px; border-bottom: 1px solid #1e293b; }}
tr:hover td {{ background: #1e293b; }}
.highlight {{ color: #38bdf8; font-weight: 600; }}
</style>
</head>
<body>
<h1>📊 算法压缩对比</h1>
<div class="meta">文件: {filename} &nbsp;|&nbsp; 原始大小: {original_size:,} B</div>
<div class="chart" id="chart"></div>
<div class="legend">
  <div class="legend-item"><div class="legend-dot" style="background:#22c55e"></div> 压缩率 &lt; 5%</div>
  <div class="legend-item"><div class="legend-dot" style="background:#eab308"></div> 压缩率 5%-30%</div>
  <div class="legend-item"><div class="legend-dot" style="background:#ef4444"></div> 压缩率 &gt; 30%</div>
</div>
<div class="table-wrap">
  <table>
    <thead><tr><th>算法</th><th>压缩后</th><th>压缩率</th><th>耗时</th><th>节省</th></tr></thead>
    <tbody id="tbody"></tbody>
  </table>
</div>
<script>
const results = {results_json};
const originalSize = {original_size};
const chart = document.getElementById('chart');
const tbody = document.getElementById('tbody');

results.forEach(r => {{
  const pct = Math.min(100, Math.max(0, r.ratio * 100));
  let color = '#22c55e';
  if (r.ratio > 0.3) color = '#ef4444';
  else if (r.ratio > 0.05) color = '#eab308';

  const group = document.createElement('div');
  group.className = 'bar-group';
  group.innerHTML =
    '<div class="bar-label">' + r.name + '</div>' +
    '<div class="bar-track"><div class="bar-fill" style="width:' + pct + '%;background:' + color + '">' + (r.ratio * 100).toFixed(1) + '%</div></div>' +
    '<div class="bar-time">' + r.time_ms.toFixed(1) + ' ms</div>';
  chart.appendChild(group);

  const saving = originalSize - r.compressed_size;
  const tr = document.createElement('tr');
  tr.innerHTML =
    '<td>' + r.name + '</td>' +
    '<td>' + r.compressed_size.toLocaleString() + ' B</td>' +
    '<td>' + (r.ratio * 100).toFixed(2) + '%</td>' +
    '<td>' + r.time_ms.toFixed(1) + ' ms</td>' +
    '<td class="highlight">' + (saving > 0 ? '-' + saving.toLocaleString() + ' B' : '+') + '</td>';
  tbody.appendChild(tr);
}});
</script>
</body>
</html>"""
    return html