from __future__ import annotations

import json
import logging
import tempfile
import webbrowser
from dataclasses import dataclass
from pathlib import Path

from gui.models import NETWORK_PROFILES, CompressionStatus, FolderRecord, compressed_payload_size

logger = logging.getLogger(__name__)


def _format_time(seconds: float) -> str:
    if seconds < 0.001:
        return f"{seconds * 1_000_000:.0f} \u03bcs"
    if seconds < 1:
        return f"{seconds * 1000:.1f} ms"
    return f"{seconds:.2f} s"


@dataclass(frozen=True)
class NetworkSimInput:
    original_size: int
    compressed_size: int
    compression_time_ms: float
    label: str
    algorithm: str = "mixed"
    scope_note: str = ""


def folder_transfer_totals(record: FolderRecord) -> NetworkSimInput | None:
    """Aggregate DONE children for whole-site transfer simulation."""
    record.ensure_files_loaded()
    original = 0
    compressed = 0
    time_ms = 0.0
    done_count = 0
    algos: set[str] = set()
    for f in record.files:
        if f.status != CompressionStatus.DONE:
            continue
        if getattr(f, "is_stored", False):
            payload = f.size
        else:
            payload = compressed_payload_size(f)
            if payload <= 0:
                continue
        original += f.size
        compressed += payload
        time_ms += float(f.compression_time_ms or 0)
        done_count += 1
        algos.add(getattr(f.algorithm, "value", str(f.algorithm)))
    if done_count == 0 or original <= 0:
        return None
    algo = ", ".join(sorted(algos)) if algos else "mixed"
    note = f"\u7f51\u9875\u6587\u4ef6\u5939\u6c47\u603b \u00b7 {done_count} \u4e2a\u5df2\u538b\u7f29\u8d44\u6e90"
    return NetworkSimInput(
        original_size=original,
        compressed_size=compressed,
        compression_time_ms=time_ms,
        label=record.name,
        algorithm=algo,
        scope_note=note,
    )


def compute_network_sim_profiles(
    original_size: int,
    compressed_size: int,
    compression_time_ms: float,
) -> list[dict]:
    profiles_data: list[dict] = []
    compress_s = compression_time_ms / 1000.0
    for name, profile in NETWORK_PROFILES.items():
        t_raw = profile.transfer_time(original_size)
        t_comp = profile.transfer_time(compressed_size)
        saving = t_raw - t_comp
        net_saving = saving - compress_s
        saving_pct = (saving / t_raw * 100.0) if t_raw > 0 else 0.0
        profiles_data.append({
            "name": name,
            "bandwidth_mbps": profile.bandwidth_bps / 1_000_000,
            "latency_ms": profile.latency_ms,
            "t_raw": t_raw,
            "t_raw_fmt": _format_time(t_raw),
            "t_comp": t_comp,
            "t_comp_fmt": _format_time(t_comp),
            "saving": saving,
            "saving_fmt": _format_time(saving),
            "saving_pct": round(saving_pct, 1),
            "net_saving": net_saving,
            "net_saving_fmt": _format_time(abs(net_saving)),
            "worth_it": net_saving > 0,
        })
    return profiles_data


def build_network_analysis_text(
    inp: NetworkSimInput,
    profiles_data: list[dict],
) -> str:
    worth_count = sum(1 for p in profiles_data if p["worth_it"])
    ratio_str = (
        f"{inp.compressed_size / inp.original_size * 100:.1f}"
        if inp.original_size
        else "0"
    )
    saving_bytes = inp.original_size - inp.compressed_size
    scope = f"{inp.scope_note}\n" if inp.scope_note else ""
    text = (
        f"{scope}"
        f"\u5bf9\u8c61: {inp.label}  |  \u7b97\u6cd5: {inp.algorithm}\n"
        f"\u539f\u59cb {inp.original_size:,} B \u2192 \u538b\u7f29\u540e {inp.compressed_size:,} B"
        f"\uff08\u4f53\u79ef\u6bd4 {ratio_str}%\uff09\uff0c\u8282\u7701 {saving_bytes:,} B\uff1b"
        f"\u538b\u7f29\u8017\u65f6 {inp.compression_time_ms:.1f} ms\u3002\n"
        f"\u5728 {len(profiles_data)} \u79cd\u7f51\u7edc\u73af\u5883\u4e2d\uff0c"
        f"\u6709 {worth_count} \u79cd\u51c0\u8282\u7701 > 0\uff08\u4f20\u8f93\u8282\u7701\u5927\u4e8e\u538b\u7f29\u8017\u65f6\uff09\u3002"
    )
    if worth_count < len(profiles_data):
        text += (
            "\n\u5728\u9ad8\u901f\u7f51\u7edc\uff08\u5982 Ethernet\uff09\u4e0b\uff0c"
            "\u5c0f\u4f53\u79ef\u6216\u5df2\u5f88\u5feb\u4f20\u5b8c\u65f6\uff0c"
            "\u538b\u7f29 CPU \u53ef\u80fd\u62b5\u6d88\u4f20\u8f93\u6536\u76ca\u3002"
        )
    else:
        text += (
            "\n\u6240\u6709\u9884\u8bbe\u7f51\u7edc\u73af\u5883\u4e0b\uff0c"
            "\u538b\u7f29\u540e\u4f20\u8f93\u5747\u4f18\u4e8e\u76f4\u63a5\u4f20\u539f\u6570\u636e\uff08\u5df2\u6263\u538b\u7f29\u8017\u65f6\uff09\u3002"
        )
    return text


