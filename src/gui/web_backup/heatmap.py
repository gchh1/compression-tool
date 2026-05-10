from __future__ import annotations

import json
import logging
import tempfile
import webbrowser
from pathlib import Path

logger = logging.getLogger(__name__)


def _ratio_to_color(ratio: float) -> str:
    if ratio <= 0.05:
        return "#22c55e"
    if ratio <= 0.2:
        r = int(34 + (ratio / 0.2) * (234 - 34))
        g = int(197 - (ratio / 0.2) * (197 - 179))
        b = int(94 - (ratio / 0.2) * (94 - 8))
        return f"rgb({r},{g},{b})"
    if ratio <= 0.5:
        t = (ratio - 0.2) / 0.3
        r = int(234 + t * (239 - 234))
        g = int(179 - t * (179 - 68))
        b = int(8 + t * (68 - 8))
        return f"rgb({r},{g},{b})"
    t = min((ratio - 0.5) / 0.5, 1.0)
    r = int(239 + t * (220 - 239))
    g = int(68 - t * (68 - 38))
    b = int(68 + t * (38 - 68))
    return f"rgb({r},{g},{b})"


def generate_heatmap(
    raw_data: bytes,
    compressed_data: bytes,
    block_size: int = 256,
    filename: str = "unknown",
    algorithm: str = "unknown",
    original_size: int = 0,
    compressed_size: int = 0,
    time_ms: float = 0.0,
) -> str:
    logger.info("[heatmap] generating for %s, algo=%s, raw=%d, comp=%d, block=%d",
                 filename, algorithm, len(raw_data), compressed_size, block_size)
    n_blocks = max(1, (len(raw_data) + block_size - 1) // block_size)
    blocks = []

    from gui.core.engine import CompressionEngine
    from gui.core.models import AlgorithmType

    algo_map = {e.value: e for e in AlgorithmType}
    algo = algo_map.get(algorithm, AlgorithmType.DEFLATE)
    logger.info("[heatmap] resolved algo: %s -> %s", algorithm, algo.value)
    engine = CompressionEngine()

    for i in range(n_blocks):
        start = i * block_size
        end = min(start + block_size, len(raw_data))
        block_raw = raw_data[start:end]

        r = engine.compress(block_raw, algo)

        ratio = r.compressed_size / len(block_raw) if len(block_raw) > 0 else 1.0
        blocks.append({
            "index": i,
            "offset": start,
            "size": end - start,
            "compressed_size": r.compressed_size,
            "ratio": round(ratio, 4),
            "color": _ratio_to_color(ratio),
        })

    logger.info("[heatmap] generated %d blocks", n_blocks)

    blocks_json = json.dumps(blocks, ensure_ascii=False)

    ratio_pct = f"{compressed_size / original_size * 100:.1f}" if original_size else "0"

    from gui.core.theme import ThemeManager
    t = ThemeManager.get()
    base_css = ThemeManager.html_base_css()

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>压缩热力图 - {filename}</title>
<style>
{base_css}
.legend-bar {{ width: 200px; height: 12px; border-radius: 6px; background: linear-gradient(to right, #22c55e, #eab308, #ef4444); }}
.heatmap {{ display: flex; flex-wrap: wrap; gap: 2px; max-width: 900px; }}
.block {{ width: 20px; height: 20px; border-radius: 2px; cursor: pointer; transition: transform 0.1s; position: relative; }}
.block:hover {{ transform: scale(1.8); z-index: 10; box-shadow: 0 0 8px rgba(255,255,255,0.3); }}
.stats {{ margin-top: 24px; display: flex; gap: 24px; }}
.stat .num {{ font-size: 24px; font-weight: 700; color: {t.accent}; }}
</style>
</head>
<body>
<h1>📊 压缩热力图</h1>
<div class="meta">
  文件: {filename} &nbsp;|&nbsp; 算法: {algorithm} &nbsp;|&nbsp;
  原始: {original_size:,} B &nbsp;|&nbsp; 压缩后: {compressed_size:,} B &nbsp;|&nbsp;
  耗时: {time_ms:.1f} ms
</div>
<div class="legend">
  <span>高压缩</span>
  <div class="legend-bar"></div>
  <span>低压缩</span>
  <span style="margin-left:12px">块大小: {block_size} B</span>
</div>
<div class="heatmap" id="heatmap"></div>
<div class="stats">
  <div class="stat"><div class="num">{original_size:,}</div><div class="desc">原始大小 (B)</div></div>
  <div class="stat"><div class="num">{compressed_size:,}</div><div class="desc">压缩后 (B)</div></div>
  <div class="stat"><div class="num">{ratio_pct}</div><div class="desc">压缩率 %</div></div>
  <div class="stat"><div class="num">{time_ms:.1f}</div><div class="desc">耗时 (ms)</div></div>
</div>
<div class="tooltip" id="tooltip"></div>
<script>
const blocks = {blocks_json};
const container = document.getElementById('heatmap');
const tooltip = document.getElementById('tooltip');

blocks.forEach(b => {{
  const div = document.createElement('div');
  div.className = 'block';
  div.style.backgroundColor = b.color;
  div.addEventListener('mouseenter', e => {{
    tooltip.innerHTML =
      '<div><span class="label">偏移:</span> <span class="value">0x' + b.offset.toString(16).toUpperCase() + '</span></div>' +
      '<div><span class="label">块大小:</span> <span class="value">' + b.size + ' B</span></div>' +
      '<div><span class="label">压缩后:</span> <span class="value">' + b.compressed_size + ' B</span></div>' +
      '<div><span class="label">压缩率:</span> <span class="value">' + (b.ratio * 100).toFixed(1) + '%</span></div>';
    tooltip.classList.add('visible');
  }});
  div.addEventListener('mousemove', e => {{
    tooltip.style.left = (e.clientX + 12) + 'px';
    tooltip.style.top = (e.clientY + 12) + 'px';
  }});
  div.addEventListener('mouseleave', () => {{
    tooltip.classList.remove('visible');
  }});
  container.appendChild(div);
}});
</script>
</body>
</html>"""
    return html


def open_heatmap_html(html: str) -> None:
    tmp = Path(tempfile.mktemp(suffix=".html"))
    tmp.write_text(html, encoding="utf-8")
    logger.info("[heatmap] wrote HTML to %s, opening in browser", tmp)
    webbrowser.open(tmp.as_uri())
