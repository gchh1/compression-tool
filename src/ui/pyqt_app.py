import sys
from html import escape
from pathlib import Path
from typing import List, Optional, Tuple

try:
    from services.compression_service import (
        APP_DIR,
        build_lz77_demo_steps,
        compress_resource,
        decompress_resource,
        import_resource,
    )
except ModuleNotFoundError:
    from src.ui.services.compression_service import (
        APP_DIR,
        build_lz77_demo_steps,
        compress_resource,
        decompress_resource,
        import_resource,
    )

try:
    from PySide6.QtCore import Qt, QTimer, Signal
    from PySide6.QtWidgets import (
        QApplication,
        QComboBox,
        QFileDialog,
        QHBoxLayout,
        QLabel,
        QListWidget,
        QListWidgetItem,
        QMainWindow,
        QMessageBox,
        QPushButton,
        QSlider,
        QTextEdit,
        QVBoxLayout,
        QWidget,
    )
except Exception as exc:
    raise SystemExit(
        "PySide6 is required. Install with: pip install PySide6"
    ) from exc


class DropListWidget(QListWidget):
    paths_dropped = Signal(list)

    def __init__(self) -> None:
        super().__init__()
        self.setAcceptDrops(True)
        self.setSelectionMode(QListWidget.SingleSelection)

    def dragEnterEvent(self, event):  # type: ignore[override]
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event):  # type: ignore[override]
        urls = event.mimeData().urls()
        paths: List[str] = []
        for url in urls:
            if url.isLocalFile():
                paths.append(url.toLocalFile())
        if paths:
            self.paths_dropped.emit(paths)
            event.acceptProposedAction()
        else:
            event.ignore()


