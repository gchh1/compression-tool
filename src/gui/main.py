from __future__ import annotations

import sys
import logging

from pathlib import Path


def setup_logging(level: int = logging.INFO):
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )


def run_cli():
    setup_logging()
    logger = logging.getLogger("gui")

    from gui.core.engine import CompressionEngine
    from gui.core.file_helper import scan_directory, load_batch
    from gui.core.strategy import StrategyDispatcher

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
        status = f"{r.compression_ratio:.1f}%" if r.status.value == "done" else r.status.value
        print(f"{r.name:<30} {r.type.value:<8} {r.size:>10} {comp_size:>10} {status:>8} {r.compression_time_ms:.1f}ms")


def run_gui():
    setup_logging()

    from PyQt6.QtWidgets import QApplication
    from gui.widgets.main_window import MainWindow

    app = QApplication(sys.argv)
    app.setApplicationName("WebCompress Pro")

    window = MainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    run_gui()
