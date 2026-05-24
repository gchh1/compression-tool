from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any

_event_logger = logging.getLogger("interaction")


def log_ui(event: str, **kwargs: Any) -> None:
    extra = {k: v for k, v in kwargs.items() if v is not None}
    if extra:
        _event_logger.info("%s %s", event, json.dumps(extra, default=str, ensure_ascii=False))
    else:
        _event_logger.info("%s", event)


def log_ui_flush(event: str = "", **kwargs: Any) -> None:
    if event:
        log_ui(event, **kwargs)
    for handler in logging.getLogger().handlers:
        if hasattr(handler, "flush"):
            try:
                handler.flush()
            except Exception:
                pass


def preview_paths(paths: list[str | Path]) -> str:
    if not paths:
        return "[]"
    if len(paths) <= 5:
        return json.dumps([str(Path(p).name) for p in paths], ensure_ascii=False)
    first = [str(Path(p).name) for p in paths[:3]]
    return json.dumps(first + [f"... ({len(paths)} total)"], ensure_ascii=False)