"""GUI logging setup (file + stderr)."""

from __future__ import annotations

import logging
import sys
import time
from pathlib import Path


class _FlushingStreamHandler(logging.StreamHandler):
    """Flush stderr after each record so console captures survive abrupt exits."""

    def emit(self, record: logging.LogRecord) -> None:
        super().emit(record)
        self.flush()


class _FlushingFileHandler(logging.FileHandler):
    """Flush the log file after each record (helps diagnose native crashes)."""

    def emit(self, record: logging.LogRecord) -> None:
        super().emit(record)
        self.flush()


def flush_logging() -> None:
    """Push all root handlers to disk/stderr (call after critical UI steps)."""
    for h in logging.root.handlers:
        try:
            h.flush()
        except Exception:
            pass


def get_log_dir() -> Path:
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).parent.parent / "logs"
    else:
        base = Path(__file__).resolve().parent.parent.parent.parent / "logs"
    base.mkdir(parents=True, exist_ok=True)
    return base


def setup_logging(level: int = logging.INFO) -> None:
    log_dir = get_log_dir()
    log_file = log_dir / "gui.log"

    root_logger = logging.getLogger()
    root_logger.handlers.clear()
    root_logger.setLevel(logging.DEBUG)

    fmt = logging.Formatter(
        "%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    console_handler = _FlushingStreamHandler(sys.stderr)
    console_handler.setLevel(level)
    console_handler.setFormatter(fmt)
    root_logger.addHandler(console_handler)

    file_handler = _FlushingFileHandler(log_file, encoding="utf-8", mode="a")
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(fmt)
    root_logger.addHandler(file_handler)

    session = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    logging.info("==== gui session start %s ====", session)
    logging.info("Logging initialized, file: %s (append mode, per-line flush)", log_file.resolve())
    flush_logging()
