from __future__ import annotations

import html as html_module
import json
import logging
import tempfile
import webbrowser
from pathlib import Path

from gui.core.models import AlgorithmType, ResourceType, TEXT_EXTENSIONS, SCRIPT_EXTENSIONS
from gui.core.token_parser import (
    Token,
    TokenType,
    ParseResult,
    get_parser,
    can_parse,
)

logger = logging.getLogger(__name__)

_TEXT_TYPES = TEXT_EXTENSIONS | SCRIPT_EXTENSIONS | frozenset({".html", ".htm", ".css", ".json", ".xml"})


def _themed_css(extra: str = "") -> str:
    from gui.core.theme import ThemeManager
    t = ThemeManager.get()
    base = (
        f"* {{ margin: 0; padding: 0; box-sizing: border-box; }}\n"
        f"body {{ font-family: 'Microsoft YaHei','Consolas','Courier New',monospace; "
        f"background: {t.bg_primary}; color: {t.text_primary}; padding: 24px; }}\n"
        f"h1 {{ font-size: 20px; margin-bottom: 6px; color: {t.text_primary}; }}\n"
        f".meta {{ color: {t.text_secondary}; font-size: 13px; margin-bottom: 16px; }}\n"
        f".stats {{ display: flex; gap: 12px; margin-bottom: 20px; flex-wrap: wrap; }}\n"
        f".stat {{ background: {t.bg_surface}; border-radius: 8px; padding: 12px 14px; min-width: 100px; }}\n"
        f".stat .num {{ font-size: 18px; font-weight: 700; color: {t.accent}; }}\n"
        f".stat .desc {{ font-size: 11px; color: {t.text_secondary}; margin-top: 2px; }}\n"
        f".legend {{ display: flex; align-items: center; gap: 10px; margin-bottom: 12px; font-size: 12px; color: {t.text_secondary}; }}\n"
        f".legend-bar {{ width: 180px; height: 10px; border-radius: 5px; background: linear-gradient(to right, #22c55e, #eab308, #ef4444); }}\n"
        f".tooltip {{ display: none; position: fixed; background: {t.bg_elevated}; border: 1px solid {t.border_dark}; "
        f"border-radius: 8px; padding: 12px 16px; font-size: 12px; z-index: 200; "
        f"pointer-events: none; box-shadow: 0 8px 24px rgba(0,0,0,0.3); min-width: 220px; }}\n"
        f".tooltip.visible {{ display: block; }}\n"
        f".tooltip .tl {{ color: {t.text_secondary}; }}\n"
        f".tooltip .tv {{ color: {t.text_primary}; font-weight: 600; font-family: Consolas,monospace; }}\n"
        f".tooltip .tg {{ color: {t.accent}; font-weight: 700; }}\n"
        f".tooltip .tr {{ color: {t.text_secondary}; font-size: 11px; margin-top: 6px; padding-top: 6px; border-top: 1px solid {t.border}; }}\n"
    )
    return base + extra


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


def _is_text_like(extension: str) -> bool:
    return extension in _TEXT_TYPES


def _try_decode_text(raw_data: bytes) -> str | None:
    try:
        return raw_data.decode("utf-8")
    except UnicodeDecodeError:
        try:
            return raw_data.decode("latin-1")
        except Exception:
            return None


def _escape_html(text: str) -> str:
    return html_module.escape(text).replace("\n", "<br>").replace("\t", "&nbsp;" * 4).replace(" ", "&nbsp;")


def _build_byte_to_char_map(text: str) -> list[int]:
    mapping = []
    char_idx = 0
    for ch in text:
        byte_len = len(ch.encode('utf-8'))
        for _ in range(byte_len):
            mapping.append(char_idx)
        char_idx += 1
    return mapping


