"""GUI logging setup (file + stderr)."""

from __future__ import annotations

import logging
import sys
from pathlib import Path


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
    root_logger.setLevel(level)

    fmt = logging.Formatter(
        "%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    console_handler = logging.StreamHandler(sys.stderr)
    console_handler.setLevel(level)
    console_handler.setFormatter(fmt)
    root_logger.addHandler(console_handler)

    file_handler = logging.FileHandler(log_file, encoding="utf-8", mode="w")
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(fmt)
    root_logger.addHandler(file_handler)

    logging.info("Logging initialized, file: %s", log_file.resolve())
