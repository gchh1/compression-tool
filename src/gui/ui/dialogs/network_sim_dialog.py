"""Network transfer simulation dialog."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QScrollArea,
    QGroupBox, QWidget, QProgressBar,
)

from gui.config.theme import ThemeManager


class NetworkSimDialog(QDialog):
    def __init__(self, original_size: int, compressed_size: int,
                 compression_time_ms: float, filename: str,
                 algorithm: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"网络传输模拟 - {filename}")
        self.setMinimumSize(720, 580)
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
            f"QProgressBar {{ border: 1px solid {t.border_dark}; border-radius: 4px; "
            f"background: {t.bg_surface}; text-align: center; color: {t.bg_primary}; "
            f"font-weight: 600; font-size: 11px; min-height: 22px; }}\n"
            f"QProgressBar::chunk {{ border-radius: 3px; }}\n"
        )
        from gui.models import NETWORK_PROFILES
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(12)

        header = QLabel("\U0001f310 网络传输模拟")
        header.setStyleSheet(f"font-size: 18px; font-weight: 700; color: {t.text_primary}; background: transparent;")
        layout.addWidget(header)

        meta = QLabel(
            f"文件: {filename}  |  算法: {algorithm}  |  "
            f"原始: {original_size:,} B  |  压缩后: {compressed_size:,} B  |  压缩耗时: {compression_time_ms:.1f} ms"
        )
        meta.setStyleSheet(f"font-size: 12px; color: {t.text_secondary}; background: transparent;")
        layout.addWidget(meta)

        cards_scroll = QScrollArea()
        cards_scroll.setWidgetResizable(True)
        cards_scroll.setStyleSheet(f"QScrollArea {{ border: none; background: transparent; }}")
        cards_container = QWidget()
        cards_layout = QVBoxLayout(cards_container)
        cards_layout.setSpacing(12)

        profiles_data = []
        worth_count = 0
        max_t_raw = 0.001
        for name, profile in NETWORK_PROFILES.items():
            t_raw = profile.transfer_time(original_size)
            t_comp = profile.transfer_time(compressed_size)
            saving = t_raw - t_comp
            net_saving = saving - compression_time_ms / 1000
            worth_it = net_saving > 0
            if worth_it:
                worth_count += 1
            max_t_raw = max(max_t_raw, t_raw)
            profiles_data.append({
                "name": name, "bandwidth_mbps": profile.bandwidth_bps / 1_000_000,
                "latency_ms": profile.latency_ms, "t_raw": t_raw, "t_comp": t_comp,
                "saving": saving, "net_saving": net_saving, "worth_it": worth_it,
            })

        for p in profiles_data:
            card = QGroupBox()
            card.setStyleSheet(f"QGroupBox {{ background: {t.bg_surface}; }}")
            cl = QVBoxLayout(card)
            cl.setSpacing(8)
            hdr = QHBoxLayout()
            title_lbl = QLabel(p["name"])
            title_lbl.setStyleSheet(f"font-size: 15px; font-weight: 700; color: {t.text_primary}; background: transparent;")
            hdr.addWidget(title_lbl)
            hdr.addStretch()
            badge = QLabel("\u2705 值得压缩" if p["worth_it"] else "\u2717 不值得")
            badge_bg = ThemeManager.resolve_hex("net_badge_good_bg") if p["worth_it"] else ThemeManager.resolve_hex("net_badge_bad_bg")
            badge_fg = ThemeManager.resolve_hex("net_badge_good_fg") if p["worth_it"] else ThemeManager.resolve_hex("net_badge_bad_fg")
            badge.setStyleSheet(
                f"font-size: 11px; padding: 3px 10px; border-radius: 12px; font-weight: 600; "
                f"background: {badge_bg}; color: {badge_fg};"
            )
            hdr.addWidget(badge)
            cl.addLayout(hdr)

            bw_lbl = QLabel(f"\u2193 {p['bandwidth_mbps']:.1f} Mbps  |  延迟 {p['latency_ms']:.0f} ms")
            bw_lbl.setStyleSheet(f"font-size: 11px; color: {t.text_muted}; background: transparent;")
            cl.addWidget(bw_lbl)

            raw_pct = int(min(100, (p["t_raw"] / max_t_raw) * 100))
            comp_pct = int(min(100, (p["t_comp"] / max_t_raw) * 100))

            for label_text, pct, is_comp in [("原始传输", raw_pct, False),
                                              ("压缩后传输", comp_pct, True)]:
                lbl = QLabel(label_text)
                lbl.setStyleSheet(f"font-size: 11px; color: {t.text_secondary}; background: transparent;")
                cl.addWidget(lbl)
                bar = QProgressBar()
                bar.setMaximum(100)
                bar.setValue(pct)
                bar.setTextVisible(True)
                bar.setFormat(_format_time_ns(p["t_raw"] if not is_comp else p["t_comp"]))
                c = ThemeManager.resolve_hex("net_bar_raw") if not is_comp else ThemeManager.resolve_hex("net_bar_compressed")
                bar.setStyleSheet(bar.styleSheet() +
                    f"QProgressBar::chunk {{ background: {c}; border-radius: 3px; }}")
                cl.addWidget(bar)

            saving_val = p["saving"]
            net_val = p["net_saving"]
            saving_row = QHBoxLayout()
            saving_row.addWidget(QLabel("传输节省"))
            saving_row.addStretch()
            sv = QLabel(f"+{_format_time_ns(saving_val)} ({saving_val/max_t_raw*100:.1f}%)")
            sv.setStyleSheet(f"font-size: 13px; font-weight: 700; color: {ThemeManager.resolve_hex('net_saving_value')}; background: transparent;")
            saving_row.addWidget(sv)
            cl.addLayout(saving_row)

            net_row = QHBoxLayout()
            net_row.addWidget(QLabel("净节省（扣除压缩耗时）"))
            net_row.addStretch()
            nv = QLabel(f"{'+' if net_val > 0 else '-'}{_format_time_ns(abs(net_val))}")
            nv_c = ThemeManager.resolve_hex("net_saving_value") if net_val > 0 else ThemeManager.resolve_hex("net_bar_raw")
            nv_css = f"font-size: 13px; font-weight: 700; color: {nv_c}; background: transparent;"
            nv.setStyleSheet(nv_css)
            net_row.addWidget(nv)
            cl.addLayout(net_row)

            cards_layout.addWidget(card)

        cards_scroll.setWidget(cards_container)
        layout.addWidget(cards_scroll, stretch=1)

        analysis_group = QGroupBox("\U0001f4a1 分析")
        al = QVBoxLayout(analysis_group)
        ratio_str = f"{compressed_size / original_size * 100:.1f}" if original_size else "0"
        saving_bytes = original_size - compressed_size
        analysis_text = (
            f"使用 {algorithm} 压缩后，文件从 {original_size:,} B 缩小到 {compressed_size:,} B"
            f"（压缩率 {ratio_str}%），节省 {saving_bytes:,} B。\n"
            f"在 {len(profiles_data)} 种网络环境中，有 {worth_count} 种值得压缩（传输节省 > 压缩耗时）。"
        )
        if worth_count < len(profiles_data):
            analysis_text += "\n在高速网络（如 Ethernet）下，小文件的压缩耗时可能超过传输节省，此时直接传输更优。"
        else:
            analysis_text += "\n所有网络环境下压缩均有收益。"
        analysis_lbl = QLabel(analysis_text)
        analysis_lbl.setStyleSheet(f"font-size: 13px; color: {t.text_secondary}; background: transparent; line-height: 1.6;")
        analysis_lbl.setWordWrap(True)
        al.addWidget(analysis_lbl)
        layout.addWidget(analysis_group)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        close_btn = QPushButton("关闭")
        close_btn.clicked.connect(self.accept)
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)



def _format_time_ns(seconds: float) -> str:
    if seconds < 0.001:
        return f"{seconds * 1_000_000:.0f} \u03bcs"
    if seconds < 1:
        return f"{seconds * 1000:.1f} ms"
    return f"{seconds:.2f} s"