def _build_token_json(tokens: list[Token], text: str | None = None) -> str:
    byte_to_char = _build_byte_to_char_map(text) if text is not None else None
    items = []
    for t in tokens:
        cs = t.compressed_size
        comp_str = f"{cs:.2f}" if isinstance(cs, float) and cs != int(cs) else str(int(cs))

        if byte_to_char is not None:
            bs = t.original_start
            be = min(bs + t.original_length, len(byte_to_char))
            cs_start = byte_to_char[bs] if bs < len(byte_to_char) else (len(byte_to_char) // max(len(text or ''), 1))
            cs_end = byte_to_char[be] if be < len(byte_to_char) else len(text or '')
            char_start = cs_start
            char_length = max(cs_end - cs_start, 0)
        else:
            char_start = t.original_start
            char_length = t.original_length

        items.append({
            "type": t.type.value,
            "start": char_start,
            "length": char_length,
            "byte_start": t.original_start,
            "byte_length": t.original_length,
            "comp_size": cs,
            "comp_str": comp_str,
            "ratio": round(t.compression_ratio, 4),
            "color": _ratio_to_color(t.compression_ratio),
            "offset": t.match_offset,
            "hf_bits": round(t.huffman_bits, 1) if t.huffman_bits else 0,
            "hf_detail": t.huffman_detail or "",
        })
    return json.dumps(items, ensure_ascii=False)


def _stats_html(pr: ParseResult, filename: str, algorithm: str,
                original_size: int, compressed_size: int, time_ms: float) -> str:

    literals = [t for t in pr.tokens if t.type == TokenType.LITERAL]
    literal_runs = [t for t in pr.tokens if t.type == TokenType.LITERAL_RUN]
    matches = [t for t in pr.tokens if t.type == TokenType.MATCH]

    total_literal_bytes = sum(t.original_length for t in literals) + \
                           sum(t.original_length for t in literal_runs)
    total_match_bytes = sum(t.original_length for t in matches)

    best_match = min(matches, key=lambda t: t.compression_ratio) if matches else None

    best_str = f"len={best_match.original_length}, off={best_match.match_offset}" if best_match else "N/A"
    worst_match = max(matches, key=lambda t: t.compression_ratio) if matches else None
    worst_str = f"ratio={worst_match.compression_ratio:.2f}" if worst_match else "N/A"

    avg_ratio = pr.total_compressed / pr.total_original if pr.total_original > 0 else 1.0

    return f"""
  <div class="stat"><div class="num">{original_size:,}</div><div class="desc">原始大小</div></div>
  <div class="stat"><div class="num">{compressed_size:,}</div><div class="desc">压缩后大小</div></div>
  <div class="stat"><div class="num">{avg_ratio*100:.1f}%</div><div class="desc">Token 加权压缩率</div></div>
  <div class="stat"><div class="num">{len(pr.tokens)}</div><div class="desc">Token 总数</div></div>
  <div class="stat"><div class="num">{len(matches)}</div><div class="desc">匹配 Token</div></div>
  <div class="stat"><div class="num">{len(literals)+len(literal_runs)}</div><div class="desc">字面量 Token</div></div>
  <div class="stat"><div class="num">{total_match_bytes:,}B / {total_literal_bytes:,}B</div><div class="desc">匹配/字面 字节</div></div>
  <div class="stat"><div class="num">{time_ms:.1f}ms</div><div class="desc">压缩耗时</div></div>"""


def _text_view_html(raw_text: str, tokens_json: str, stats_html: str,
                    filename: str, algorithm: str,
                    original_size: int, compressed_size: int, time_ms: float) -> str:
        from gui.core.theme import ThemeManager
t = ThemeManager.get()
    _tv_extra = (
    f".text-view {{\n"
    f"  background: {t.bg_surface}; border-radius: 8px; padding: 16px;\n"
    f"  font-family: \\"Consolas\\",\\"Courier New\\",monospace; font-size: 13px; line-height: 1.6;\n"
    f"  white-space: pre-wrap; word-break: break-all; overflow-x: auto; max-height: 60vh;\n"
    f"  border: 1px solid {t.border};\n"
    f"}}\n"
    f".token {{ cursor: pointer; padding: 1px 0; border-radius: 2px; transition: background 0.15s; }}\n"
    f".token:hover {{ filter: brightness(1.3); outline: 1px solid rgba(255,255,255,0.3); }}\n"
)
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>Token 热力图 - {filename}</title>
<style>
{_themed_css(_bk_extra)}
</style>
</head>
<body>
<h1>🔥 Token 级别压缩热力图</h1>
<div class="meta">文件: {filename} &nbsp;|&nbsp; 算法: {algorithm} &nbsp;|&nbsp; 模式: 文本高亮</div>

<div class="stats">{stats_html}</div>

<div class="legend">
  <span>高效压缩</span>
  <div class="legend-bar"></div>
  <span>低效/膨胀</span>
  <span style="margin-left:12px; color:#64748b">| 悬停查看 Token 详情</span>
</div>

<div class="text-view" id="textView"></div>
<div class="tooltip" id="tooltip"></div>

<script>
const tokens = {tokens_json};
const rawText = `{_escape_html(raw_text.replace("`", "\\`"))}`;
const textView = document.getElementById('textView');
const tooltip = document.getElementById('tooltip');

let html = '';
let pos = 0;

tokens.forEach((t, idx) => {{
  const chunk = rawText.substring(pos, pos + t.length);
  const span = '<span class="token" data-idx="' + idx + '" style="background:' + t.color + '">' + chunk + '</span>';
  html += span;
  pos += t.length;
}});
textView.innerHTML = html;

textView.addEventListener('mouseenter', function(e) {{
  if (!e.target.classList.contains('token')) return;
  const idx = parseInt(e.target.dataset.idx);
  const t = tokens[idx];
  const typeLabel = t.type === 'match' ? 'MATCH' : (t.type === 'literal_run' ? 'LITERAL_RUN' : 'LITERAL');
  let extra = '';
  if (t.type === 'match') {{
    extra = '<div class="tr">回退偏移: <span class="tv">' + t.offset + '</span> 字符</div>';
  }}
  tooltip.innerHTML =
    '<div><span class="tl">类型:</span> <span class="tg">' + typeLabel + '</span></div>' +
    '<div><span class="tl">字符位置:</span> <span class="tv">' + t.start + ' ~ ' + (t.start + t.length - 1) + '</span></div>' +
    '<div><span class="tl">字节位置:</span> <span class="tv">' + t.byte_start + ' ~ ' + (t.byte_start + t.byte_length - 1) + '</span></div>' +
    '<div><span class="tl">字符/字节长度:</span> <span class="tv">' + t.length + ' / ' + t.byte_length + '</span></div>' +
    '<div><span class="tl">编码大小:</span> <span class="tv">' + t.comp_str + ' B</span></div>' +
    '<div><span class="tl">压缩率:</span> <span class="tv" style="color:' + t.color + '">' + (t.ratio * 100).toFixed(1) + '%</span></div>' +
    (t.hf_bits > 0 ? '<div class="tr"><span class="tl">Huffman:</span> <span class="tv" style="color:#a78bfa">' + t.hf_bits + ' bit | ' + t.hf_detail + '</span></div>' : '') +
    extra;
  tooltip.classList.add('visible');
}}, true);

textView.addEventListener('mousemove', function(e) {{
  if (!e.target.classList.contains('token')) return;
  tooltip.style.left = Math.min(e.clientX + 14, window.innerWidth - 260) + 'px';
  tooltip.style.top = Math.min(e.clientY + 14, window.innerHeight - 160) + 'px';
}}, true);

textView.addEventListener('mouseleave', function(e) {{
  if (!e.target.classList.contains('token')) return;
  tooltip.classList.remove('visible');
}}, true);

}});
</script>
</body>
</html>"""


def _block_view_html(tokens_json: str, stats_html: str,
                     filename: str, algorithm: str,
                     original_size: int, compressed_size: int, time_ms: float) -> str:
        from gui.core.theme import ThemeManager
t = ThemeManager.get()
    _tv_extra = (
    f".text-view {{\n"
    f"  background: {t.bg_surface}; border-radius: 8px; padding: 16px;\n"
    f"  font-family: \\"Consolas\\",\\"Courier New\\",monospace; font-size: 13px; line-height: 1.6;\n"
    f"  white-space: pre-wrap; word-break: break-all; overflow-x: auto; max-height: 60vh;\n"
    f"  border: 1px solid {t.border};\n"
    f"}}\n"
    f".token {{ cursor: pointer; padding: 1px 0; border-radius: 2px; transition: background 0.15s; }}\n"
    f".token:hover {{ filter: brightness(1.3); outline: 1px solid rgba(255,255,255,0.3); }}\n"
)
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>Token 热力图 - {filename}</title>
<style>
{_themed_css(_tv_extra)}
</style>
</head>
<body>
<h1>🔥 Token 级别压缩热力图</h1>
<div class="meta">文件: {filename} &nbsp;|&nbsp; 算法: {algorithm} &nbsp;|&nbsp; 模式: 块状视图（二进制/非文本）</div>

<div class="stats">{stats_html}</div>

<div class="legend">
  <span>高效压缩</span>
  <div class="legend-bar"></div>
  <span>低效/膨胀</span>
  <span style="margin-left:12px;color:#64748b">| 绿色边框=Match 黄色=Literal | 悬停查看详情</span>
</div>

<div class="blocks" id="blocks"></div>
<div class="tooltip" id="tooltip"></div>

<script>
const tokens = {tokens_json};
const container = document.getElementById('blocks');
const tooltipEl = document.getElementById('tooltip');

const maxSize = Math.max(...tokens.map(t => t.length), 1);
const minBlockW = 8;
const maxBlockW = 120;

tokens.forEach((t, idx) => {{
  const w = Math.max(minBlockW, Math.round((t.length / maxSize) * maxBlockW));
  const cls = t.type === 'match' ? 'block match' : (t.type === 'literal_run' ? 'block literal run' : 'block literal');
  const div = document.createElement('div');
  div.className = cls;
  div.style.backgroundColor = t.color;
  div.style.width = w + 'px';
  div.title = '#' + (idx+1) + ' ' + t.type + ' [' + t.length + 'B]';
  div.dataset.idx = idx;
  container.appendChild(div);
}});

container.addEventListener('mouseenter', function(e) {{
  if (!e.target.classList.contains('block')) return;
  const idx = parseInt(e.target.dataset.idx);
  const t = tokens[idx];
  const typeLabel = t.type === 'match' ? 'MATCH' : (t.type === 'literal_run' ? 'LITERAL_RUN' : 'LITERAL');
  let extra = '';
  if (t.type === 'match') {{
    extra = '<div style="margin-top:6px;padding-top:6px;border-top:1px solid #334155;color:#94a3b8;font-size:11px">回退偏移: <span style="color:#f1f5f9;font-weight:600">' + t.offset + '</span></div>';
  }}
  tooltipEl.innerHTML =
    '<div><span style="color:#94a3b8">类型:</span> <span style="color:#38bdf8;font-weight:700">' + typeLabel + '</span></div>' +
    '<div><span style="color:#94a3b8">字节偏移:</span> <span style="color:#f1f5f9;font-weight:600;font-family:Consolas,monospace">' + t.byte_start + '</span></div>' +
    '<div><span style="color:#94a3b8">字节长度:</span> <span style="color:#f1f5f9;font-weight:600">' + t.byte_length + ' B</span></div>' +
    '<div><span style="color:#94a3b8">编码大小:</span> <span style="color:#f1f5f9;font-weight:600">' + t.comp_str + ' B</span></div>' +
    '<div><span style="color:#94a3b8">压缩率:</span> <span style="color:' + t.color + ';font-weight:700">' + (t.ratio * 100).toFixed(1) + '%</span></div>' +
    (t.hf_bits > 0 ? '<div style="margin-top:6px;padding-top:6px;border-top:1px solid #334155;color:#94a3b8;font-size:11px">Huffman: <span style="color:#a78bfa;font-weight:600">' + t.hf_bits + ' bit | ' + t.hf_detail + '</span></div>' : '') +
    extra;
  tooltipEl.classList.add('visible');
}}, true);

container.addEventListener('mousemove', function(e) {{
  if (!e.target.classList.contains('block')) return;
  tooltipEl.style.left = Math.min(e.clientX + 14, window.innerWidth - 280) + 'px';
  tooltipEl.style.top = Math.min(e.clientY + 14, window.innerHeight - 180) + 'px';
}}, true);

container.addEventListener('mouseleave', function(e) {{
  if (!e.target.classList.contains('block')) return;
  tooltipEl.classList.remove('visible');
}}, true);
</script>
</body>
</html>"""