def _report_table_rows(profiles_data: list[dict]) -> str:
    rows = []
    for p in profiles_data:
        verdict = "\u503c\u5f97" if p["worth_it"] else "\u4e0d\u503c\u5f97"
        net_sign = "+" if p["net_saving"] > 0 else "-"
        rows.append(
            "<tr>"
            f"<td>{p['name']}</td>"
            f"<td>{p['bandwidth_mbps']:.1f} Mbps</td>"
            f"<td>{p['latency_ms']:.0f} ms</td>"
            f"<td>{p['t_raw_fmt']}</td>"
            f"<td>{p['t_comp_fmt']}</td>"
            f"<td>{p['saving_fmt']}</td>"
            f"<td>{p['saving_pct']:.1f}%</td>"
            f"<td>{net_sign}{p['net_saving_fmt']}</td>"
            f"<td>{verdict}</td>"
            "</tr>"
        )
    return "\n".join(rows)


def generate_network_sim(inp: NetworkSimInput) -> str:
    logger.info(
        "[network_sim] generating for %s, algo=%s, orig=%d, comp=%d",
        inp.label,
        inp.algorithm,
        inp.original_size,
        inp.compressed_size,
    )
    profiles_data = compute_network_sim_profiles(
        inp.original_size, inp.compressed_size, inp.compression_time_ms
    )
    profiles_json = json.dumps(
        [
            {
                **p,
                "t_raw": round(p["t_raw"], 6),
                "t_comp": round(p["t_comp"], 6),
                "saving": round(p["saving"], 6),
                "net_saving": round(p["net_saving"], 6),
            }
            for p in profiles_data
        ],
        ensure_ascii=False,
    )
    analysis = build_network_analysis_text(inp, profiles_data).replace("\n", "<br>")
    scope_line = f"{inp.scope_note}<br>" if inp.scope_note else ""
    table_rows = _report_table_rows(profiles_data)

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>\u4f20\u8f93\u6548\u7387\u5bf9\u6bd4\u62a5\u544a - {inp.label}</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ font-family: "Microsoft YaHei", "Segoe UI", sans-serif; background: #0f172a; color: #e2e8f0; padding: 24px; }}
h1 {{ font-size: 20px; margin-bottom: 8px; }}
h2 {{ font-size: 16px; margin: 24px 0 12px; }}
.meta {{ color: #94a3b8; font-size: 13px; margin-bottom: 16px; line-height: 1.7; }}
.report-table {{ width: 100%; max-width: 1100px; border-collapse: collapse; margin-bottom: 24px; font-size: 13px; }}
.report-table th, .report-table td {{ border: 1px solid #334155; padding: 8px 10px; text-align: center; }}
.report-table th {{ background: #1e293b; color: #94a3b8; font-weight: 600; }}
.report-table tr:nth-child(even) td {{ background: #1e293b55; }}
.cards {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 16px; max-width: 1100px; }}
.card {{ background: #1e293b; border-radius: 10px; padding: 20px; border: 1px solid #334155; }}
.card-header {{ display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }}
.card-title {{ font-size: 16px; font-weight: 700; }}
.card-badge {{ font-size: 11px; padding: 3px 10px; border-radius: 12px; font-weight: 600; }}
.badge-good {{ background: #166534; color: #86efac; }}
.badge-bad {{ background: #7f1d1d; color: #fca5a5; }}
.race {{ margin-bottom: 12px; }}
.race-label {{ font-size: 11px; color: #94a3b8; margin-bottom: 4px; }}
.race-track {{ height: 22px; background: #0f172a; border-radius: 4px; overflow: hidden; }}
.race-fill {{ height: 100%; border-radius: 4px; display: flex; align-items: center; padding-left: 8px; font-size: 11px; font-weight: 600; color: #0f172a; }}
.race-fill.raw {{ background: #ef4444; }}
.race-fill.comp {{ background: #22c55e; }}
.saving-row {{ display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-top: 1px solid #334155; margin-top: 8px; }}
.saving-label {{ font-size: 12px; color: #94a3b8; }}
.saving-value {{ font-size: 14px; font-weight: 700; }}
.saving-value.positive {{ color: #22c55e; }}
.saving-value.negative {{ color: #ef4444; }}
.bandwidth {{ font-size: 11px; color: #64748b; margin-top: 2px; }}
.summary {{ margin-top: 28px; background: #1e293b; border-radius: 10px; padding: 20px; max-width: 1100px; }}
.summary p {{ font-size: 13px; color: #94a3b8; line-height: 1.8; }}
@media print {{ body {{ background: #fff; color: #111; }} }}
</style>
</head>
<body>
<h1>\U0001f310 \u4f20\u8f93\u6548\u7387\u5bf9\u6bd4\u62a5\u544a\uff08F8 \u7f51\u7edc\u4f20\u8f93\u6a21\u62df\uff09</h1>
<div class="meta">
  {scope_line}
  \u5bf9\u8c61: {inp.label} &nbsp;|&nbsp; \u7b97\u6cd5: {inp.algorithm} &nbsp;|&nbsp;
  \u539f\u59cb: {inp.original_size:,} B &nbsp;|&nbsp; \u538b\u7f29\u540e: {inp.compressed_size:,} B &nbsp;|&nbsp;
  \u538b\u7f29\u8017\u65f6: {inp.compression_time_ms:.1f} ms
</div>
<h2>\U0001f4cb \u5bf9\u6bd4\u6570\u636e\u8868</h2>
<table class="report-table">
<thead><tr>
<th>\u7f51\u7edc</th><th>\u5e26\u5bbd</th><th>\u5ef6\u8fdf</th><th>\u4f20\u539f\u6587\u4ef6</th><th>\u4f20\u538b\u7f29\u540e</th>
<th>\u4f20\u8f93\u8282\u7701</th><th>\u63d0\u5347%</th><th>\u51c0\u8282\u7701</th><th>\u7ed3\u8bba</th>
</tr></thead>
<tbody>
{table_rows}
</tbody>
</table>
<h2>\U0001f4ca \u53ef\u89c6\u5316\u5bf9\u6bd4</h2>
<div class="cards" id="cards"></div>
<div class="summary">
  <h2>\U0001f4a1 \u5206\u6790</h2>
  <p>{analysis}</p>
</div>
<script>
const profiles = {profiles_json};
const cards = document.getElementById('cards');
const maxTime = Math.max(...profiles.map(p => p.t_raw), 0.001);
profiles.forEach(p => {{
  const rawPct = (p.t_raw / maxTime) * 100;
  const compPct = (p.t_comp / maxTime) * 100;
  const card = document.createElement('div');
  card.className = 'card';
  card.innerHTML =
    '<div class="card-header">' +
      '<div class="card-title">' + p.name + '</div>' +
      '<div class="card-badge ' + (p.worth_it ? 'badge-good' : 'badge-bad') + '">' +
        (p.worth_it ? '\u2713 \u503c\u5f97\u538b\u7f29' : '\u2717 \u4e0d\u503c\u5f97') +
      '</div>' +
    '</div>' +
    '<div class="bandwidth">\\u2193 ' + p.bandwidth_mbps + ' Mbps | \u5ef6\u8fdf ' + p.latency_ms + ' ms</div>' +
    '<div class="race"><div class="race-label">\u539f\u59cb\u4f20\u8f93</div>' +
      '<div class="race-track"><div class="race-fill raw" style="width:' + rawPct + '%">' + p.t_raw_fmt + '</div></div></div>' +
    '<div class="race"><div class="race-label">\u538b\u7f29\u540e\u4f20\u8f93</div>' +
      '<div class="race-track"><div class="race-fill comp" style="width:' + compPct + '%">' + p.t_comp_fmt + '</div></div></div>' +
    '<div class="saving-row"><span class="saving-label">\u4f20\u8f93\u8282\u7701</span>' +
      '<span class="saving-value positive">' + p.saving_fmt + ' (' + p.saving_pct + '%)</span></div>' +
    '<div class="saving-row"><span class="saving-label">\u51c0\u8282\u7701\uff08\u6263\u538b\u7f29\u8017\u65f6\uff09</span>' +
      '<span class="saving-value ' + (p.net_saving > 0 ? 'positive' : 'negative') + '">' +
        (p.net_saving > 0 ? '+' : '-') + p.net_saving_fmt + '</span></div>';
  cards.appendChild(card);
}});
</script>
</body>
</html>"""


def open_network_sim_html(html: str) -> None:
    tmp = Path(tempfile.mktemp(suffix=".html"))
    tmp.write_text(html, encoding="utf-8")
    logger.info("[network_sim] wrote HTML to %s, opening in browser", tmp)
    webbrowser.open(tmp.as_uri())
