from __future__ import annotations

import json
import logging
import tempfile
import webbrowser
from pathlib import Path

logger = logging.getLogger(__name__)


def generate_comparison(
    results: list[dict],
    filename: str = "unknown",
    original_size: int = 0,
) -> str:
    logger.info("[comparison] generating for %s, original=%d, %d algorithms",
                 filename, original_size, len(results))
    results_json = json.dumps(results, ensure_ascii=False)

        from gui.core.theme import ThemeManager
t = ThemeManager.get()
    _cmp_extra = (
    f".chart {{ max-width: 700px; }}\n"
    f".bar-group {{ display: flex; align-items: center; margin-bottom: 12px; }}\n"
    f".bar-label {{ width: 120px; font-size: 13px; text-align: right; padding-right: 12px; color: {t.text_secondary}; flex-shrink: 0; }}\n"
    f".bar-track {{ flex: 1; height: 28px; background: {t.bg_surface}; border-radius: 6px; overflow: hidden; position: relative; }}\n"
    f".bar-fill {{ height: 100%; border-radius: 6px; transition: width 0.6s ease; display: flex; align-items: center; padding-left: 8px; font-size: 12px; font-weight: 600; color: {t.text_primary}; min-width: 40px; }}\n"
    f".bar-time {{ width: 80px; font-size: 12px; text-align: right; color: {t.text_secondary}; flex-shrink: 0; padding-left: 8px; }}\n"
    f".legend {{ display: flex; gap: 20px; margin-top: 20px; font-size: 12px; color: {t.text_secondary}; }}\n"
    f".legend-item {{ display: flex; align-items: center; gap: 6px; }}\n"
    f".legend-dot {{ width: 10px; height: 10px; border-radius: 50%; }}\n"
    f".table-wrap {{ margin-top: 28px; }}\n"
    f"table {{ border-collapse: collapse; width: 100%; max-width: 700px; font-size: 13px; }}\n"
    f"th {{ background: {t.bg_surface}; color: {t.text_secondary}; padding: 10px 14px; text-align: left; font-weight: 500; }}\n"
    f"td {{ padding: 10px 14px; border-bottom: 1px solid {t.border}; color: {t.text_primary}; }}\n"
    f"tr:hover td {{ background: {t.bg_hover}; }}\n"
    f".highlight {{ color: {t.accent}; font-weight: 600; }}\n"
)
    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>算法对比 - {filename}</title>
<style>
{_themed_css(_cmp_extra)}
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

const maxRatio = Math.max(...results.map(r => r.ratio), 0.01);

results.forEach(r => {{
  const pct = (r.ratio / maxRatio) * 100;
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


def open_comparison_html(html: str) -> None:
    tmp = Path(tempfile.mktemp(suffix=".html"))
    tmp.write_text(html, encoding="utf-8")
    logger.info("[comparison] wrote HTML to %s, opening in browser", tmp)
    webbrowser.open(tmp.as_uri())