def _dp_text_view_html(raw_text: str, tokens_json: str, dp_json: str,
                       stats_html: str, filename: str, algorithm: str,
                       original_size: int, compressed_size: int, time_ms: float) -> str:
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
    from gui.core.theme import ThemeManager
t = ThemeManager.get()
    _dp_extra = (
    f".tabs {{ display: flex; gap: 0; margin-bottom: 16px; }}\n"
    f".tab {{ padding: 8px 20px; cursor: pointer; background: {t.bg_surface}; color: {t.text_secondary}; \n"
    f"  border: 1px solid {t.border}; font-size: 13px; }}\n"
    f".tab:first-child {{ border-radius: 6px 0 0 6px; }}\n"
    f".tab:last-child {{ border-radius: 0 6px 6px 0; }}\n"
    f".tab.active {{ background: {t.bg_hover}; color: {t.text_primary}; font-weight: 600; }}\n"
    f".panel {{ display: none; }}\n"
    f".panel.active {{ display: block; }}\n"
    f".text-view {{\n"
    f"  background: {t.bg_surface}; border-radius: 8px; padding: 16px;\n"
    f"  font-family: \\"Consolas\\",\\"Courier New\\",monospace; font-size: 13px; line-height: 1.6;\n"
    f"  white-space: pre-wrap; word-break: break-all; overflow-x: auto; max-height: 60vh;\n"
    f"  border: 1px solid {t.border};\n"
    f"}}\n"
    f".token {{ cursor: pointer; padding: 1px 0; border-radius: 2px; transition: background 0.15s; }}\n"
    f".token:hover {{ filter: brightness(1.3); outline: 1px solid rgba(255,255,255,0.3); }}\n"
    f".token.dp-chosen {{ outline: 2px solid #22c55e; }}\n"
    f".token.dp-rejected {{ outline: 2px dashed #ef4444; opacity: 0.7; }}\n"
    f".dp-section {{ margin-top: 24px; }}\n"
    f".dp-step {{ background: {t.bg_surface}; border-radius: 8px; padding: 12px 16px; margin-bottom: 8px; border: 1px solid {t.border}; }}\n"
    f".dp-step-header {{ display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }}\n"
    f".dp-pos {{ color: {t.accent}; font-weight: 700; font-family: Consolas,monospace; }}\n"
    f".dp-count {{ color: {t.text_secondary}; font-size: 12px; }}\n"
    f".dp-candidates {{ display: flex; flex-direction: column; gap: 4px; }}\n"
    f".dp-cand {{ display: flex; align-items: center; gap: 8px; padding: 4px 8px; border-radius: 4px; font-size: 12px; font-family: Consolas,monospace; }}\n"
    f".dp-cand.chosen {{ background: rgba(34,197,94,0.15); border-left: 3px solid #22c55e; }}\n"
    f".dp-cand.rejected {{ background: rgba(239,68,68,0.08); border-left: 3px solid #ef4444; opacity: 0.7; }}\n"
    f".dp-timeline {{ margin-top: 24px; }}\n"
    f".dp-bar-container {{ position: relative; height: 60px; background: {t.bg_surface}; border-radius: 8px; overflow: hidden; border: 1px solid {t.border}; }}\n"
    f".dp-bar {{ position: absolute; top: 0; height: 100%; transition: all 0.3s; cursor: pointer; }}\n"
    f".dp-bar:hover {{ filter: brightness(1.3); z-index: 10; }}\n"
)
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>DP 过程可视化 - {filename}</title>
<style>
{_themed_css(_dp_extra)}
</style>
</head>
<body>
<h1>🔥 LZMine DP 过程可视化</h1>
<div class="meta">文件: {filename} &nbsp;|&nbsp; 算法: {algorithm} &nbsp;|&nbsp; DP 优化模式</div>

