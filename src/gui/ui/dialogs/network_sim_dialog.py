"""Network transfer simulation: formula estimate + real throttled transfer benchmark."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QDialog,
    QFileDialog,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from gui.config.theme import ThemeManager
from gui.engine.network_transfer import NetworkBenchmarkTarget, ProfileBenchmarkResult
from gui.ui.network_transfer_worker import NetworkTransferWorker
from gui.windows.network_sim import (
    NetworkSimInput,
    build_network_analysis_text,
    compute_network_sim_profiles,
    generate_network_sim,
    open_network_sim_html,
)


class NetworkSimDialog(QDialog):
    def __init__(
        self,
        original_size: int,
        compressed_size: int,
        compression_time_ms: float,
        filename: str,
        algorithm: str,
        scope_note: str = "",
        benchmark_target: NetworkBenchmarkTarget | None = None,
        parent=None,
    ):
        super().__init__(parent)
        self._inp = NetworkSimInput(
            original_size=original_size,
            compressed_size=compressed_size,
            compression_time_ms=compression_time_ms,
            label=filename,
            algorithm=algorithm,
            scope_note=scope_note,
        )
        self._benchmark_target = benchmark_target
        self._measured: list[ProfileBenchmarkResult] | None = None
        self._worker: NetworkTransferWorker | None = None
        self._profiles_data = compute_network_sim_profiles(
            original_size, compressed_size, compression_time_ms
        )
        self._t = ThemeManager.get()
        self.setWindowTitle(f"\u7f51\u7edc\u4f20\u8f93\u6a21\u62df - {filename}")
        self.setMinimumSize(760, 620)
        self._apply_style()
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(20, 16, 20, 16)
        self._layout.setSpacing(12)

        header = QLabel("\U0001f310 \u7f51\u7edc\u4f20\u8f93\u6a21\u62df")
        header.setStyleSheet(
            f"font-size: 18px; font-weight: 700; color: {self._t.text_primary}; background: transparent;"
        )
        self._layout.addWidget(header)

        flow = QLabel(
            "\u771f\u5b9e\u7f51\u9875\u6d41\u7a0b\uff1a\u670d\u52a1\u5668\u6301\u6709\u538b\u7f29\u4f53 "
            "\u2192 \u6309\u9009\u5b9a\u5e26\u5bbd\u9650\u901f\u53d1\u9001 "
            "\u2192 \u5ba2\u6237\u7aef\u7528\u672c\u673a\u5f15\u64ce\u89e3\u538b\u3002"
            "\u70b9\u300c\u5f00\u59cb\u771f\u5b9e\u4f20\u8f93\u6d4b\u8bd5\u300d\u540e\u4f1a\u5b9e\u9645\u8017\u65f6\uff08\u4f8b\u5982 4G \u4f20\u5927\u6587\u4ef6\u53ef\u80fd\u9700\u6570\u5341\u79d2\uff09\u3002"
        )
        flow.setWordWrap(True)
        flow.setStyleSheet(
            f"font-size: 12px; color: {self._t.text_secondary}; background: transparent; line-height: 1.5;"
        )
        self._layout.addWidget(flow)

        scope = scope_note + "  |  " if scope_note else ""
        self._meta = QLabel(
            f"{scope}"
            f"\u5bf9\u8c61: {filename}  |  \u7b97\u6cd5: {algorithm}  |  "
            f"\u539f\u59cb: {original_size:,} B  |  \u538b\u7f29\u540e: {compressed_size:,} B  |  "
            f"\u538b\u7f29\u8017\u65f6: {compression_time_ms:.1f} ms"
        )
        self._meta.setWordWrap(True)
        self._meta.setStyleSheet(
            f"font-size: 12px; color: {self._t.text_secondary}; background: transparent;"
        )
        self._layout.addWidget(self._meta)

        self._progress = QProgressBar()
        self._progress.setVisible(False)
        self._progress.setTextVisible(True)
        self._layout.addWidget(self._progress)

        self._cards_scroll = QScrollArea()
        self._cards_scroll.setWidgetResizable(True)
        self._cards_scroll.setStyleSheet("QScrollArea { border: none; background: transparent; }")
        self._cards_container = QWidget()
        self._cards_layout = QVBoxLayout(self._cards_container)
        self._cards_layout.setSpacing(12)
        self._cards_scroll.setWidget(self._cards_container)
        self._layout.addWidget(self._cards_scroll, stretch=1)

        self._analysis_group = QGroupBox("\U0001f4a1 \u5206\u6790")
        al = QVBoxLayout(self._analysis_group)
        self._analysis_lbl = QLabel(build_network_analysis_text(self._inp, self._profiles_data))
        self._analysis_lbl.setWordWrap(True)
        self._analysis_lbl.setStyleSheet(
            f"font-size: 13px; color: {self._t.text_secondary}; background: transparent; line-height: 1.6;"
        )
        al.addWidget(self._analysis_lbl)
        self._layout.addWidget(self._analysis_group)

        btn_layout = QHBoxLayout()
        self._run_btn = QPushButton("\u5f00\u59cb\u771f\u5b9e\u4f20\u8f93\u6d4b\u8bd5")
        self._run_btn.setEnabled(benchmark_target is not None)
        self._run_btn.clicked.connect(self._on_run_benchmark)
        btn_layout.addWidget(self._run_btn)
        self._cancel_btn = QPushButton("\u53d6\u6d88\u6d4b\u8bd5")
        self._cancel_btn.setVisible(False)
        self._cancel_btn.clicked.connect(self._on_cancel_benchmark)
        btn_layout.addWidget(self._cancel_btn)
        export_btn = QPushButton("\u5bfc\u51fa HTML \u62a5\u544a")
        export_btn.clicked.connect(self._on_export_html)
        btn_layout.addWidget(export_btn)
        browser_btn = QPushButton("\u5728\u6d4f\u89c8\u5668\u4e2d\u6253\u5f00")
        browser_btn.clicked.connect(self._on_open_browser)
        btn_layout.addWidget(browser_btn)
        btn_layout.addStretch()
        close_btn = QPushButton("\u5173\u95ed")
        close_btn.clicked.connect(self._on_close)
        btn_layout.addWidget(close_btn)
        self._layout.addLayout(btn_layout)

        self._rebuild_cards()

    def _apply_style(self) -> None:
        t = self._t
        self.setStyleSheet(
            f"QDialog {{ background: {t.bg_primary}; }}\n"
            f"QLabel {{ color: {t.text_primary}; background: transparent; }}\n"
            f"QGroupBox {{ color: {t.text_primary}; font-weight: 600; border: 1px solid {t.border}; "
            f"border-radius: 8px; margin-top: 8px; padding-top: 8px; }}\n"
            f"QPushButton {{ background: {t.bg_elevated}; color: {t.text_primary}; "
            f"border: 1px solid {t.border}; border-radius: 6px; padding: 6px 20px; }}\n"
            f"QPushButton:hover {{ background: {t.bg_hover}; }}\n"
            f"QPushButton:disabled {{ color: {t.text_muted}; }}\n"
            f"QProgressBar {{ border: 1px solid {t.border_dark}; border-radius: 4px; "
            f"background: {t.bg_surface}; text-align: center; min-height: 22px; }}\n"
        )

    def _clear_cards(self) -> None:
        while self._cards_layout.count():
            item = self._cards_layout.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()

    def _rebuild_cards(self) -> None:
        self._clear_cards()
        if self._measured:
            max_t = max((m.total_raw_path_s for m in self._measured), default=0.001)
            for m in self._measured:
                self._cards_layout.addWidget(self._build_measured_card(m, max_t))
            return
        max_t_raw = max((p["t_raw"] for p in self._profiles_data), default=0.001)
        for p in self._profiles_data:
            self._cards_layout.addWidget(self._build_formula_card(p, max_t_raw))

    def _build_formula_card(self, p: dict, max_t_raw: float) -> QGroupBox:
        t = self._t
        card = QGroupBox(f"{p['name']} \u00b7 \u7406\u8bba\u4f30")
        card.setStyleSheet(f"QGroupBox {{ background: {t.bg_surface}; }}")
        cl = QVBoxLayout(card)
        self._add_profile_header(cl, p["name"], p["worth_it"], p, t)
        raw_pct = int(min(100, (p["t_raw"] / max_t_raw) * 100))
        comp_pct = int(min(100, (p["t_comp"] / max_t_raw) * 100))
        self._add_bar_row(cl, "\u539f\u59cb\u4f20\u8f93\uff08\u4f30\u7b97\uff09", raw_pct, p["t_raw"], False, t)
        self._add_bar_row(cl, "\u538b\u7f29\u4f53\u4f20\u8f93\uff08\u4f30\u7b97\uff09", comp_pct, p["t_comp"], True, t)
        self._add_metric_row(cl, "\u4f20\u8f93\u8282\u7701", f"+{_format_time_ns(p['saving'])}", "#22c55e")
        net_val = p["net_saving"]
        self._add_metric_row(
            cl,
            "\u51c0\u8282\u7701\uff08\u6263\u538b\u7f29\u8017\u65f6\uff09",
            f"{'+' if net_val > 0 else '-'}{_format_time_ns(abs(net_val))}",
            "#22c55e" if net_val > 0 else "#ef4444",
        )
        return card

    def _build_measured_card(self, m: ProfileBenchmarkResult, max_t: float) -> QGroupBox:
        t = self._t
        card = QGroupBox(f"{m.name} \u00b7 \u5b9e\u6d4b")
        card.setStyleSheet(f"QGroupBox {{ background: {t.bg_surface}; }}")
        cl = QVBoxLayout(card)
        p_stub = {
            "bandwidth_mbps": m.bandwidth_mbps,
            "latency_ms": m.latency_ms,
        }
        self._add_profile_header(cl, m.name, m.worth_it, p_stub, t)
        raw_pct = int(min(100, (m.total_raw_path_s / max_t) * 100))
        comp_pct = int(min(100, (m.total_comp_path_s / max_t) * 100))
        self._add_bar_row(
            cl, "\u76f4\u4f20\u539f\u6587\u4ef6\uff08\u9650\u901f\u5b9e\u6d4b\uff09", raw_pct, m.total_raw_path_s, False, t
        )
        self._add_bar_row(
            cl,
            "\u4f20\u538b\u7f29\u4f53+\u5ba2\u6237\u7aef\u89e3\u538b",
            comp_pct,
            m.total_comp_path_s,
            True,
            t,
        )
        muted = ThemeManager.hex("text_muted")
        self._add_metric_row(
            cl,
            "\u5176\u4e2d\uff1a\u9650\u901f\u4f20\u8f93\u538b\u7f29\u4f53",
            _format_time_ns(m.comp_transfer_s),
            muted,
        )
        self._add_metric_row(
            cl,
            "\u5176\u4e2d\uff1a\u5ba2\u6237\u7aef\u89e3\u538b",
            _format_time_ns(m.decompress_s),
            muted,
        )
        self._add_metric_row(
            cl,
            "\u76f8\u6bd4\u76f4\u4f20\u8282\u7701",
            f"{'+' if m.net_vs_raw_s > 0 else '-'}{_format_time_ns(abs(m.net_vs_raw_s))}",
            "#22c55e" if m.net_vs_raw_s > 0 else "#ef4444",
        )
        return card

    def _add_profile_header(self, cl, name: str, worth_it: bool, p, t) -> None:
        hdr = QHBoxLayout()
        title_lbl = QLabel(name)
        title_lbl.setStyleSheet(
            f"font-size: 15px; font-weight: 700; color: {t.text_primary}; background: transparent;"
        )
        hdr.addWidget(title_lbl)
        hdr.addStretch()
        badge = QLabel("\u2705 \u538b\u7f29\u4f53\u66f4\u4f18" if worth_it else "\u2717 \u76f4\u4f20\u66f4\u4f18")
        badge_color = "#166534" if worth_it else "#7f1d1d"
        badge_fg = "#86efac" if worth_it else "#fca5a5"
        badge.setStyleSheet(
            f"font-size: 11px; padding: 3px 10px; border-radius: 12px; font-weight: 600; "
            f"background: {badge_color}; color: {badge_fg};"
        )
        hdr.addWidget(badge)
        cl.addLayout(hdr)
        bw_lbl = QLabel(
            f"\u2193 {p['bandwidth_mbps']:.1f} Mbps  |  \u5ef6\u8fdf {p['latency_ms']:.0f} ms"
        )
        bw_lbl.setStyleSheet(f"font-size: 11px; color: {t.text_muted}; background: transparent;")
        cl.addWidget(bw_lbl)

    def _add_bar_row(self, cl, label: str, pct: int, seconds: float, is_comp: bool, t) -> None:
        lbl = QLabel(label)
        lbl.setStyleSheet(f"font-size: 11px; color: {t.text_secondary}; background: transparent;")
        cl.addWidget(lbl)
        bar = QProgressBar()
        bar.setMaximum(100)
        bar.setValue(pct)
        bar.setTextVisible(True)
        bar.setFormat(_format_time_ns(seconds))
        c = "#ef4444" if not is_comp else "#22c55e"
        bar.setStyleSheet(bar.styleSheet() + f"QProgressBar::chunk {{ background: {c}; border-radius: 3px; }}")
        cl.addWidget(bar)

    def _add_metric_row(self, cl, label: str, value: str, color) -> None:
        row = QHBoxLayout()
        row.addWidget(QLabel(label))
        row.addStretch()
        val = QLabel(value)
        if isinstance(color, str) and color.startswith("#"):
            css = f"font-size: 13px; font-weight: 700; color: {color}; background: transparent;"
        else:
            css = f"font-size: 13px; color: {color}; background: transparent;"
        val.setStyleSheet(css)
        row.addWidget(val)
        cl.addLayout(row)

    def _on_run_benchmark(self) -> None:
        if not self._benchmark_target or self._worker:
            return
        self._run_btn.setEnabled(False)
        self._cancel_btn.setVisible(True)
        self._progress.setVisible(True)
        self._progress.setRange(0, 0)
        self._worker = NetworkTransferWorker(self._benchmark_target, self)
        self._worker.progress.connect(self._on_benchmark_progress)
        self._worker.finished_ok.connect(self._on_benchmark_done)
        self._worker.failed.connect(self._on_benchmark_failed)
        self._worker.start()

    def _on_cancel_benchmark(self) -> None:
        if self._worker:
            self._worker.cancel()

    def _on_benchmark_progress(self, msg: str, cur: int, tot: int) -> None:
        if tot > 0:
            self._progress.setRange(0, tot)
            self._progress.setValue(cur)
        self._progress.setFormat(msg)

    def _on_benchmark_done(self, results: list) -> None:
        self._worker = None
        self._run_btn.setEnabled(True)
        self._cancel_btn.setVisible(False)
        self._progress.setVisible(False)
        self._measured = results
        self._rebuild_cards()
        worth = sum(1 for r in results if r.worth_it)
        self._analysis_lbl.setText(
            f"{self._benchmark_target.scope_note + chr(10) if self._benchmark_target.scope_note else ''}"
            f"\u5b9e\u6d4b\u5b8c\u6210\uff08\u9650\u901f\u53d1\u9001 + \u5ba2\u6237\u7aef\u89e3\u538b\uff09\u3002"
            f"{len(results)} \u79cd\u7f51\u7edc\u4e2d {worth} \u79cd\u4f20\u538b\u7f29\u4f53\u8def\u5f84\u66f4\u5feb\u3002\n"
            f"\u7406\u8bba\u4f30\u89c1\u4e0a\u65b9\u5361\u7247\uff1b\u5b9e\u6d4b\u7ed3\u679c\u5df2\u66ff\u6362\u4e3a\u5b9e\u6d4b\u6570\u636e\u3002"
        )

    def _on_benchmark_failed(self, message: str) -> None:
        self._worker = None
        self._run_btn.setEnabled(True)
        self._cancel_btn.setVisible(False)
        self._progress.setVisible(False)
        if message != "\u5df2\u53d6\u6d88":
            QMessageBox.warning(self, "\u4f20\u8f93\u6d4b\u8bd5", message)

    def _on_close(self) -> None:
        if self._worker and self._worker.isRunning():
            self._worker.cancel()
            self._worker.wait(3000)
        self.accept()

    def _report_html(self) -> str:
        return generate_network_sim(self._inp)

    def _on_export_html(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self,
            "\u4fdd\u5b58\u4f20\u8f93\u6548\u7387\u5bf9\u6bd4\u62a5\u544a",
            f"{self._inp.label}_transfer_report.html",
            "HTML (*.html)",
        )
        if path:
            try:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(self._report_html())
                QMessageBox.information(self, "\u6210\u529f", f"\u5df2\u4fdd\u5b58:\n{path}")
            except OSError as e:
                QMessageBox.warning(self, "\u9519\u8bef", str(e))

    def _on_open_browser(self) -> None:
        open_network_sim_html(self._report_html())


def _format_time_ns(seconds: float) -> str:
    if seconds < 0.001:
        return f"{seconds * 1_000_000:.0f} \u03bcs"
    if seconds < 1:
        return f"{seconds * 1000:.1f} ms"
    return f"{seconds:.2f} s"
