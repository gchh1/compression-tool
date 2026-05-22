"""Structured logging for decompression (memory + file pipeline).

All messages use the ``gui.decompress`` logger with a stable ``[decompress]`` prefix
and ``key=value`` fields for grep in ``gui.log``.

**WCX → 磁盘解压策略（与规格对齐；勿与 LZDP「二分块」混淆）**

1. **默认策略 1（整包在内存，整段解码，再落盘）**：WCX 整文件读入 Python → ``unpack`` → ``pipeline_decompress``
   （C++ ``api::decompress`` + ``Pipeline``；``memory::MemoryPool`` 见 ``src/utils/include/MemoryPool.hpp``，仅用于管线
   **内部**固定块复用，整段 payload / 明文仍为连续缓冲）→ 解码完成后 **一次性** 写入目标文件。
2. **整包在内存，解码器流式产出、分块写盘**：payload 仍一次在内存，但管线 ``pull`` 多次，按块 ``write``，
   降低单次写缓冲峰值（设计层概念；与 ``streaming-compression-design.md`` 里 **Phase 2 二分块**
   ——temp A/B 上 **cur/next 双块滚动**——不是「把输出文件对半切」）。
3. **分块读压缩体进内存，边解边写、释放读缓冲**：``WEBCOMPRESS_NATIVE_DECOMPRESS_FILE=1`` 时走 C++
   ``decompressFile`` / ``pipeline_decompress_file``；payload 大于阈值则按 ``stream_chunk_bytes`` 分块读入。

- C++ ``decompressFile``：设 ``WEBCOMPRESS_DECOMPRESS_DEBUG=1`` 可在 **stderr** 打 ``[decompress][native]`` 行。
"""

from __future__ import annotations

import logging
from typing import Any

logger = logging.getLogger("gui.decompress")


def log_decompress(event: str, *, level: int = logging.INFO, **fields: Any) -> None:
    """Emit one line: ``[decompress] <event> k=v ...``."""
    parts: list[str] = [event]
    for key in sorted(fields):
        val = fields[key]
        if val is None:
            continue
        s = str(val).replace("\n", "\\n")
        if len(s) > 800:
            s = s[:797] + "..."
        parts.append(f"{key}={s}")
    msg = " ".join(parts)
    logger.log(level, "[decompress] %s", msg)


def summarize_result(obj: Any) -> dict[str, Any]:
    """Pick common fields from CompressResult / PipelineCompressResult bindings."""
    out: dict[str, Any] = {}
    for k in (
        "success",
        "original_size",
        "compressed_size",
        "compression_ratio",
        "time_ms",
        "error_message",
    ):
        if hasattr(obj, k):
            out[k] = getattr(obj, k)
    if hasattr(obj, "data"):
        try:
            d = getattr(obj, "data")
            out["out_bytes_len"] = len(d) if d is not None else 0
        except Exception:
            out["out_bytes_len"] = -1
    return out