<div class="stats">{stats_html}</div>

<div class="tabs">
  <div class="tab active" data-panel="token-view">Token 热力图</div>
  <div class="tab" data-panel="dp-view">DP 决策过程</div>
  <div class="tab" data-panel="table-view">Token 明细表</div>
</div>

<div class="panel active" id="token-view">
  <div class="legend">
    <span>高效压缩</span>
    <div class="legend-bar"></div>
    <span>低效/膨胀</span>
    <span style="margin-left:12px; color:#64748b">| 绿色边框=DP选中 红色虚线=DP未选 | 悬停查看详情</span>
  </div>
  <div class="text-view" id="textView"></div>
</div>

<div class="panel" id="dp-view">
  <div class="legend" style="margin-bottom:12px">
    <span style="color:#22c55e">● 选中路径</span>
    <span style="color:#ef4444; margin-left:12px">● 被淘汰候选</span>
    <span style="color:#94a3b8; margin-left:12px">每个位置显示 KMP 匹配候选和 DP 最优决策</span>
  </div>
  <div id="dpSteps"></div>
</div>

<div class="tooltip" id="tooltip"></div>

<script>
const tokens = {tokens_json};
const dpSteps = {dp_json};
const rawText = JSON.parse({json.dumps(json.dumps(raw_text, ensure_ascii=False))});
const textView = document.getElementById('textView');
const tooltip = document.getElementById('tooltip');

const dpPosSet = new Set();
dpSteps.forEach(s => dpPosSet.add(s.position));

let html = '';
let pos = 0;
tokens.forEach((t, idx) => {{
  const chunk = rawText.substring(pos, pos + t.length);
  let cls = 'token';
  if (dpPosSet.has(pos)) cls += ' dp-chosen';
  const span = '<span class="' + cls + '" data-idx="' + idx + '" style="background:' + t.color + '">' + chunk + '</span>';
  html += span;
  pos += t.length;
}});
textView.innerHTML = html;

textView.addEventListener('mouseenter', function(e) {{
  if (!e.target.classList.contains('token')) return;
  const idx = parseInt(e.target.dataset.idx);
  const t = tokens[idx];
  const typeLabel = t.type === 'match' ? 'MATCH' : (t.type === 'literal_run' ? 'LITERAL_RUN' : 'LITERAL');
  let extra = '';
  if (t.type === 'match') {{
    extra = '<div class="tr">回退偏移: <span class="tv">' + t.offset + '</span> 字符</div>';
  }}
  tooltip.innerHTML =
    '<div><span class="tl">类型:</span> <span class="tg">' + typeLabel + '</span></div>' +
    '<div><span class="tl">字符位置:</span> <span class="tv">' + t.start + ' ~ ' + (t.start + t.length - 1) + '</span></div>' +
    '<div><span class="tl">字节位置:</span> <span class="tv">' + t.byte_start + ' ~ ' + (t.byte_start + t.byte_length - 1) + '</span></div>' +
    '<div><span class="tl">字符/字节长度:</span> <span class="tv">' + t.length + ' / ' + t.byte_length + '</span></div>' +
    '<div><span class="tl">编码大小:</span> <span class="tv">' + t.comp_str + ' B</span></div>' +
    '<div><span class="tl">压缩率:</span> <span class="tv" style="color:' + t.color + '">' + (t.ratio * 100).toFixed(1) + '%</span></div>' +
    (t.hf_bits > 0 ? '<div class="tr"><span class="tl">Huffman:</span> <span class="tv" style="color:#a78bfa">' + t.hf_bits + ' bit | ' + t.hf_detail + '</span></div>' : '') +
    extra;
  tooltip.classList.add('visible');
}}, true);

textView.addEventListener('mousemove', function(e) {{
  if (!e.target.classList.contains('token')) return;
  tooltip.style.left = Math.min(e.clientX + 14, window.innerWidth - 260) + 'px';
  tooltip.style.top = Math.min(e.clientY + 14, window.innerHeight - 160) + 'px';
}}, true);

textView.addEventListener('mouseleave', function(e) {{
  if (!e.target.classList.contains('token')) return;
  tooltip.classList.remove('visible');
}}, true);

const dpContainer = document.getElementById('dpSteps');
dpSteps.forEach((step, idx) => {{
  const div = document.createElement('div');
  div.className = 'dp-step';
  let candHtml = '';
  step.candidates.forEach(c => {{
    const cls = c.is_chosen ? 'dp-cand chosen' : 'dp-cand rejected';
    const tag = c.is_chosen ? '<span class="tag">SELECTED</span>' : '<span class="tag">REJECTED</span>';
    const typeStr = c.offset > 0 ? 'MATCH(off=' + c.offset + ', len=' + (c.length+1) + ')' : 'LITERAL';
    candHtml += '<div class="' + cls + '">' + tag + ' <span>' + typeStr + '</span> <span style="color:#94a3b8">next=0x' + c.next_byte.toString(16).padStart(2,'0') + '</span></div>';
  }});
  div.innerHTML =
    '<div class="dp-step-header"><span class="dp-pos">位置 ' + step.position + '</span><span class="dp-count">最优 token 数: ' + step.best_token_count + ' | 候选: ' + step.candidates.length + '</span></div>' +
    '<div class="dp-candidates">' + candHtml + '</div>';
  dpContainer.appendChild(div);
}});

