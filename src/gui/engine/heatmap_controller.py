"""Lazy mmap-based reader for .heat v2 entropy files.

Format::

    Header (20 bytes):
      magic:         u32 = 0x54414548 ("HEAT" LE)
      version:       u32 = 2
      total_chunks:  u32
      chunk_bytes:   u32
      avg_entropy:    f32

    Chunk Data Array:
      [total_chunks × f32 entropy]
"""

from __future__ import annotations

import logging
import math
import mmap
import struct
from pathlib import Path
from typing import BinaryIO

logger = logging.getLogger(__name__)

HEAT_MAGIC = 0x54414548  # "HEAT" LE
HEAT_VERSION = 2

_HEADER_FMT = struct.Struct("<I I I I f")   # magic, version, total_chunks, chunk_bytes, avg_entropy
_CHUNK_FMT = struct.Struct("<f")             # single entropy float
_HEADER_SIZE = _HEADER_FMT.size              # 20 bytes


class HeatmapController:
    """Lazy mmap-based reader for .heat v2 files (single-file entropy data).

    - Opens .heat via mmap on first access (``_ensure_mmap``).
    - Header parsed immediately (20 bytes) — O(1).
    - Chunk entropy values accessed via O(1) mmap slice.
    - Byte-level heatmap slices from the original file via transient mmap.
    """

    def __init__(self, heat_path: str) -> None:
        self._heat_path = heat_path
        self._mm: mmap.mmap | None = None
        self._f: BinaryIO | None = None
        self._total_chunks: int = 0
        self._chunk_bytes: int = 0
        self._avg_entropy: float = 0.0
        self._parse_header()

    # ── internal ─────────────────────────────────────────────────

    def _ensure_mmap(self) -> mmap.mmap:
        if self._mm is not None:
            return self._mm
        self._f = open(self._heat_path, "rb")
        self._mm = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)
        return self._mm

    def _parse_header(self) -> None:
        mm = self._ensure_mmap()
        if len(mm) < _HEADER_SIZE:
            raise ValueError(
                f"{self._heat_path}: file too small for .heat v2 header "
                f"({len(mm)} < {_HEADER_SIZE})"
            )

        magic, version, self._total_chunks, self._chunk_bytes, self._avg_entropy = \
            _HEADER_FMT.unpack(mm[0:_HEADER_SIZE])

        if magic != HEAT_MAGIC:
            raise ValueError(
                f"{self._heat_path}: bad magic 0x{magic:08X}, "
                f"expected 0x{HEAT_MAGIC:08X}"
            )
        if version != HEAT_VERSION:
            raise ValueError(
                f"{self._heat_path}: unsupported version {version} "
                f"(expected {HEAT_VERSION})"
            )

    def close(self) -> None:
        if self._mm is not None:
            self._mm.close()
            self._mm = None
        if self._f is not None:
            self._f.close()
            self._f = None

    # ── Properties ───────────────────────────────────────────────

    @property
    def total_chunks(self) -> int:
        return self._total_chunks

    @property
    def chunk_bytes(self) -> int:
        return self._chunk_bytes

    @property
    def avg_entropy(self) -> float:
        return self._avg_entropy

    @property
    def estimated_input_bytes(self) -> int:
        """Nominal input size (chunk_bytes × total_chunks)."""
        return self._chunk_bytes * self._total_chunks

    @property
    def entropy_array(self) -> list[float]:
        """Lazy mmap read of all chunk entropy values."""
        if self._total_chunks == 0:
            return []
        mm = self._ensure_mmap()
        off = _HEADER_SIZE
        count = self._total_chunks
        entropy_bytes = mm[off:off + count * 4]
        return [
            _CHUNK_FMT.unpack(entropy_bytes[i:i + 4])[0]
            for i in range(0, len(entropy_bytes), 4)
        ]

    @property
    def macro_data(self) -> dict:
        """Return macro data payload compatible with ThreeTierHeatmapDialog.

        v2 is single-file, so ``files`` is always a single-element list.
        """
        name = Path(self._heat_path).stem
        entropy = self.entropy_array
        return {
            "total_input_bytes": self.estimated_input_bytes,
            "chunk_bytes": self._chunk_bytes,
            "total_chunks": self._total_chunks,
            "files": [{
                "name": name,
                "global_start": 0,
                "global_end": self.estimated_input_bytes,
                "input_bytes": self.estimated_input_bytes,
                "chunk_count": self._total_chunks,
                "entropy_array": entropy,
            }],
        }

    # ── Micro API (mmap original file) ───────────────────────────

    @staticmethod
    def slice_byte_heatmap(filepath: str, offset: int, size: int) -> dict:
        """mmap *filepath* at [offset, offset+size), return per-byte heat array.

        Returns a dict with:
          - ``bytes``: list of int (raw byte values 0-255)
          - ``offset``: actual start offset
          - ``size``: actual byte count returned
          - ``entropy_64k``: Shannon entropy of this slice (or 0.0 on empty)
        """
        path = Path(filepath)
        if not path.is_file():
            raise FileNotFoundError(f"File not found: {filepath}")

        file_size = path.stat().st_size
        actual_offset = max(0, min(offset, file_size))
        actual_size = max(0, min(size, file_size - actual_offset))

        if actual_size == 0:
            return {"bytes": [], "offset": actual_offset, "size": 0, "entropy_64k": 0.0}

        f: BinaryIO | None = None
        mm: mmap.mmap | None = None
        try:
            f = open(filepath, "rb")
            mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

            raw = mm[actual_offset:actual_offset + actual_size]
            byte_values = list(raw)

            counts = [0] * 256
            for b in byte_values:
                counts[b] += 1
            entropy = 0.0
            inv = 1.0 / actual_size
            for c in counts:
                if c > 0:
                    p = c * inv
                    entropy -= p * math.log2(p)

            return {
                "bytes": byte_values,
                "offset": actual_offset,
                "size": actual_size,
                "entropy_64k": round(entropy, 4),
            }
        finally:
            if mm is not None:
                mm.close()
            if f is not None:
                f.close()
