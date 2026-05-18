"""Structured UI / interaction logging (no algorithm or codec paths).

Use for menu actions, add/remove files, dialogs, and other Qt workflow steps.
Messages go to ``gui.ui`` with a stable ``[ui]`` prefix for grep in ``gui.log``.
"""

from __future__ import annotations

import logging
from typing import Any

from gui.utils.logging import flush_logging

logger = logging.getLogger("gui.ui")


def _truncate(s: str, max_len: int = 200) -> str:
    s = s.replace("\n", "\\n")
    if len(s) <= max_len:
        return s
    return s[: max_len - 3] + "..."


def log_ui(event: str, *, level: int = logging.INFO, **fields: Any) -> None:
    """Emit one line: ``[ui] <event> k=v ...`` (sorted keys)."""
    parts: list[str] = [event]
    for key in sorted(fields):
        val = fields[key]
        if val is None:
            continue
        parts.append(f"{key}={_truncate(str(val), 800)}")
    msg = " ".join(parts)
    logger.log(level, "[ui] %s", msg)


def log_ui_flush(event: str, **fields: Any) -> None:
    """``log_ui`` then flush all log handlers (disk + stderr)."""
    log_ui(event, **fields)
    flush_logging()


def preview_paths(paths: list[str], *, max_items: int = 5, each_max: int = 120) -> str:
    """Short human-readable list for logs."""
    if not paths:
        return ""
    shown = [_truncate(p, each_max) for p in paths[:max_items]]
    extra = len(paths) - max_items
    if extra > 0:
        shown.append(f"(+{extra} more)")
    return " | ".join(shown)