const tbody = document.getElementById('tokenTable');
tokens.forEach((t, idx) => {{
  const typeClass = t.type === 'match' ? 'type-match' : (t.type === 'literal_run' ? 'type-run' : 'type-literal');
  const ratioClass = t.ratio <= 0.3 ? 'ratio-good' : 'ratio-bad';
  const typeLabel = t.type === 'match' ? 'MATCH' : (t.type === 'literal_run' ? 'LITERAL_RUN' : 'LITERAL');
  const offsetStr = t.type === 'match' ? t.offset : '-';
  const tr = document.createElement('tr');
  tr.innerHTML =
    '<td>' + (idx+1) + '</td>' +
    '<td class="' + typeClass + '">' + typeLabel + '</td>' +
    '<td>' + t.start + ' ~ ' + (t.start + t.length - 1) + '</td>' +
    '<td>' + t.length + '</td>' +
    '<td>' + t.comp_str + '</td>' +
    '<td class="' + ratioClass + '">' + (t.ratio * 100).toFixed(2) + '%</td>' +
    '<td>' + offsetStr + '</td>';
  tbody.appendChild(tr);
}});

document.querySelectorAll('.tab').forEach(tab => {{
  tab.addEventListener('click', () => {{
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
    tab.classList.add('active');
    document.getElementById(tab.dataset.panel).classList.add('active');
  }});
}});
</script>
</body>
</html>"""


def _dp_block_view_html(tokens_json: str, dp_json: str, stats_html: str,
                        filename: str, algorithm: str,
                        original_size: int, compressed_size: int, time_ms: float) -> str:
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
    from gui.core.theme import ThemeManager
t = ThemeManager.get()
    _dp_extra = (
    f".tabs {{ display: flex; gap: 0; margin-bottom: 16px; }}\n"
    f".tab {{ padding: 8px 20px; cursor: pointer; background: {t.bg_surface}; color: {t.text_secondary}; \n"
    f"  border: 1px solid {t.border}; font-size: 13px; }}\n"
    f".tab:first-child {{ border-radius: 6px 0 0 6px; }}\n"
    f".tab:last-child {{ border-radius: 0 6px 6px 0; }}\n"
    f".tab.active {{ background: {t.bg_hover}; color: {t.text_primary}; font-weight: 600; }}\n"
    f".panel {{ display: none; }}\n"
    f".panel.active {{ display: block; }}\n"
    f".text-view {{\n"
    f"  background: {t.bg_surface}; border-radius: 8px; padding: 16px;\n"
    f"  font-family: \\"Consolas\\",\\"Courier New\\",monospace; font-size: 13px; line-height: 1.6;\n"
    f"  white-space: pre-wrap; word-break: break-all; overflow-x: auto; max-height: 60vh;\n"
    f"  border: 1px solid {t.border};\n"
    f"}}\n"
    f".token {{ cursor: pointer; padding: 1px 0; border-radius: 2px; transition: background 0.15s; }}\n"
    f".token:hover {{ filter: brightness(1.3); outline: 1px solid rgba(255,255,255,0.3); }}\n"
    f".token.dp-chosen {{ outline: 2px solid #22c55e; }}\n"
    f".token.dp-rejected {{ outline: 2px dashed #ef4444; opacity: 0.7; }}\n"
    f".dp-section {{ margin-top: 24px; }}\n"
    f".dp-step {{ background: {t.bg_surface}; border-radius: 8px; padding: 12px 16px; margin-bottom: 8px; border: 1px solid {t.border}; }}\n"
    f".dp-step-header {{ display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }}\n"
    f".dp-pos {{ color: {t.accent}; font-weight: 700; font-family: Consolas,monospace; }}\n"
    f".dp-count {{ color: {t.text_secondary}; font-size: 12px; }}\n"
    f".dp-candidates {{ display: flex; flex-direction: column; gap: 4px; }}\n"
    f".dp-cand {{ display: flex; align-items: center; gap: 8px; padding: 4px 8px; border-radius: 4px; font-size: 12px; font-family: Consolas,monospace; }}\n"
    f".dp-cand.chosen {{ background: rgba(34,197,94,0.15); border-left: 3px solid #22c55e; }}\n"
    f".dp-cand.rejected {{ background: rgba(239,68,68,0.08); border-left: 3px solid #ef4444; opacity: 0.7; }}\n"
    f".dp-timeline {{ margin-top: 24px; }}\n"
    f".dp-bar-container {{ position: relative; height: 60px; background: {t.bg_surface}; border-radius: 8px; overflow: hidden; border: 1px solid {t.border}; }}\n"
    f".dp-bar {{ position: absolute; top: 0; height: 100%; transition: all 0.3s; cursor: pointer; }}\n"
    f".dp-bar:hover {{ filter: brightness(1.3); z-index: 10; }}\n"
)
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>DP 过程可视化 - {filename}</title>
<style>
{_themed_css(_dp_extra)}
</style>
</head>
<body>
<h1>🔥 LZMine DP 过程可视化</h1>
<div class="meta">文件: {filename} &nbsp;|&nbsp; 算法: {algorithm} &nbsp;|&nbsp; DP 优化模式（二进制/非文本）</div>

<div class="stats">{stats_html}</div>

<div class="tabs">
  <div class="tab active" data-panel="block-view">Token 块状图</div>
  <div class="tab" data-panel="dp-view">DP 决策过程</div>
  <div class="tab" data-panel="table-view">Token 明细表</div>
</div>

<div class="panel active" id="block-view">
  <div class="legend">
    <span>高效压缩</span>
    <div class="legend-bar"></div>
    <span>低效/膨胀</span>
    <span style="margin-left:12px;color:#64748b">| 绿色边框=Match 黄色=Literal | 悬停查看详情</span>
  </div>
  <div class="blocks" id="blocks"></div>
</div>

<div class="panel" id="dp-view">
  <div class="legend" style="margin-bottom:12px">
    <span style="color:#22c55e">● 选中路径</span>
    <span style="color:#ef4444; margin-left:12px">● 被淘汰候选</span>
  </div>
  <div id="dpSteps"></div>
</div>

<div class="tooltip" id="tooltip"></div>

<script>
const tokens = {tokens_json};
const dpSteps = {dp_json};
const container = document.getElementById('blocks');
const tooltipEl = document.getElementById('tooltip');

const maxSize = Math.max(...tokens.map(t => t.length), 1);
const minBlockW = 8;
const maxBlockW = 120;

tokens.forEach((t, idx) => {{
  const w = Math.max(minBlockW, Math.round((t.length / maxSize) * maxBlockW));
  const cls = t.type === 'match' ? 'block match' : (t.type === 'literal_run' ? 'block literal run' : 'block literal');
  const div = document.createElement('div');
  div.className = cls;
  div.style.backgroundColor = t.color;
  div.style.width = w + 'px';
  div.title = '#' + (idx+1) + ' ' + t.type + ' [' + t.length + 'B]';
  div.dataset.idx = idx;
  container.appendChild(div);
}});

container.addEventListener('mouseenter', function(e) {{
  if (!e.target.classList.contains('block')) return;
  const idx = parseInt(e.target.dataset.idx);
  const t = tokens[idx];
  const typeLabel = t.type === 'match' ? 'MATCH' : (t.type === 'literal_run' ? 'LITERAL_RUN' : 'LITERAL');
  let extra = '';
  if (t.type === 'match') {{
    extra = '<div style="margin-top:6px;padding-top:6px;border-top:1px solid #334155;color:#94a3b8;font-size:11px">回退偏移: <span style="color:#f1f5f9;font-weight:600">' + t.offset + '</span></div>';
  }}
  tooltipEl.innerHTML =
    '<div><span style="color:#94a3b8">类型:</span> <span style="color:#38bdf8;font-weight:700">' + typeLabel + '</span></div>' +
    '<div><span style="color:#94a3b8">字节偏移:</span> <span style="color:#f1f5f9;font-weight:600;font-family:Consolas,monospace">' + t.byte_start + '</span></div>' +
    '<div><span style="color:#94a3b8">字节长度:</span> <span style="color:#f1f5f9;font-weight:600">' + t.byte_length + ' B</span></div>' +
    '<div><span style="color:#94a3b8">编码大小:</span> <span style="color:#f1f5f9;font-weight:600">' + t.comp_str + ' B</span></div>' +
    '<div><span style="color:#94a3b8">压缩率:</span> <span style="color:' + t.color + ';font-weight:700">' + (t.ratio * 100).toFixed(1) + '%</span></div>' +
    (t.hf_bits > 0 ? '<div style="margin-top:6px;padding-top:6px;border-top:1px solid #334155;color:#94a3b8;font-size:11px">Huffman: <span style="color:#a78bfa;font-weight:600">' + t.hf_bits + ' bit | ' + t.hf_detail + '</span></div>' : '') +
    extra;
  tooltipEl.classList.add('visible');
}}, true);

container.addEventListener('mousemove', function(e) {{
  if (!e.target.classList.contains('block')) return;
  tooltipEl.style.left = Math.min(e.clientX + 14, window.innerWidth - 280) + 'px';
  tooltipEl.style.top = Math.min(e.clientY + 14, window.innerHeight - 180) + 'px';
}}, true);

container.addEventListener('mouseleave', function(e) {{
  if (!e.target.classList.contains('block')) return;
  tooltipEl.classList.remove('visible');
}}, true);

const dpContainer = document.getElementById('dpSteps');
dpSteps.forEach((step, idx) => {{
  const div = document.createElement('div');
  div.className = 'dp-step';
  let candHtml = '';
  step.candidates.forEach(c => {{
    const cls = c.is_chosen ? 'dp-cand chosen' : 'dp-cand rejected';
    const tag = c.is_chosen ? '<span class="tag">SELECTED</span>' : '<span class="tag">REJECTED</span>';
    const typeStr = c.offset > 0 ? 'MATCH(off=' + c.offset + ', len=' + (c.length+1) + ')' : 'LITERAL';
    candHtml += '<div class="' + cls + '">' + tag + ' <span>' + typeStr + '</span> <span style="color:#94a3b8">next=0x' + c.next_byte.toString(16).padStart(2,'0') + '</span></div>';
  }});
  div.innerHTML =
    '<div class="dp-step-header"><span class="dp-pos">位置 ' + step.position + '</span><span class="dp-count">最优 token 数: ' + step.best_token_count + ' | 候选: ' + step.candidates.length + '</span></div>' +
    '<div class="dp-candidates">' + candHtml + '</div>';
  dpContainer.appendChild(div);
}});

const tbody = document.getElementById('tokenTable');
tokens.forEach((t, idx) => {{
  const typeClass = t.type === 'match' ? 'type-match' : (t.type === 'literal_run' ? 'type-run' : 'type-literal');
  const ratioClass = t.ratio <= 0.3 ? 'ratio-good' : 'ratio-bad';
  const typeLabel = t.type === 'match' ? 'MATCH' : (t.type === 'literal_run' ? 'LITERAL_RUN' : 'LITERAL');
  const offStr = t.type === 'match' ? String(t.offset) : '-';
  const tr = document.createElement('tr');
  tr.innerHTML =
    '<td>' + (idx+1) + '</td>' +
    '<td class="' + typeClass + '">' + typeLabel + '</td>' +
    '<td>' + t.start + '</td>' +
    '<td>' + t.length + '</td>' +
    '<td>' + t.comp_str + '</td>' +
    '<td class="' + ratioClass + '">' + (t.ratio * 100).toFixed(2) + '%</td>' +
    '<td>' + offStr + '</td>';
  tbody.appendChild(tr);
}});

document.querySelectorAll('.tab').forEach(tab => {{
  tab.addEventListener('click', () => {{
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
    tab.classList.add('active');
    document.getElementById(tab.dataset.panel).classList.add('active');
  }});
}});
</script>
</body>
</html>"""


