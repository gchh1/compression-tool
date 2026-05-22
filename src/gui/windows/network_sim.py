from __future__ import annotations

import json
import logging
import tempfile
import webbrowser
from pathlib import Path

from gui.models import NETWORK_PROFILES
from gui.config.theme import ThemeManager

logger = logging.getLogger(__name__)


def _format_time(seconds: float) -> str:
    if seconds < 0.001:
        return f"{seconds * 1_000_000:.0f} μs"
    if seconds < 1:
        return f"{seconds * 1000:.1f} ms"
    return f"{seconds:.2f} s"


def generate_network_sim(
    original_size: int,
    compressed_size: int,
    compression_time_ms: float,
    filename: str = "unknown",
    algorithm: str = "unknown",
) -> str:
    logger.info("[network_sim] generating for %s, algo=%s, orig=%d, comp=%d",
                 filename, algorithm, original_size, compressed_size)
    profiles_data = []
    for name, profile in NETWORK_PROFILES.items():
        t_raw = profile.transfer_time(original_size)
        t_comp = profile.transfer_time(compressed_size)
        saving = t_raw - t_comp
        net_saving = saving - compression_time_ms / 1000
        worth_it = net_saving > 0

        profiles_data.append({
            "name": name,
            "bandwidth_mbps": profile.bandwidth_bps / 1_000_000,
            "latency_ms": profile.latency_ms,
            "t_raw": round(t_raw, 6),
            "t_raw_fmt": _format_time(t_raw),
            "t_comp": round(t_comp, 6),
            "t_comp_fmt": _format_time(t_comp),
            "saving": round(saving, 6),
            "saving_fmt": _format_time(saving),
            "saving_pct": round(saving / t_raw * 100, 1) if t_raw > 0 else 0,
            "net_saving": round(net_saving, 6),
            "net_saving_fmt": _format_time(abs(net_saving)),
            "worth_it": worth_it,
        })

    profiles_json = json.dumps(profiles_data, ensure_ascii=False)

    t = ThemeManager.get()
    badge_good_bg = ThemeManager.resolve_hex("net_badge_good_bg")
    badge_good_fg = ThemeManager.resolve_hex("net_badge_good_fg")
    badge_bad_bg = ThemeManager.resolve_hex("net_badge_bad_bg")
    badge_bad_fg = ThemeManager.resolve_hex("net_badge_bad_fg")
    bar_raw = ThemeManager.resolve_hex("net_bar_raw")
    bar_comp = ThemeManager.resolve_hex("net_bar_compressed")
    saving_pos = ThemeManager.resolve_hex("net_saving_value")

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>网络传输模拟 - {filename}</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ font-family: "Microsoft YaHei", "Segoe UI", sans-serif; background: {t.bg_primary}; color: {t.text_primary}; padding: 24px; }}
h1 {{ font-size: 20px; margin-bottom: 8px; }}
.meta {{ color: {t.text_secondary}; font-size: 13px; margin-bottom: 24px; }}
.cards {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 16px; max-width: 900px; }}
.card {{ background: {t.bg_surface}; border-radius: 10px; padding: 20px; border: 1px solid {t.border}; }}
.card-header {{ display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }}
.card-title {{ font-size: 16px; font-weight: 700; }}
.card-badge {{ font-size: 11px; padding: 3px 10px; border-radius: 12px; font-weight: 600; }}
.badge-good {{ background: {badge_good_bg}; color: {badge_good_fg}; }}
.badge-bad {{ background: {badge_bad_bg}; color: {badge_bad_fg}; }}
.race {{ margin-bottom: 12px; }}
.race-label {{ font-size: 11px; color: {t.text_secondary}; margin-bottom: 4px; }}
.race-track {{ height: 22px; background: {t.bg_primary}; border-radius: 4px; overflow: hidden; }}
.race-fill {{ height: 100%; border-radius: 4px; display: flex; align-items: center; padding-left: 8px; font-size: 11px; font-weight: 600; color: {t.bg_primary}; transition: width 1s ease; }}
.race-fill.raw {{ background: {bar_raw}; }}
.race-fill.comp {{ background: {bar_comp}; }}
.race-time {{ font-size: 12px; color: {t.text_secondary}; margin-top: 2px; text-align: right; }}
.saving-row {{ display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-top: 1px solid {t.border}; margin-top: 8px; }}
.saving-label {{ font-size: 12px; color: {t.text_secondary}; }}
.saving-value {{ font-size: 14px; font-weight: 700; }}
.saving-value.positive {{ color: {saving_pos}; }}
.saving-value.negative {{ color: {bar_raw}; }}
.bandwidth {{ font-size: 11px; color: {t.text_muted}; margin-top: 2px; }}
.summary {{ margin-top: 28px; background: {t.bg_surface}; border-radius: 10px; padding: 20px; max-width: 900px; }}
.summary h2 {{ font-size: 16px; margin-bottom: 12px; }}
.summary p {{ font-size: 13px; color: {t.text_secondary}; line-height: 1.8; }}
.summary .highlight {{ color: {t.accent}; font-weight: 600; }}
</style>
</head>
<body>
<h1>🌐 网络传输模拟</h1>
<div class="meta">
  文件: {filename} &nbsp;|&nbsp; 算法: {algorithm} &nbsp;|&nbsp;
  原始: {original_size:,} B &nbsp;|&nbsp; 压缩后: {compressed_size:,} B &nbsp;|&nbsp;
  压缩耗时: {compression_time_ms:.1f} ms
