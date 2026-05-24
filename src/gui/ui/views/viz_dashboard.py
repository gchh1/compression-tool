"""DEFLATE compression visualization — state-machine playback dashboard.

Implements the DEFLATE algorithm's three-phase state machine:
  FIND_MATCH → BUILD_TREE → FLUSH_TOKENS → (next block)

The entire dashboard runs inside a QWebEngineView with D3.js.
Python pushes per-block data; JS manages the state machine and
calls ``stepForward()`` on each Qt slider tick.

Communication:
  Python → JS:  ``page.runJavaScript("func(args)")``
  JS → Python:  ``console.log("VIZ:" + JSON)``
"""

from __future__ import annotations

import base64
import json
import logging
from pathlib import Path

from PyQt6.QtCore import Qt, QUrl, pyqtSignal, QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWebEngineCore import QWebEnginePage
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QSlider, QPushButton,
)

from gui.config.theme import ThemeManager
from gui.engine.viz_loader import VizLoader, HuffmanTreeBuilt, BlockBoundary

logger = logging.getLogger(__name__)

_PAGE_SIZE = 100_000
_RAW_WINDOW = 64 * 1024

_HTML_DIR = Path(__file__).resolve().parent / "html"
_HTML_PATH = _HTML_DIR / "deflate_dashboard.html"


class _VizPage(QWebEnginePage):
    js_message = pyqtSignal(str)

    def javaScriptConsoleMessage(self, level, message, line, source):
        if message.startswith("VIZ:"):
            self.js_message.emit(message[4:])