def _lz_slider_view_html(raw_text: str, tokens_json: str, stats_html: str,
                         filename: str, algorithm: str,
                         original_size: int, compressed_size: int, time_ms: float) -> str:
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
    from gui.core.theme import ThemeManager
t = ThemeManager.get()
    _lz_extra = (
    f".lz-step {{ background: {t.bg_surface}; border-radius: 10px; padding: 20px 24px; margin-bottom: 16px; border: 1px solid {t.border}; }}\n"
    f".lz-step-header {{ display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; }}\n"
    f".step-num {{ font-size: 15px; font-weight: 700; color: {t.accent}; background: rgba(56,189,248,0.1); padding: 4px 12px; border-radius: 20px; }}\n"
    f".step-type {{ font-size: 13px; font-weight: 600; padding: 3px 10px; border-radius: 12px; }}\n"
    f".step-match {{ background: rgba(34,197,94,0.15); color: #22c55e; }}\n"
    f".step-literal {{ background: rgba(251,191,36,0.15); color: #eab308; }}\n"
    f".step-eob {{ background: rgba(239,68,68,0.15); color: #ef4444; }}\n"
    f".lz-detail {{ display: grid; grid-template-columns: auto 1fr; gap: 6px 16px; font-size: 13px; }}\n"
    f".lz-label {{ color: {t.text_secondary}; text-align: right; }}\n"
    f".lz-value {{ color: {t.text_primary}; font-weight: 500; font-family: Consolas,monospace; }}\n"
    f".lz-value.highlight {{ color: #22c55e; }}\n"
    f".lz-value.warn {{ color: #ef4444; }}\n"
    f".lz-preview {{ background: {t.bg_elevated}; border-radius: 6px; padding: 10px 14px; margin-top: 10px; font-family: Consolas,monospace; font-size: 12px; overflow-x: auto; border: 1px solid {t.border}; white-space: pre-wrap; word-break: break-all; }}\n"
    f".lz-preview .match {{ color: #22c55e; font-weight: 600; }}\n"
    f".lz-preview .literal {{ color: #eab308; }}\n"
    f".lz-preview .cursor {{ color: #38bdf8; font-weight: 700; }}\n"
    f".lz-summary {{ background: {t.bg_elevated}; border-radius: 10px; padding: 20px 24px; margin-top: 24px; border: 1px solid {t.border_dark}; }}\n"
    f".lz-summary h2 {{ font-size: 17px; margin-bottom: 12px; color: {t.text_primary}; }}\n"
    f".lz-stats-grid {{ display: grid; grid-template-columns: repeat(auto-fit,minmax(140px,1fr)); gap: 12px; }}\n"
    f".lz-stat-card {{ background: {t.bg_surface}; border-radius: 8px; padding: 14px; text-align: center; border: 1px solid {t.border}; }}\n"
    f".lz-stat-card .num {{ font-size: 22px; font-weight: 700; color: {t.accent}; }}\n"
    f".lz-stat-card .label {{ font-size: 11px; color: {t.text_secondary}; margin-top: 4px; }}\n"
)
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>LZ 算法步骤可视化 - {filename}</title>
<style>
{_themed_css(_lz_extra)}
</style>
</head>
<body>
<h1>🎚️ LZ 算法步骤可视化</h1>
<div class="meta">文件: {filename} &nbsp;|&nbsp; 算法: {algorithm} &nbsp;|&nbsp; 步骤滑动模式</div>

