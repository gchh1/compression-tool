from __future__ import annotations

import logging

logger = logging.getLogger("gui.decompress")


def log_decompress(event: str, **fields):
    extra = {}
    for k, v in fields.items():
        if v is not None:
            extra[k] = v
    level = extra.pop("level", logging.INFO)
    if extra:
        logger.log(level, "[%s] %s", event, " ".join(f"{k}={v}" for k, v in extra.items()))
    else:
        logger.log(level, "[%s]", event)


def summarize_result(result) -> dict:
    out = {}
    for attr in ("success", "original_size", "compressed_size", "compression_ratio",
                  "time_ms", "error_message", "data"):
        if hasattr(result, attr):
            v = getattr(result, attr)
            if attr == "data" and v is not None:
                out["data_bytes"] = len(v) if isinstance(v, (bytes, bytearray)) else 0
            elif v is not None:
                out[attr] = v
    return out