class VizDashboard(QWidget):
    """DEFLATE compression playback dashboard.

    Parameters
    ----------
    viz_path : str       Path to .viz binary file.
    source_path : str    Path to original uncompressed file.
    parent : QWidget, optional
    """

    def __init__(self, viz_path: str, source_path: str = "", parent=None):
        super().__init__(parent)
        self._viz_path = viz_path
        self._source_path = source_path
        self._loader: VizLoader | None = None
        self._blocks: list[BlockBoundary] = []
        self._huffman_trees: list[HuffmanTreeBuilt] = []
        self._total_match: int = 0
        self._auto_timer: QTimer | None = None
        self._auto_playing: bool = False
        self._auto_speed: int = 1          # 1, 2, 4, 8
        self._js_ready: bool = False
        self._js_state: str = "IDLE"       # tracked from JS state_changed
        self._pending_js: list[str] = []

        # Per-block data caches
        self._block_events: dict[int, list[dict]] = {}      # block_idx → serialized events
        self._block_raw: dict[int, tuple[bytes, int]] = {}  # block_idx → (raw_bytes, offset)
        self._block_huff: dict[int, tuple[list, list]] = {} # block_idx → (lit_len_list, dist_list)
        self._current_block: int = -1
        self._raw_total: int = 0
        self._block_event_counts: list[int] = []  # cumulative event counts per block

        self.setWindowTitle(f"DEFLATE 压缩回放 — {Path(viz_path).name}")
        self._setup_ui()
        self._load_data()
        ThemeManager().theme_changed.connect(self._on_theme_changed)

    # ── UI ──────────────────────────────────────────────────────────

    def _setup_ui(self) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        ctrl = QHBoxLayout()
        ctrl.setContentsMargins(10, 6, 10, 6)
        ctrl.setSpacing(4)

        self._info_lbl = QLabel("加载中...")
        self._info_lbl.setStyleSheet(
            f"color: {ThemeManager.resolve_hex('text_muted')}; font-size: 11px;")
        ctrl.addWidget(self._info_lbl)
        ctrl.addStretch()

        prev_btn = QPushButton("后退")
        prev_btn.setFixedWidth(60)
        prev_btn.setToolTip("上一步")
        prev_btn.clicked.connect(self._prev_step)
        ctrl.addWidget(prev_btn)

        self._auto_btn = QPushButton("播放")
        self._auto_btn.setFixedWidth(60)
        self._auto_btn.setToolTip("自动播放")
        self._auto_btn.clicked.connect(self._toggle_auto)
        ctrl.addWidget(self._auto_btn)

        next_btn = QPushButton("前进")
        next_btn.setFixedWidth(60)
        next_btn.setToolTip("下一步")
        next_btn.clicked.connect(self._next_step)
        ctrl.addWidget(next_btn)

        jump_btn = QPushButton("跳树")
        jump_btn.setFixedWidth(56)
        jump_btn.setToolTip("跳转到 BUILD_TREE")
        jump_btn.clicked.connect(self._jump_to_tree)
        ctrl.addWidget(jump_btn)

        self._slider = QSlider(Qt.Orientation.Horizontal)
        self._slider.setMinimum(0)
        self._slider.setMaximum(0)
        self._slider.valueChanged.connect(self._on_slider)
        ctrl.addWidget(self._slider)

        self._step_lbl = QLabel("0/0")
        self._step_lbl.setStyleSheet(
            f"color: {ThemeManager.resolve_hex('text_primary')}; font-size: 11px; min-width: 60px;")
        ctrl.addWidget(self._step_lbl)

        # Speed selector
        for sp, tip in [(1, "1x"), (2, "2x"), (4, "4x"), (8, "8x")]:
            btn = QPushButton(tip)
            btn.setFixedWidth(44)
            btn.setCheckable(True)
            btn.setToolTip(f"自动播放速度 {tip}")
            btn.clicked.connect(lambda checked, s=sp: self._set_speed(s))
            if sp == 1:
                btn.setChecked(True)
            ctrl.addWidget(btn)
            setattr(self, f"_spd_{sp}", btn)

        layout.addLayout(ctrl)

        self._webview = QWebEngineView()
        page = _VizPage(self._webview)
        self._webview.setPage(page)
        page.js_message.connect(self._on_js_message)
        self._webview.loadFinished.connect(self._on_page_loaded)
        url = QUrl.fromLocalFile(str(_HTML_PATH))
        self._webview.load(url)
        layout.addWidget(self._webview)

    # ── Data loading ────────────────────────────────────────────────

    def _load_data(self) -> None:
        try:
            self._loader = VizLoader(self._viz_path)
            self._blocks = self._loader.load_block_boundaries()
            self._huffman_trees = self._loader.load_huffman_trees()
            self._total_match = self._loader.match_count
            self._js_state = "IDLE"

            dp_total = 0
            try:
                dp_total = self._loader.dp_count
            except Exception:
                pass

            self._slider.setMaximum(self._total_match)
            self._step_lbl.setText(f"0 / {self._total_match} events")

            self._info_lbl.setText(
                f"匹配事件: {self._total_match} | 块: {len(self._blocks)} | "
                f"v{self._loader.version}" +
                (f" | DP: {dp_total}" if dp_total else "")
            )

            # Precompute block event counts (cumulative)
            self._block_event_counts = []
            cum = 0
            for b in self._blocks:
                cum += b.literal_count + b.match_count
                self._block_event_counts.append(cum)

            # Preload raw source file size
            if self._source_path:
                sp = Path(self._source_path)
                if sp.is_file():
                    self._raw_total = sp.stat().st_size

            # Push blocks metadata
            self._push_init()
            self._push_blocks()

            # Load first block (receiveBlockEvents resets state machine)
            if self._blocks:
                self._load_block_events(0)

        except Exception as e:
            logger.exception("Failed to load viz")
            self._info_lbl.setText(f"加载失败: {e}")

    def _load_block_events(self, block_idx: int) -> None:
        """Load & cache all events + huffman + raw bytes for *block_idx*."""
        if block_idx < 0 or block_idx >= len(self._blocks):
            return
        if block_idx in self._block_events:
            # Already cached — just push
            self._push_block_data(block_idx)
            return

        b = self._blocks[block_idx]
        n_events = b.literal_count + b.match_count

        # Calculate event offset for this block
        event_start = 0
        for i in range(block_idx):
            event_start += self._blocks[i].literal_count + self._blocks[i].match_count

        # Load match events for this block
        if self._loader and self._loader.version >= 2:
            raw_events = self._loader.get_match_page(event_start, n_events * 9)
        else:
            raw_events = (self._loader.load_match_events() if self._loader else [])
            raw_events = raw_events[event_start:event_start + n_events]

        events = []
        for ev in raw_events:
            events.append({
                "input_pos": ev.input_pos,
                "offset": ev.offset,
                "length": ev.length,
                "literal": ev.literal,
            })
        self._block_events[block_idx] = events

        # Load raw bytes window around this block
        raw_data = b""
        raw_off = 0
        if self._source_path:
            sp = Path(self._source_path)
            if sp.is_file():
                byte_start = b.input_start
                byte_end = min(byte_start + b.input_bytes + _RAW_WINDOW // 2, sp.stat().st_size)
                byte_start = max(0, byte_end - _RAW_WINDOW)
                with open(sp, "rb") as f:
                    f.seek(byte_start)
                    raw_data = f.read(byte_end - byte_start)
                raw_off = byte_start
        self._block_raw[block_idx] = (raw_data, raw_off)

        # Build Huffman data for this block
        lit_len = []
        dist = []
        for t in self._huffman_trees:
            if t.block_index != b.block_index:
                continue
            target = dist if t.tree_type in (1, 3) else lit_len
            for sym in range(t.alphabet_size):
                if sym >= len(t.code_lengths):
                    break
                cl = t.code_lengths[sym]
                if cl == 0:
                    continue
                target.append({
                    "symbol": sym,
                    "code_length": cl,
                    "is_literal": t.tree_type in (0, 2),
                })
        self._block_huff[block_idx] = (lit_len, dist)

        self._push_block_data(block_idx)

    def _push_block_data(self, block_idx: int) -> None:
        """Push a block's events + huffman + raw bytes to JS."""
        events = self._block_events.get(block_idx, [])
        raw_data, raw_off = self._block_raw.get(block_idx, (b"", 0))
        lit_len, dist = self._block_huff.get(block_idx, ([], []))

        data = {
            "block_index": block_idx,
            "events": events,
            "raw_b64": base64.b64encode(raw_data).decode("ascii") if raw_data else "",
            "raw_start": raw_off,
            "huff_lit": lit_len,
            "huff_dist": dist,
        }
        self._call_js(f"receiveBlockEvents({json.dumps(data)})")
        self._current_block = block_idx

        # Also push slider-to-block mapping for cursor updates
        event_start = 0
        for i in range(block_idx):
            event_start += self._blocks[i].literal_count + self._blocks[i].match_count
        self._block_event_offset = event_start  # starting event index for this block

    def _find_block_for_event(self, event_idx: int) -> int:
        """Return block index that contains global *event_idx*."""
        for i, cum in enumerate(self._block_event_counts):
            if event_idx < cum:
                return i
        return len(self._blocks) - 1 if self._blocks else -1

    # ── Python → JS bridge ──────────────────────────────────────────

    def _call_js(self, js: str) -> None:
        if self._js_ready:
            self._webview.page().runJavaScript(js)
        else:
            self._pending_js.append(js)

    def _on_page_loaded(self, ok: bool) -> None:
        if not ok:
            logger.error("WebEngine page load failed")
            return
        self._js_ready = True
        try:
            self._push_theme()
        except Exception:
            logger.exception("_push_theme")
        for js in self._pending_js:
            try:
                self._webview.page().runJavaScript(js)
            except Exception:
                logger.exception("runJavaScript: %s", js[:80])
        self._pending_js.clear()

    def _push_theme(self) -> None:
        t = ThemeManager.get()

        def _blend(hex_color: str, over_hex: str, alpha: float) -> str:
            """Blend *hex_color* over *over_hex* with given *alpha* (0-1)."""
            fg = QColor(hex_color)
            bg = QColor(over_hex)
            r = int(fg.red() * alpha + bg.red() * (1 - alpha))
            g = int(fg.green() * alpha + bg.green() * (1 - alpha))
            b = int(fg.blue() * alpha + bg.blue() * (1 - alpha))
            return f"#{r:02x}{g:02x}{b:02x}"

        def _alpha(hex_color: str, a: float) -> str:
            c = QColor(hex_color)
            return f"rgba({c.red()},{c.green()},{c.blue()},{a})"

        css = {
            # Base
            "bg": t.bg_primary, "surface": t.bg_surface, "border": t.border,
            "text": t.text_primary, "text-muted": t.text_muted,
            "text-secondary": t.text_secondary,
            "accent": t.accent, "success": t.success,
            "warning": t.warning, "error": t.error, "chart-5": t.chart_5,
            # State pills
            "pill-find": _blend(t.accent, t.bg_primary, 0.25),
            "pill-find-active": t.accent,
            "pill-build": _blend(t.warning, t.bg_primary, 0.25),
            "pill-build-active": t.warning,
            "pill-flush": _blend(t.chart_5, t.bg_primary, 0.25),
            "pill-flush-active": t.chart_5,
            # Tokens
            "tok-bg": _alpha(t.accent, 0.18), "tok-text": t.text_primary,
            # Treadmill
            "tz-focus-border": t.warning,
            "tz-focus-bg": _alpha(t.warning, 0.08),
            "progress-fill": t.accent,
            "tz-match-bg": _alpha(t.success, 0.22),
            "tz-match-text": "#ffffff",
            "tz-cell-past": _alpha(t.text_primary, 0.04),
            "tz-cell-future": "transparent",
            # Huffman trees
            "tree-lit-fill": _alpha(t.accent, 0.15),
            "tree-lit-stroke": t.accent,
            "tree-dist-fill": _alpha(t.success, 0.15),
            "tree-dist-stroke": t.success,
            "tree-int-fill": t.bg_surface,
            "tree-int-stroke": t.text_muted,
            "tree-link": t.border,
            # Tooltip
            "tip-bg": _alpha(t.bg_primary, 0.97),
            "tip-code": t.accent,
            "tip-value": t.success,
        }
        self._call_js(f"receiveTheme({json.dumps(css)})")

    def _push_init(self) -> None:
        data = {
            "match_count": self._total_match,
            "block_count": len(self._blocks),
            "version": self._loader.version if self._loader else 0,
            "raw_total": self._raw_total,
        }
        self._call_js(f"receiveInit({json.dumps(data)})")

    def _push_blocks(self) -> None:
        blocks = []
        for b in self._blocks:
            ratio = (b.output_bytes / max(b.input_bytes, 1)) * 100 if b.input_bytes > 0 else 0
            blocks.append({
                "block_index": b.block_index,
                "input_start": b.input_start,
                "input_bytes": b.input_bytes,
                "literal_count": b.literal_count,
                "match_count": b.match_count,
                "output_bytes": b.output_bytes,
                "ratio": round(ratio, 1),
            })
        self._call_js(f"receiveBlocks({json.dumps(blocks)})")

    # ── JS → Python ─────────────────────────────────────────────────

    def _on_js_message(self, raw_json: str) -> None:
        try:
            msg = json.loads(raw_json)
        except json.JSONDecodeError:
            return
        action = msg.get("action", "")
        payload = msg.get("payload", {})

        if action == "load_block":
            bi = payload.get("block_index")
            if bi is not None:
                self._load_block_events(bi)
                start = 0
                for i in range(bi):
                    start += self._blocks[i].literal_count + self._blocks[i].match_count
                self._slider.blockSignals(True)
                self._slider.setValue(start)
                self._slider.blockSignals(False)
                self._step_lbl.setText(f"{start}/{self._total_match}")

        elif action == "sync_slider":
            # JS auto-play / stepBackward reports global event index
            global_idx = payload.get("global_idx", 0)
            if not self._slider.isSliderDown():
                self._slider.blockSignals(True)
                self._slider.setValue(min(global_idx, self._total_match))
                self._slider.blockSignals(False)
                self._step_lbl.setText(f"{global_idx}/{self._total_match}")

        elif action == "state_changed":
            new_state = payload.get("state", "")
            if new_state:
                self._js_state = new_state

    # ── Navigation (slider / buttons) ───────────────────────────────

    def _prev_step(self) -> None:
        """Go back one step: undo event in FIND_MATCH, or undo merge in BUILD_TREE."""
        if self._total_match == 0:
            return
        v = self._slider.value()
        if v == 0:
            return
        max_v = self._slider.maximum()
        if v >= max_v:
            # At max — in BUILD_TREE or FLUSH_TOKENS. JS stepBackward handles all states.
            self._call_js("stepBackward()")
        else:
            # In FIND_MATCH — just decrement the event counter.
            self._slider.setValue(v - 1)

    def _next_step(self) -> None:
        if self._total_match == 0:
            return
        v = self._slider.value()
        max_v = self._slider.maximum()

        # When the slider is at the end of the *current block's* events,
        # call stepForward() so JS can enter BUILD_TREE (or advance to the
        # next block).  Previously we only checked v >= max_v, which meant
        # tree-building was skipped for every block except the last one.
        bi = self._current_block if self._current_block >= 0 else 0
        if bi < len(self._blocks):
            block_end = sum(
                self._blocks[i].literal_count + self._blocks[i].match_count
                for i in range(bi + 1)
            )
            if v >= block_end:
                self._call_js("stepForward()")
                return

        if v >= max_v:
            self._call_js("stepForward()")
        else:
            self._slider.setValue(v + 1)

    def _jump_to_tree(self) -> None:
        """Jump slider to end-of-block to trigger BUILD_TREE transition."""
        if self._total_match == 0:
            return
        bi = self._current_block if self._current_block >= 0 else 0
        if bi >= len(self._blocks):
            return
        # Set slider to end of current block (triggers _on_slider → jumpToEvent → prepareBuildTree)
        end = 0
        for i in range(bi + 1):
            end += self._blocks[i].literal_count + self._blocks[i].match_count
        self._slider.setValue(min(end, self._total_match))

    def _on_slider(self, value: int) -> None:
        self._step_lbl.setText(f"{value} / {self._total_match} events")

        # For value 0, just init (no events processed)
        if value == 0:
            bi = 0
            if bi < len(self._blocks) and bi not in self._block_events:
                self._load_block_events(bi)
            elif bi < len(self._blocks):
                self._push_block_data(bi)
            if bi < len(self._blocks):
                self._call_js("jumpToEvent(0)")
            return

        bi = self._find_block_for_event(value - 1)  # event at index value-1
        if bi < 0:
            return

        if bi != self._current_block and bi not in self._block_events:
            self._load_block_events(bi)
        elif bi != self._current_block:
            self._push_block_data(bi)

        event_start = 0
        for i in range(bi):
            event_start += self._blocks[i].literal_count + self._blocks[i].match_count
        local_count = value - event_start  # number of events to process in this block

        self._call_js(f"jumpToEvent({local_count})")

    # ── Auto-play ───────────────────────────────────────────────────

    def _toggle_auto(self) -> None:
        if self._auto_playing:
            self._stop_auto()
        else:
            self._start_auto()

    def _start_auto(self) -> None:
        self._auto_playing = True
        self._auto_btn.setText("停止")
        self._auto_timer = QTimer(self)
        self._auto_timer.timeout.connect(self._auto_step)
        self._auto_timer.start(max(20, 80 // self._auto_speed))

    def _stop_auto(self) -> None:
        self._auto_playing = False
        self._auto_btn.setText("播放")
        if self._auto_timer:
            self._auto_timer.stop()
            self._auto_timer = None

    def _auto_step(self) -> None:
        if self._total_match == 0:
            self._stop_auto()
            return
        self._call_js("stepForward()")

    def _set_speed(self, speed: int) -> None:
        """Set auto-play speed multiplier (1, 2, 4, 8)."""
        self._auto_speed = speed
        for s in (1, 2, 4, 8):
            btn = getattr(self, f"_spd_{s}", None)
            if btn:
                btn.setChecked(s == speed)
        if self._auto_playing:
            # Restart timer with new interval
            self._auto_timer.stop()
            self._auto_timer.start(max(20, 80 // speed))

    # ── Theme ───────────────────────────────────────────────────────

    def _on_theme_changed(self) -> None:
        self.setStyleSheet(ThemeManager.full_dialog_sheet())
        self._push_theme()

    # ── Cleanup ─────────────────────────────────────────────────────

    def closeEvent(self, event) -> None:
        self._stop_auto()
        if self._loader:
            self._loader.close()
            self._loader = None
        super().closeEvent(event)