<div class="stats">{stats_html}</div>

<div class="slider-section">
  <div class="slider-header">
    <span class="slider-title">算法执行步骤</span>
    <span class="step-info">Token <span class="step-num" id="stepNum">0</span> / <span class="total-num" id="totalNum">0</span></span>
  </div>
  <div class="slider-container">
    <button class="btn" id="btnPrev" onclick="stepBy(-1)">◀ 上一步</button>
    <input type="range" id="slider" min="0" max="0" value="0" step="1"
           oninput="onSliderChange(this.value)">
    <button class="btn" id="btnNext" onclick="stepBy(1)">下一步 ▶</button>
    <button class="btn" id="btnPlay" onclick="togglePlay()">▶ 自动播放</button>
  </div>
</div>

<div class="text-display" id="textView"></div>

<div class="token-detail" id="detailPanel">
  <div class="detail-grid" id="detailGrid"></div>
</div>

<div class="color-legend">
  <div class="legend-item"><div class="legend-dot" style="background:#1e293b;border:1px solid #475569"></div> 已处理区域（搜索窗口）</div>
  <div class="legend-item"><div class="legend-dot" style="background:#38bdf8"></div> 当前 Token（匹配/字面量）</div>
  <div class="legend-item"><div class="legend-dot" style="background:#334155"></div> 待处理区域（Lookahead）</div>
</div>

<div class="tooltip" id="tooltip"></div>

<script>
const tokens = {tokens_json};
const rawText = JSON.parse({json.dumps(json.dumps(raw_text, ensure_ascii=False))});
const textView = document.getElementById('textView');
const tooltip = document.getElementById('tooltip');
const slider = document.getElementById('slider');
const stepNumEl = document.getElementById('stepNum');
const totalNumEl = document.getElementById('totalNum');
const detailGrid = document.getElementById('detailGrid');
const btnPlay = document.getElementById('btnPlay');

let currentStep = 0;
let isPlaying = false;
let playTimer = null;

function getTokenEndPos(idx) {{
  if (idx < 0) return 0;
  if (idx >= tokens.length) return rawText.length;
  let pos = 0;
  for (let i = 0; i <= idx; i++) pos += tokens[i].length;
  return pos;
}}

