import logging
import logging.handlers
import sys
from pathlib import Path

_log_file: Path | None = None


def get_log_path() -> Path:
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).parent.parent / "logs"
    else:
        repo = Path(__file__).resolve().parent.parent.parent.parent
        pkg_logs = repo / "Package" / "logs"
        if pkg_logs.exists():
            base = pkg_logs
        else:
            base = repo / "logs"
    base.mkdir(parents=True, exist_ok=True)
    return base / "gui.log"


def setup_logging(log_path: Path | None = None):
    global _log_file

    root = logging.getLogger()
    root.setLevel(logging.DEBUG)

    for handler in root.handlers[:]:
        root.removeHandler(handler)

    _log_file = log_path if log_path is not None else get_log_path()

    if _log_file:
        _log_file.parent.mkdir(parents=True, exist_ok=True)
        file_handler = logging.handlers.RotatingFileHandler(
            _log_file,
            maxBytes=10 * 1024 * 1024,
            backupCount=5,
            encoding="utf-8",
        )
        file_handler.setLevel(logging.DEBUG)
        file_formatter = logging.Formatter(
            "%(asctime)s %(name)-20s %(levelname)-5s %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S",
        )
        file_handler.setFormatter(file_formatter)
        root.addHandler(file_handler)

    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(logging.INFO)
    console_formatter = logging.Formatter(
        "%(asctime)s %(name)s: %(levelname)s %(message)s"
    )
    console_handler.setFormatter(console_formatter)
    root.addHandler(console_handler)


def flush_logging():
    for handler in logging.getLogger().handlers:
        if hasattr(handler, "flush"):
            try:
                handler.flush()
            except Exception:
                pass