class TokenWindow(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Saved Tokens")
        self.resize(680, 520)
        self.viewer = QTextEdit()
        self.viewer.setReadOnly(True)
        layout = QVBoxLayout()
        layout.addWidget(self.viewer)
        self.setLayout(layout)

    def render(self, steps: list, active_index: int) -> None:
        lines: List[str] = []
        for i, step in enumerate(steps):
            prefix = ">> " if i == active_index else "   "
            if step.token_type == "literal_run":
                line = (
                    f"{prefix}[{i:03d}] literal_run len={step.literal_length} "
                    f"token={step.token_value} token_bytes={step.token_bytes_hex}"
                )
            else:
                line = (
                    f"{prefix}[{i:03d}] match off={step.match_distance} len={step.match_length} "
                    f"next={step.next_symbol} token={step.token_value} token_bytes={step.token_bytes_hex}"
                )
            lines.append(line)
        self.viewer.setPlainText("\n".join(lines))


class LZ77DemoWindow(QWidget):
    def __init__(self, demo_file: Path) -> None:
        super().__init__()
        self.setWindowTitle("LZ77 Visualization")
        self.resize(1100, 700)

        demo = build_lz77_demo_steps(str(demo_file))
        self.steps = demo.steps
        self.source_bytes = Path(demo.source_path).read_bytes()
        self.token_window = TokenWindow()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.next_step)

        self.title = QLabel(f"Demo source: {demo.source_path}")
        self.position = QLabel("Step: 0")
        self.meta = QLabel("")
        self.stream_mode = QComboBox()
        self.stream_mode.addItems(["auto", "char", "byte", "bit"])
        self.stream_mode.currentTextChanged.connect(self.render_current)

        self.btn_play = QPushButton("Play")
        self.btn_pause = QPushButton("Pause")
        self.btn_prev = QPushButton("Prev")
        self.btn_next = QPushButton("Next")
        self.speed_slider = QSlider(Qt.Horizontal)
        self.speed_slider.setMinimum(1)
        self.speed_slider.setMaximum(10)
        self.speed_slider.setValue(5)
        self.speed_slider.valueChanged.connect(self.update_speed)

        self.stream_view = QTextEdit()
        self.stream_view.setReadOnly(True)
        self.detail = QTextEdit()
        self.detail.setReadOnly(True)

        self.slider = QSlider(Qt.Horizontal)
        self.slider.setMinimum(0)
        self.slider.setMaximum(max(0, len(self.steps) - 1))
        self.slider.valueChanged.connect(self.render_step)
        self.current_index = 0

        control = QHBoxLayout()
        control.addWidget(QLabel("Display:"))
        control.addWidget(self.stream_mode)
        control.addWidget(self.btn_prev)
        control.addWidget(self.btn_play)
        control.addWidget(self.btn_pause)
        control.addWidget(self.btn_next)
        control.addWidget(QLabel("Speed"))
        control.addWidget(self.speed_slider)

        layout = QVBoxLayout()
        layout.addWidget(self.title)
        layout.addWidget(self.position)
        layout.addWidget(self.meta)
        layout.addLayout(control)
        layout.addWidget(self.slider)
        layout.addWidget(self.stream_view)
        layout.addWidget(self.detail)
        self.setLayout(layout)

        self.btn_play.clicked.connect(self.play)
        self.btn_pause.clicked.connect(self.pause)
        self.btn_prev.clicked.connect(self.prev_step)
        self.btn_next.clicked.connect(self.next_step)
        self.update_speed()
        self.render_step(0)
        self.token_window.show()

    def closeEvent(self, event):  # type: ignore[override]
        self.timer.stop()
        self.token_window.close()
        super().closeEvent(event)

    def update_speed(self) -> None:
        # 1..10 -> 800..100 ms
        ms = max(100, 900 - self.speed_slider.value() * 80)
        self.timer.setInterval(ms)

    def play(self) -> None:
        self.timer.start()

    def pause(self) -> None:
        self.timer.stop()

    def prev_step(self) -> None:
        idx = max(0, self.current_index - 1)
        self.slider.setValue(idx)

    def next_step(self) -> None:
        if not self.steps:
            return
        idx = self.current_index + 1
        if idx >= len(self.steps):
            self.timer.stop()
            return
        self.slider.setValue(idx)

    def _is_text_like(self, data: bytes) -> bool:
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            return False
        printable = sum(1 for b in data if 32 <= b <= 126 or b in (9, 10, 13))
        return printable / max(1, len(data)) > 0.9

    def _mode_units(self) -> Tuple[str, List[str]]:
        mode = self.stream_mode.currentText()
        if mode == "auto":
            mode = "char" if self._is_text_like(self.source_bytes) else "byte"

        if mode == "char":
            # One-byte display to keep index mapping consistent with compression bytes.
            return "char", [chr(b) if 32 <= b <= 126 else "." for b in self.source_bytes]
        if mode == "byte":
            return "byte", [format(b, "08b") for b in self.source_bytes]
        bit_units = list("".join(format(b, "08b") for b in self.source_bytes))
        return "bit", bit_units

    @staticmethod
    def _in_range(pos: int, rg: Tuple[int, int]) -> bool:
        return rg[0] <= pos < rg[1]

    def _render_stream_html(
        self, units: List[str], mode: str, match_rg: Tuple[int, int], look_rg: Tuple[int, int]
    ) -> str:
        parts: List[str] = []
        sep = " " if mode == "byte" else ""

        # High-contrast palette for readability:
        # - match only: warm amber
        # - lookahead only: cool cyan
        # - overlap: violet accent
        normal_style = "color:#1f2937;"
        match_style = (
            "background-color:#ffd166; color:#111827; "
            "border:1px solid #b45309; border-radius:3px; padding:1px 2px;"
        )
        look_style = (
            "background-color:#8ecae6; color:#0b132b; "
            "border:1px solid #1d4ed8; border-radius:3px; padding:1px 2px;"
        )
        overlap_style = (
            "background-color:#cdb4db; color:#111827; "
            "border:1px solid #7c3aed; border-radius:3px; padding:1px 2px; font-weight:600;"
        )

        for i, token in enumerate(units):
            text = escape(token)
            in_match = self._in_range(i, match_rg)
            in_look = self._in_range(i, look_rg)
            style = normal_style
            if in_match and in_look:
                style = overlap_style
            elif in_match:
                style = match_style
            elif in_look:
                style = look_style
            if style:
                parts.append(f'<span style="{style}">{text}</span>')
            else:
                parts.append(text)
        joined = sep.join(parts)
        return (
            "<div style='font-family:Consolas,monospace; line-height:1.65; "
            "font-size:14px; padding:8px; background-color:#f8fafc; border:1px solid #cbd5e1; "
            "border-radius:8px; color:#1f2937; white-space: pre-wrap;'>"
            f"{joined}</div>"
        )

    def render_current(self) -> None:
        self.render_step(self.current_index)

    def render_step(self, index: int) -> None:
        self.current_index = index
        if not self.steps:
            self.position.setText("No steps generated.")
            self.meta.setText("")
            self.stream_view.setPlainText("")
            self.detail.setPlainText("")
            return

        step = self.steps[index]
        mode, units = self._mode_units()

        if mode == "bit":
            cursor = step.cursor * 8
            look_len = max(1, step.consumed_length * 8)
            match_start = max(0, step.cursor - step.match_distance) * 8
            match_len = max(0, step.match_length * 8)
        else:
            cursor = step.cursor
            look_len = max(1, step.consumed_length)
            match_start = max(0, step.cursor - step.match_distance)
            match_len = max(0, step.match_length)

        look_rg = (cursor, cursor + look_len)
        match_rg = (match_start, match_start + match_len)

        self.position.setText(f"Step {step.step_index + 1}/{len(self.steps)} | Cursor={step.cursor}")
        self.meta.setText(
            "Mode={mode} | TokenType={tt} | Match(off={off}, len={ln}) | "
            "Color: amber=match, cyan=lookahead, violet=overlap".format(
                mode=mode,
                tt=step.token_type,
                off=step.match_distance,
                ln=step.match_length,
            )
        )
        self.stream_view.setHtml(self._render_stream_html(units, mode, match_rg, look_rg))
        self.detail.setPlainText(
            "\n".join(
                [
                    f"token: {step.token_value}",
                    f"token_bytes(hex): {step.token_bytes_hex}",
                    f"literal_bytes(hex): {step.literal_bytes_hex}",
                    f"consumed_length: {step.consumed_length}",
                    f"next_symbol: {step.next_symbol}",
                ]
            )
        )
        self.token_window.render(self.steps, index)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Compression Interface (PyQt/PySide6)")
        self.resize(900, 600)

        self.imported_paths: List[str] = []
        self.last_compressed_path: Optional[str] = None
        self.demo_window: Optional[LZ77DemoWindow] = None

        container = QWidget()
        layout = QVBoxLayout()
        info = QLabel(
            "Drop files/folders here, or use import buttons. "
            "Imported resources are staged into resources/staging."
        )
        self.resource_list = DropListWidget()
        self.resource_list.paths_dropped.connect(self.handle_paths)

        btn_row1 = QHBoxLayout()
        self.btn_import_file = QPushButton("Import File")
        self.btn_import_folder = QPushButton("Import Folder")
        btn_row1.addWidget(self.btn_import_file)
        btn_row1.addWidget(self.btn_import_folder)

        btn_row2 = QHBoxLayout()
        self.btn_compress = QPushButton("Compress")
        self.btn_decompress = QPushButton("Decompress")
        self.btn_visualize = QPushButton("Algorithm Visualization")
        btn_row2.addWidget(self.btn_compress)
        btn_row2.addWidget(self.btn_decompress)
        btn_row2.addWidget(self.btn_visualize)

        self.log = QTextEdit()
        self.log.setReadOnly(True)

        layout.addWidget(info)
        layout.addLayout(btn_row1)
        layout.addWidget(self.resource_list)
        layout.addLayout(btn_row2)
        layout.addWidget(self.log)
        container.setLayout(layout)
        self.setCentralWidget(container)

        self.btn_import_file.clicked.connect(self.import_file)
        self.btn_import_folder.clicked.connect(self.import_folder)
        self.btn_compress.clicked.connect(self.compress_selected)
        self.btn_decompress.clicked.connect(self.decompress_latest)
        self.btn_visualize.clicked.connect(self.show_demo)

    def append_log(self, message: str) -> None:
        self.log.append(message)

    def selected_resource_path(self) -> Optional[str]:
        item = self.resource_list.currentItem()
        if not item:
            return None
        return item.data(Qt.UserRole)

    def handle_paths(self, paths: List[str]) -> None:
        for path in paths:
            try:
                resource = import_resource(path)
                icon = "📁" if resource.resource_type == "folder" else "📄"
                label = (
                    f"{icon} {resource.name} | type={resource.resource_type} "
                    f"| size={resource.size_bytes}B | files={resource.file_count}"
                )
                item = QListWidgetItem(label)
                item.setData(Qt.UserRole, resource.staged_path)
                self.resource_list.addItem(item)
                self.imported_paths.append(resource.staged_path)
                self.append_log(f"[IMPORT] {resource.source_path} -> {resource.staged_path}")
            except Exception as exc:
                self.append_log(f"[ERROR] import failed: {exc}")
                QMessageBox.warning(self, "Import failed", str(exc))

    def import_file(self) -> None:
        file_path, _ = QFileDialog.getOpenFileName(self, "Select file")
        if file_path:
            self.handle_paths([file_path])

    def import_folder(self) -> None:
        folder_path = QFileDialog.getExistingDirectory(self, "Select folder")
        if folder_path:
            self.handle_paths([folder_path])

    def compress_selected(self) -> None:
        selected = self.selected_resource_path()
        if not selected:
            QMessageBox.information(self, "No selection", "Select one resource first.")
            return
        try:
            result = compress_resource(selected)
            self.last_compressed_path = result.output_path
            self.append_log(
                f"[COMPRESS] {result.input_path} -> {result.output_path} | "
                f"ratio={result.ratio:.3f} | {result.message}"
            )
        except Exception as exc:
            self.append_log(f"[ERROR] compress failed: {exc}")
            QMessageBox.warning(self, "Compress failed", str(exc))

    def decompress_latest(self) -> None:
        archive_path = self.last_compressed_path
        selected = self.selected_resource_path()
        if selected and selected.endswith(".lz77"):
            archive_path = selected
        if not archive_path:
            QMessageBox.information(
                self,
                "No archive",
                "Run compress first or select a .lz77 entry to decompress.",
            )
            return
        try:
            result = decompress_resource(archive_path)
            self.append_log(
                f"[DECOMPRESS] {result.input_path} -> {result.output_path} | "
                f"time={result.duration_sec:.4f}s | {result.message}"
            )
        except Exception as exc:
            self.append_log(f"[ERROR] decompress failed: {exc}")
            QMessageBox.warning(self, "Decompress failed", str(exc))

    def show_demo(self) -> None:
        demo_file = APP_DIR / "resources" / "test1.txt"
        try:
            self.demo_window = LZ77DemoWindow(demo_file)
            self.demo_window.show()
            self.append_log(f"[VISUAL] Demo opened with: {demo_file}")
        except Exception as exc:
            self.append_log(f"[ERROR] visualization failed: {exc}")
            QMessageBox.warning(self, "Visualization failed", str(exc))


def main() -> None:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