function render() {{
  const t = tokens[currentStep];
  const prevEnd = getTokenEndPos(currentStep - 1);
  const currEnd = getTokenEndPos(currentStep);

  let html = '';
  if (prevEnd > 0) {{
    html += '<span class="zone-processed">' + _esc(rawText.substring(0, prevEnd)) + '</span>';
  }}
  if (t) {{
    const chunk = rawText.substring(prevEnd, currEnd);
    html += '<span class="zone-current" data-idx="' + currentStep +
            '" style="background:' + t.color + ';color:#0f172a;padding:1px 3px;border-radius:3px">' +
            _esc(chunk) + '</span>';
  }}
  if (currEnd < rawText.length) {{
    html += '<span class="zone-pending">' + _esc(rawText.substring(currEnd)) + '</span>';
  }}
  textView.innerHTML = html;
  stepNumEl.textContent = currentStep + 1;

  let detailHtml = '';
  if (t) {{
    const typeLabel = t.type === 'match' ? 'MATCH' : (t.type === 'literal_run' ? 'LITERAL_RUN' : 'LITERAL');
    const ratioPct = (t.ratio * 100).toFixed(1);
    detailHtml +=
      '<div class="detail-item"><div class="detail-label">Token 类型</div>' +
      '<div class="detail-value" style="color:' + (t.type === 'match' ? '#22c55e' : '#fbbf24') + '">' + typeLabel + '</div></div>' +
      '<div class="detail-item"><div class="detail-label">字符位置</div>' +
      '<div class="detail-value">' + t.start + ' ~ ' + (t.start + t.length - 1) + '</div></div>' +
      '<div class="detail-item"><div class="detail-label">字节位置</div>' +
      '<div class="detail-value">' + t.byte_start + ' ~ ' + (t.byte_start + t.byte_length - 1) + '</div></div>' +
      '<div class="detail-item"><div class="detail-label">长度</div>' +
      '<div class="detail-value">' + t.length + ' 字符 / ' + t.byte_length + ' B</div></div>' +
      '<div class="detail-item"><div class="detail-label">编码大小</div>' +
      '<div class="detail-value">' + t.comp_str + ' B</div></div>' +
      '<div class="detail-item"><div class="detail-label">压缩率</div>' +
      '<div class="detail-value" style="color:' + t.color + '">' + ratioPct + '%</div></div>';
    if (t.hf_bits > 0) {{
      detailHtml += '<div class="detail-item"><div class="detail-label">Huffman</div>' +
                    '<div class="detail-value" style="color:#a78bfa;font-size:13px">' + t.hf_bits + ' bit</div></div>' +
                    '<div class="detail-item" style="grid-column:1/-1"><div class="detail-label">编码详情</div>' +
                    '<div class="detail-value" style="font-size:12px;color:#94a3b8">' + t.hf_detail + '</div></div>';
    }}
    if (t.type === 'match') {{
      detailHtml += '<div class="detail-item"><div class="detail-label">回退偏移</div>' +
                    '<div class="detail-value" style="color:#38bdf8">' + t.offset + ' B</div></div>';
    }}
  }} else {{
    detailHtml = '<div class="detail-item" style="grid-column:1/-1;text-align:center;color:#64748b">' +
                 '完成 — 所有 Token 已处理</div>';
  }}
  detailGrid.innerHTML = detailHtml;

  btnPrev.disabled = currentStep <= 0;
  document.getElementById('btnNext').disabled = currentStep >= tokens.length - 1;
}}

function _esc(s) {{
  const d = document.createElement('div');
  d.textContent = s;
  return d.innerHTML;
}}

function onSliderChange(val) {{
  currentStep = parseInt(val);
  render();
}}

function stepBy(delta) {{
  currentStep = Math.max(0, Math.min(tokens.length - 1, currentStep + delta));
  slider.value = currentStep;
  render();
}}

function togglePlay() {{
  isPlaying = !isPlaying;
  if (isPlaying) {{
    btnPlay.textContent = '⏸ 暂停';
    btnPlay.classList.add('active');
    playTimer = setInterval(() => {{
      if (currentStep >= tokens.length - 1) {{
        togglePlay();
        return;
      }}
      stepBy(1);
    }}, 800);
  }} else {{
    btnPlay.textContent = '▶ 自动播放';
    btnPlay.classList.remove('active');
    clearInterval(playTimer);
  }}
}}

document.addEventListener('keydown', function(e) {{
  if (e.key === 'ArrowLeft') stepBy(-1);
  else if (e.key === 'ArrowRight') stepBy(1);
  else if (e.key === ' ') {{ e.preventDefault(); togglePlay(); }}
}});

slider.max = tokens.length - 1;
totalNumEl.textContent = tokens.length;
render();
</script>
</body>
</html>"""


def generate_token_heatmap(
    raw_data: bytes,
    compressed_data: bytes,
    algorithm: AlgorithmType,
    filename: str = "unknown",
    original_size: int = 0,
    time_ms: float = 0.0,
) -> str | None:
    logger.info(
        "[token_heatmap] generating for %s, algo=%s, raw=%d, comp=%d",
        filename, algorithm.value, len(raw_data), len(compressed_data),
    )

    if not can_parse(algorithm):
        logger.warning("[token_heatmap] algorithm %s not supported for token parsing", algorithm.value)
        return None

    parser = get_parser(algorithm)
    if parser is None:
        return None

    pr = parser.parse(compressed_data, raw_data)
    logger.info(
        "[token_heatmap] parsed %d tokens (orig=%d, payload=%d)",
        len(pr.tokens), pr.original_size, pr.compressed_payload_size,
    )

    ext = Path(filename).suffix.lower() if "." in filename else ""
    text_content = _try_decode_text(raw_data) if _is_text_like(ext) else None

    tokens_json = _build_token_json(pr.tokens, text_content)
    stats_html = _stats_html(pr, filename, algorithm.value, original_size, len(compressed_data), time_ms)

    if pr.dp_steps is not None and len(pr.dp_steps) > 0:
        logger.info("[token_heatmap] using DP visualization mode for %s (dp_steps=%d)", filename, len(pr.dp_steps))
        dp_json = json.dumps(pr.dp_steps, ensure_ascii=False)
        if text_content is not None:
            return _dp_text_view_html(
                text_content, tokens_json, dp_json, stats_html,
                filename, algorithm.value, original_size, len(compressed_data), time_ms,
            )
        return _dp_block_view_html(
            tokens_json, dp_json, stats_html,
            filename, algorithm.value, original_size, len(compressed_data), time_ms,
        )

    if text_content is not None:
        logger.info("[token_heatmap] using text highlight mode for %s", filename)
        _LZ_ALGOS = {AlgorithmType.LZSS, AlgorithmType.LZMINE}
        if algorithm in _LZ_ALGOS and len(pr.tokens) > 0:
            logger.info("[token_heatmap] using LZ slider view for %s (%d tokens)", filename, len(pr.tokens))
            return _lz_slider_view_html(
                text_content, tokens_json, stats_html,
                filename, algorithm.value, original_size, len(compressed_data), time_ms,
            )
        return _text_view_html(
            text_content, tokens_json, stats_html,
            filename, algorithm.value, original_size, len(compressed_data), time_ms,
        )

    logger.info("[token_heatmap] using block view mode for %s", filename)
    return _block_view_html(
        tokens_json, stats_html,
        filename, algorithm.value, original_size, len(compressed_data), time_ms,
    )


def open_token_heatmap_html(html: str) -> None:
    tmp = Path(tempfile.mktemp(suffix=".html"))
    tmp.write_text(html, encoding="utf-8")
    logger.info("[token_heatmap] wrote HTML to %s, opening in browser", tmp)
    webbrowser.open(tmp.as_uri())