</div>
<div class="cards" id="cards"></div>
<div class="summary">
  <h2>💡 分析</h2>
  <p id="analysis"></p>
</div>
<script>
const profiles = {profiles_json};
const originalSize = {original_size};
const compressedSize = {compressed_size};
const compressTimeMs = {compression_time_ms};
const cards = document.getElementById('cards');

let worthCount = 0;
const maxTime = Math.max(...profiles.map(p => p.t_raw));

profiles.forEach(p => {{
  const rawPct = (p.t_raw / maxTime) * 100;
  const compPct = (p.t_comp / maxTime) * 100;
  if (p.worth_it) worthCount++;

  const card = document.createElement('div');
  card.className = 'card';
  card.innerHTML =
    '<div class="card-header">' +
      '<div class="card-title">' + p.name + '</div>' +
      '<div class="card-badge ' + (p.worth_it ? 'badge-good' : 'badge-bad') + '">' +
        (p.worth_it ? '✓ 值得压缩' : '✗ 不值得') +
      '</div>' +
    '</div>' +
    '<div class="bandwidth">↓ ' + p.bandwidth_mbps + ' Mbps &nbsp;|&nbsp; 延迟 ' + p.latency_ms + ' ms</div>' +
    '<div class="race">' +
      '<div class="race-label">原始传输</div>' +
      '<div class="race-track"><div class="race-fill raw" style="width:' + rawPct + '%">' + p.t_raw_fmt + '</div></div>' +
    '</div>' +
    '<div class="race">' +
      '<div class="race-label">压缩后传输</div>' +
      '<div class="race-track"><div class="race-fill comp" style="width:' + compPct + '%">' + p.t_comp_fmt + '</div></div>' +
    '</div>' +
    '<div class="saving-row">' +
      '<span class="saving-label">传输节省</span>' +
      '<span class="saving-value positive">' + p.saving_fmt + ' (' + p.saving_pct + '%)</span>' +
    '</div>' +
    '<div class="saving-row">' +
      '<span class="saving-label">扣除压缩耗时后净节省</span>' +
      '<span class="saving-value ' + (p.net_saving > 0 ? 'positive' : 'negative') + '">' +
        (p.net_saving > 0 ? '+' : '-') + p.net_saving_fmt +
      '</span>' +
    '</div>';
  cards.appendChild(card);
}});

const analysis = document.getElementById('analysis');
const ratio = (compressedSize / originalSize * 100).toFixed(1);
const savingBytes = originalSize - compressedSize;
analysis.innerHTML =
  '使用 <span class="highlight">' + '{algorithm}' + '</span> 压缩后，文件从 <span class="highlight">' + originalSize.toLocaleString() + ' B</span> 缩小到 <span class="highlight">' + compressedSize.toLocaleString() + ' B</span>（压缩率 ' + ratio + '%），节省 <span class="highlight">' + savingBytes.toLocaleString() + ' B</span>。<br>' +
  '在 ' + profiles.length + ' 种网络环境中，有 <span class="highlight">' + worthCount + '</span> 种值得压缩（传输节省 > 压缩耗时）。<br>' +
  (worthCount < profiles.length ? '在高速网络（如 Ethernet）下，小文件的压缩耗时可能超过传输节省，此时直接传输更优。' : '所有网络环境下压缩均有收益。');
</script>
</body>
</html>"""
    return html


def open_network_sim_html(html: str) -> None:
    tmp = Path(tempfile.mktemp(suffix=".html"))
    tmp.write_text(html, encoding="utf-8")
    logger.info("[network_sim] wrote HTML to %s, opening in browser", tmp)
    webbrowser.open(tmp.as_uri())
