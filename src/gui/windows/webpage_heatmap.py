from __future__ import annotations

import logging
import re
import tempfile
import webbrowser
from pathlib import Path
from html.parser import HTMLParser

logger = logging.getLogger(__name__)


class ResourceReference:
    __slots__ = ('tag', 'attr', 'url', 'start', 'end', 'element_start', 'element_end')

    def __init__(self, tag: str, attr: str, url: str,
                 start: int, end: int, element_start: int, element_end: int):
        self.tag = tag
        self.attr = attr
        self.url = url
        self.start = start
        self.end = end
        self.element_start = element_start
        self.element_end = element_end


_RESOURCE_TAGS = {
    'link': 'href',
    'script': 'src',
    'img': 'src',
    'source': 'src',
    'video': 'src',
    'audio': 'src',
    'iframe': 'src',
    'embed': 'src',
    'object': 'data',
}


def _ratio_to_color(ratio: float) -> str:
    if ratio <= 0.3:
        return "#22c55e"
    if ratio <= 0.6:
        t = (ratio - 0.3) / 0.3
        r = int(34 + t * (234 - 34))
        g = int(197 - t * (197 - 179))
        b = int(94 - t * (94 - 8))
        return f"rgb({r},{g},{b})"
    t = min((ratio - 0.6) / 0.4, 1.0)
    r = int(234 + t * (220 - 234))
    g = int(179 - t * (179 - 38))
    b = int(8 + t * (38 - 8))
    return f"rgb({r},{g},{b})"


def _extract_filename(url: str) -> str:
    name = url.split('?')[0].split('#')[0]
    return Path(name).name if name else ""


def _find_resource_refs(html_content: str) -> list[ResourceReference]:
    refs = []
    pattern = re.compile(
        r'<(link|script|img|source|video|audio|iframe|embed|object)'
        r'\s([^>]*?)>',
        re.IGNORECASE | re.DOTALL
    )
    for m in pattern.finditer(html_content):
        tag = m.group(1).lower()
        attr_name = _RESOURCE_TAGS.get(tag)
        if not attr_name:
            continue
        attrs_str = m.group(2)
        attr_pattern = re.compile(
            rf'{attr_name}\s*=\s*["\']([^"\']+)["\']',
            re.IGNORECASE
        )
        attr_match = attr_pattern.search(attrs_str)
        if not attr_match:
            continue
        url = attr_match.group(1)
        if url.startswith('data:') or url.startswith('javascript:') or url.startswith('//'):
            continue
        abs_start = m.start(2) + attr_match.start(1)
        abs_end = m.start(2) + attr_match.end(1)
        refs.append(ResourceReference(
            tag=tag, attr=attr_name, url=url,
            start=abs_start, end=abs_end,
            element_start=m.start(), element_end=m.end(),
        ))
    return refs


def _inline_style_refs(html_content: str) -> list[ResourceReference]:
    refs = []
    pattern = re.compile(r'<style[^>]*>', re.IGNORECASE)
    for m in pattern.finditer(html_content):
        refs.append(ResourceReference(
            tag='style', attr='inline', url='__inline_css__',
            start=m.start(), end=m.end(),
            element_start=m.start(), element_end=m.end(),
        ))
    return refs


