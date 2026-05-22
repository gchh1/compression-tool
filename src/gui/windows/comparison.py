from __future__ import annotations

import json
import logging
import tempfile
import webbrowser
from pathlib import Path

from gui.config.theme import ThemeManager

logger = logging.getLogger(__name__)


def generate_comparison(
    results: list[dict],
    filename: str = "unknown",
    original_size: int = 0,
) -> str:
    logger.info("[comparison] generating for %s, original=%d, %d algorithms",
                 filename, original_size, len(results))
    results_json = json.dumps(results, ensure_ascii=False)

    t = ThemeManager.get()
    heat_low = ThemeManager.resolve_hex("heatmap_low")
    heat_mid = ThemeManager.resolve_hex("heatmap_mid")
    heat_high = ThemeManager.resolve_hex("heatmap_high")

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>算法对比 - {filename}</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ font-family: "Microsoft YaHei", "Segoe UI", sans-serif; background: {t.bg_primary}; color: {t.text_primary}; padding: 24px; }}
h1 {{ font-size: 20px; margin-bottom: 8px; }}
.meta {{ color: {t.text_secondary}; font-size: 13px; margin-bottom: 24px; }}
.chart {{ max-width: 700px; }}
.bar-group {{ display: flex; align-items: center; margin-bottom: 12px; }}
.bar-label {{ width: 120px; font-size: 13px; text-align: right; padding-right: 12px; color: {t.text_primary}; flex-shrink: 0; }}
.bar-track {{ flex: 1; height: 28px; background: {t.bg_surface}; border-radius: 6px; overflow: hidden; position: relative; }}
.bar-fill {{ height: 100%; border-radius: 6px; transition: width 0.6s ease; display: flex; align-items: center; padding-left: 8px; font-size: 12px; font-weight: 600; color: {t.bg_primary}; min-width: 40px; }}
.bar-time {{ width: 80px; font-size: 12px; text-align: right; color: {t.text_secondary}; flex-shrink: 0; padding-left: 8px; }}
.legend {{ display: flex; gap: 20px; margin-top: 20px; font-size: 12px; color: {t.text_secondary}; }}
.legend-item {{ display: flex; align-items: center; gap: 6px; }}
.legend-dot {{ width: 10px; height: 10px; border-radius: 50%; }}
.table-wrap {{ margin-top: 28px; }}
table {{ border-collapse: collapse; width: 100%; max-width: 700px; font-size: 13px; }}
th {{ background: {t.bg_surface}; color: {t.text_secondary}; padding: 10px 14px; text-align: left; font-weight: 500; }}
td {{ padding: 10px 14px; border-bottom: 1px solid {t.bg_surface}; }}
tr:hover td {{ background: {t.bg_surface}; }}
.highlight {{ color: {t.accent}; font-weight: 600; }}
</style>
</head>
<body>
<h1>📊 算法压缩对比</h1>
<div class="meta">文件: {filename} &nbsp;|&nbsp; 原始大小: {original_size:,} B</div>
<div class="chart" id="chart"></div>
<div class="legend">
  <div class="legend-item"><div class="legend-dot" style="background:{heat_low}"></div> 压缩率 &lt; 5%</div>
  <div class="legend-item"><div class="legend-dot" style="background:{heat_mid}"></div> 压缩率 5%-30%</div>
  <div class="legend-item"><div class="legend-dot" style="background:{heat_high}"></div> 压缩率 &gt; 30%</div>
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
  let color = '{heat_low}';
  if (r.ratio > 0.3) color = '{heat_high}';
  else if (r.ratio > 0.05) color = '{heat_mid}';

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


def open_comparison_html(html: str) -> None:
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
