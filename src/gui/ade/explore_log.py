"""Structured logging for silent exploration (ADE background jobs).

Messages use ``gui.explore`` with a stable ``[explore]`` prefix and ``key=value``
fields for grep in ``Package/logs/gui.log``.
"""

from __future__ import annotations

import logging
import threading
from typing import Any

logger = logging.getLogger("gui.explore")


def _thread_tag() -> str:
    t = threading.current_thread()
    return t.name or f"tid={t.ident}"


def log_explore(event: str, *, level: int = logging.INFO, **fields: Any) -> None:
    """Emit one line: ``[explore] <event> k=v ...``."""
    parts: list[str] = [event]
    if "thread" not in fields:
        parts.append(f"thread={_thread_tag()}")
    for key in sorted(fields):
        val = fields[key]
        if val is None:
            continue
        s = str(val).replace("\n", "\\n")
        if len(s) > 800:
            s = s[:797] + "..."
        parts.append(f"{key}={s}")
    msg = " ".join(parts)
    logger.log(level, "[explore] %s", msg)


def summarize_stream_result(res: Any) -> dict[str, Any]:
    """Fields from ``ExploreStreamResult`` or pipeline binding."""
    out: dict[str, Any] = {}
    for k in (
        "success",
        "cancelled",
        "job_id",
        "wcx_path",
        "payload_bytes",
        "compression_ratio",
        "time_ms",
        "bytes_processed",
        "error_message",
        "manifest_path",
    ):
        if hasattr(res, k):
            out[k] = getattr(res, k)
    return out


def summarize_pipeline_result(obj: Any) -> dict[str, Any]:
    """Fields from C++ ``PipelineCompressResult`` / ``CompressResult``."""
    out: dict[str, Any] = {}
    for k in (
        "success",
        "cancelled",
        "original_size",
        "compressed_size",
        "compression_ratio",
        "time_ms",
        "bytes_processed",
        "error_message",
    ):
        if hasattr(obj, k):
            out[k] = getattr(obj, k)
    return out