def generate_webpage_heatmap(
    html_content: str,
    resource_ratios: dict[str, float],
    folder_name: str = "webpage",
    total_original: int = 0,
    total_compressed: int = 0,
) -> str:
    refs = _find_resource_refs(html_content)
    style_refs = _inline_style_refs(html_content)
    all_refs = refs + style_refs

    resource_map = {}
    for url, ratio in resource_ratios.items():
        fname = _extract_filename(url)
        if fname:
            resource_map[fname.lower()] = ratio
        resource_map[url.lower()] = ratio

    annotated = []
    annotated_count = 0
    best_name, best_ratio = "", 1.0
    worst_name, worst_ratio = "", 0.0

    for ref in refs:
        fname = _extract_filename(ref.url)
        ratio = resource_map.get(fname.lower()) or resource_map.get(ref.url.lower())
        if ratio is not None:
            color = _ratio_to_color(ratio)
            pct = ratio * 100
            annotated.append({
                'element_start': ref.element_start,
                'element_end': ref.element_end,
                'color': color,
                'ratio': ratio,
                'label': f"{fname} ({pct:.0f}%)",
                'tag': ref.tag,
            })
            annotated_count += 1
            if ratio < best_ratio:
                best_ratio = ratio
                best_name = fname
            if ratio > worst_ratio:
                worst_ratio = ratio
                worst_name = fname

    inject_css = """
<style id="wcx-heatmap-inject">
.wcx-heatmap-overlay {
  position: relative;
  display: inline;
}
.wcx-heatmap-overlay::after {
  content: '';
  position: absolute;
  top: 0; left: 0; right: 0; bottom: 0;
  pointer-events: none;
  border-radius: 3px;
  z-index: 9999;
}
.wcx-heatmap-badge {
  position: absolute;
  top: -18px;
  left: 4px;
  font-size: 10px;
  font-family: Consolas, monospace;
  color: #fff;
  padding: 1px 5px;
  border-radius: 3px;
  white-space: nowrap;
  z-index: 10000;
  pointer-events: none;
  font-weight: 600;
  box-shadow: 0 1px 4px rgba(0,0,0,0.4);
}
.wcx-heatmap-wrap {
  position: relative;
  display: inline-block;
  margin: 4px 2px;
  border-radius: 4px;
  padding: 2px 4px;
}
.wcx-heatmap-wrap > .wcx-heatmap-badge {
  top: -16px;
  left: 2px;
}
</style>
"""

    inject_js = """
<script id="wcx-heatmap-js">
(function() {
  const badges = document.querySelectorAll('.wcx-heatmap-badge');
  badges.forEach(b => {
    b.addEventListener('mouseenter', function() {
      this.style.transform = 'scale(1.2)';
      this.style.zIndex = '10001';
    });
    b.addEventListener('mouseleave', function() {
      this.style.transform = '';
      this.style.zIndex = '10000';
    });
  });

  const panel = document.createElement('div');
  panel.id = 'wcx-panel';
  panel.innerHTML = `
    <div style="position:fixed;bottom:16px;right:16px;background:#0f172a;border:1px solid #334155;
                border-radius:12px;padding:16px 20px;font-family:'Microsoft YaHei',sans-serif;
                color:#e2e8f0;font-size:13px;z-index:99999;box-shadow:0 8px 32px rgba(0,0,0,0.5);
                min-width:280px;max-width:400px;">
      <div style="font-size:16px;font-weight:700;margin-bottom:8px;">🔥 网页资源压缩热力图</div>
      <div style="color:#94a3b8;font-size:11px;margin-bottom:12px;">标注资源: __COUNT__ 个</div>
      <div style="display:flex;gap:8px;margin-bottom:8px;">
        <div style="background:#1e293b;border-radius:6px;padding:8px 12px;flex:1;text-align:center;">
          <div style="font-size:18px;font-weight:700;color:#38bdf8;">__TOTAL_ORIG__</div>
          <div style="font-size:10px;color:#94a3b8;">原始大小</div>
        </div>
        <div style="background:#1e293b;border-radius:6px;padding:8px 12px;flex:1;text-align:center;">
          <div style="font-size:18px;font-weight:700;color:#38bdf8;">__TOTAL_COMP__</div>
          <div style="font-size:10px;color:#94a3b8;">压缩后</div>
        </div>
        <div style="background:#1e293b;border-radius:6px;padding:8px 12px;flex:1;text-align:center;">
          <div style="font-size:18px;font-weight:700;color:#38bdf8;">__TOTAL_RATIO__</div>
          <div style="font-size:10px;color:#94a3b8;">总压缩率</div>
        </div>
      </div>
      <div style="display:flex;gap:8px;">
        <div style="background:#1e293b;border-radius:6px;padding:6px 10px;flex:1;text-align:center;">
          <span style="color:#22c55e;">🟢 最优</span>
          <div style="font-size:12px;font-weight:600;color:#22c55e;">__BEST__</div>
        </div>
        <div style="background:#1e293b;border-radius:6px;padding:6px 10px;flex:1;text-align:center;">
          <span style="color:#ef4444;">🔴 最差</span>
          <div style="font-size:12px;font-weight:600;color:#ef4444;">__WORST__</div>
        </div>
      </div>
      <div style="margin-top:8px;color:#64748b;font-size:10px;">图例: 🟢高效 🟡中等 🔴低效 | 资源边框颜色=压缩热力</div>
    </div>
  `;
  document.body.appendChild(panel);
})();
</script>
"""

    def _fmt_size(n: int) -> str:
        if n < 1024:
            return f"{n}B"
        if n < 1024 * 1024:
            return f"{n / 1024:.1f}KB"
        return f"{n / (1024 * 1024):.1f}MB"

    total_ratio_val = (1 - total_compressed / total_original) if total_original > 0 else 0
    inject_js = inject_js.replace('__COUNT__', str(annotated_count))
    inject_js = inject_js.replace('__TOTAL_ORIG__', _fmt_size(total_original))
    inject_js = inject_js.replace('__TOTAL_COMP__', _fmt_size(total_compressed))
    inject_js = inject_js.replace('__TOTAL_RATIO__', f"{total_ratio_val * 100:.1f}%")
    inject_js = inject_js.replace('__BEST__', f"{best_name} ({best_ratio * 100:.0f}%)" if best_name else "N/A")
    inject_js = inject_js.replace('__WORST__', f"{worst_name} ({worst_ratio * 100:.0f}%)" if worst_name else "N/A")

    modified = html_content

    if '</head>' in modified.lower():
        head_close = modified.lower().find('</head>')
        modified = modified[:head_close] + inject_css + modified[head_close:]
    else:
        modified = inject_css + modified

    for ref in sorted(annotated, key=lambda r: r['element_start'], reverse=True):
        orig_tag = modified[ref['element_start']:ref['element_end']]
        color = ref['color']
        label = ref['label']
        border_style = f'border:2px solid {color};background:rgba({_css_color_to_rgba(color)},0.08);'
        if ref['tag'] == 'img':
            wrap = (f'<span class="wcx-heatmap-wrap" style="{border_style}position:relative;display:inline-block;">'
                    f'{orig_tag}'
                    f'<span class="wcx-heatmap-badge" style="background:{color};">{label}</span>'
                    f'</span>')
        else:
            wrap = (f'{orig_tag[:-1]} data-wcx-heatmap="1" '
                    f'style="border:2px solid {color};background:rgba({_css_color_to_rgba(color)},0.06);border-radius:3px;"'
                    f'><span class="wcx-heatmap-badge" style="background:{color};position:absolute;margin-top:-20px;">{label}</span>')
        modified = modified[:ref['element_start']] + wrap + modified[ref['element_end']:]

    if '</body>' in modified.lower():
        body_close = modified.lower().rfind('</body>')
        modified = modified[:body_close] + inject_js + modified[body_close:]
    else:
        modified += inject_js

    return modified


def _css_color_to_rgba(color: str) -> str:
    if color.startswith('#') and len(color) == 7:
        r = int(color[1:3], 16)
        g = int(color[3:5], 16)
        b = int(color[5:7], 16)
        return f"{r},{g},{b}"
    if color.startswith('rgb('):
        return color[4:-1]
    return "100,100,100"


def open_webpage_heatmap(html: str) -> None:
    tmp = Path(tempfile.mktemp(suffix=".html"))
    tmp.write_text(html, encoding="utf-8")
    logger.info("[webpage_heatmap] wrote to %s, opening", tmp)
    webbrowser.open(tmp.as_uri())
