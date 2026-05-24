from __future__ import annotations

import logging
import sys
from pathlib import Path

# Add src/ to sys.path automatically if running directly
_src_dir = Path(__file__).resolve().parent.parent
if str(_src_dir) not in sys.path:
    sys.path.insert(0, str(_src_dir))

from gui.utils.logging import setup_logging, flush_logging
from gui.utils.resources import resolve_icon_path


def run_cli():
    setup_logging()
    logger = logging.getLogger("gui")

    from gui.utils.workspace import ensure_workspace_layout

    ensure_workspace_layout()

    from gui.engine.compressor import CompressionEngine
    from gui.utils.file_helper import scan_directory, load_batch
    from gui.ade.engine import StrategyDispatcher

    if len(sys.argv) < 2:
        print("Usage: python -m gui <directory>")
        sys.exit(1)

    target = Path(sys.argv[1])
    if not target.exists():
        print(f"Path not found: {target}")
        sys.exit(1)

    engine = CompressionEngine()
    if not engine.available:
        logger.error("C++ core_engine not available, cannot proceed")
        sys.exit(1)

    records = scan_directory(target)
    if not records:
        logger.warning("No files found in %s", target)
        sys.exit(0)

    load_batch(records)

    dispatcher = StrategyDispatcher(engine)
    results = dispatcher.dispatch_batch(records)

    print(f"\n{'File':<30} {'Type':<8} {'Original':>10} {'Compressed':>10} {'Ratio':>8} {'Time':>8}")
    print("-" * 80)
    for r in results:
        comp_size = len(r.compressed_data) if r.compressed_data else 0
        status = f"{r.compression_ratio:.2f}%" if r.status.value == "done" else r.status.value
        print(f"{r.name:<30} {r.type.value:<8} {r.size:>10} {comp_size:>10} {status:>8} {r.compression_time_ms:.1f}ms")


def run_gui():
    setup_logging()

    import logging
    logger = logging.getLogger("gui.main")

    def handle_exception(exc_type, exc_value, exc_traceback):
        if issubclass(exc_type, KeyboardInterrupt):
            sys.__excepthook__(exc_type, exc_value, exc_traceback)
            return
        logger.error("Uncaught exception", exc_info=(exc_type, exc_value, exc_traceback))
        flush_logging()

    sys.excepthook = handle_exception

    import PyQt6.QtWebEngineWidgets  # noqa: F401 — must precede QApplication
    from PyQt6.QtWidgets import QApplication
    from PyQt6.QtGui import QIcon
    from gui.ui.main_window import MainWindow
    from gui.config.settings import apply_theme

    app = QApplication(sys.argv)
    app.setApplicationName("WebCompress")
    app.setApplicationDisplayName("WebCompress")

    _icon_path = resolve_icon_path()
    if _icon_path and _icon_path.exists():
        app.setWindowIcon(QIcon(str(_icon_path)))

    apply_theme()

    from gui.utils.workspace import ensure_workspace_layout

    ensure_workspace_layout()

    from gui.utils.workspace import cleanup_workspace_on_app_quit

    def _log_quit() -> None:
        logger.info("[ui] aboutToQuit")
        flush_logging()

    app.aboutToQuit.connect(_log_quit)
    app.aboutToQuit.connect(cleanup_workspace_on_app_quit)

    window = MainWindow()
    window.show()
    logger.info("[ui] MainWindow shown, entering event loop")
    flush_logging()

    code = app.exec()
    logger.info("[ui] event loop exited code=%s", code)
    flush_logging()
    sys.exit(code)


if __name__ == "__main__":
    run_gui()
