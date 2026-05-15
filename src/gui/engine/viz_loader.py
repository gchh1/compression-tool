"""Binary .viz file reader for compression visualization data.

File formats
------------
v1 (interleaved) — deprecated, kept for backward compat:
    Header:  "VIZ0"(4) + version=1(4) = 8 bytes
    Events:  [type_tag:u8][payload:variable]...
    Footer:  num_sections:u32 + 4×(type:u32+byte_size:u64) + event_count:u32 + footer_start:u64

v2 (section-based):
    Header:        "VIZ0"(4) + version=2(4) = 8 bytes
    Section table: 4×(type:u32 + offset:u64 + size:u64) = 80 bytes
    Section 0:     MatchEvent[9] × N0
    Section 1:     BlockBoundary[24] × N1
    Section 2:     HuffmanTreeBuilt[7+alpha_sz] × N2
    Section 3:     DPStateEvent[17] × N3
    Footer:        total_events:u32 + footer_start:u64 = 12 bytes
"""

from __future__ import annotations

import mmap
import struct
from dataclasses import dataclass
from typing import BinaryIO

# ── Event dataclasses ──────────────────────────────────────────────

@dataclass(slots=True)
class MatchEvent:
    input_pos: int
    offset: int
    length: int
    literal: int


@dataclass(slots=True)
class BlockBoundary:
    block_index: int
    input_start: int
    input_bytes: int
    literal_count: int
    match_count: int
    output_bytes: int


@dataclass(slots=True)
class HuffmanTreeBuilt:
    block_index: int
    tree_type: int
    alphabet_size: int
    code_lengths: bytes


@dataclass(slots=True)
class DPStateEvent:
    position: int
    token_count: int
    predecessor: int
    match_offset: int
    match_length: int
    is_chosen: bool


# ── Constants ──────────────────────────────────────────────────────

VIZ_MAGIC = 0x305A4956   # "VIZ0" LE
VIZ_HEADER_SIZE = 8       # magic + version
NUM_EVENT_TYPES = 4

# v1 serialized payload sizes (WITH 1-byte type tag)
EVENT_SIZES_V1 = {0: 10, 1: 25, 2: -1, 3: 18}

# v2 serialized payload sizes (NO type tag)
EVENT_SIZES_V2 = {0: 9, 1: 24, 2: -1, 3: 17}

# Struct formats for fixed-size events (no type tag — v2)
MATCH_FMT = struct.Struct("<I H H B")       # 9 bytes
BLOCK_FMT = struct.Struct("<I I I I I I")    # 24 bytes
DPSTATE_FMT = struct.Struct("<I I I H H B")  # 17 bytes

# v1 formats (with leading type tag skipped by caller)
MATCH_FMT_V1 = struct.Struct("<x I H H B")         # skip type, 9 payload
BLOCK_FMT_V1 = struct.Struct("<x I I I I I I")     # skip type, 24 payload
DPSTATE_FMT_V1 = struct.Struct("<x I I I H H B")   # skip type, 17 payload

# ── mmap page size for lazy MatchEvent loading ─────────────────────

MATCH_PAGE_BYTES = 256 * 1024  # 256 KB per page (~28K events)


class VizLoader:
    """Reads a .viz binary file (v1 or v2).

    v2 files are memory-mapped for O(1) random access to fixed-size
    events.  v1 files (legacy) fall back to sequential scanning.
    """

    def __init__(self, path: str) -> None:
        self._path = path
        self._version: int = 0
        self._section_offsets: dict[int, int] = {}
        self._section_sizes: dict[int, int] = {}
        self._total_events: int = 0

        self._mm: mmap.mmap | None = None
        self._file: BinaryIO | None = None

        self._parse_metadata()

    # ── public properties ──────────────────────────────────────────

    @property
    def version(self) -> int:
        return self._version

    @property
    def total_events(self) -> int:
        return self._total_events

    @property
    def section_sizes(self) -> dict[int, int]:
        return dict(self._section_sizes)

    @property
    def match_count(self) -> int:
        sz = self._section_sizes.get(0, 0)
        return sz // 9 if self._version >= 2 else 0

    @property
    def block_count(self) -> int:
        sz = self._section_sizes.get(1, 0)
        return sz // 24 if self._version >= 2 else 0

    @property
    def dp_count(self) -> int:
        sz = self._section_sizes.get(3, 0)
        return sz // 17 if self._version >= 2 else 0

    # ── mmap init ──────────────────────────────────────────────────

    def _ensure_mmap(self) -> mmap.mmap:
        if self._mm is not None:
            return self._mm
        self._file = open(self._path, "rb")
        self._mm = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
        return self._mm

    def close(self) -> None:
        if self._mm is not None:
            self._mm.close()
            self._mm = None
        if self._file is not None:
            self._file.close()
            self._file = None

    # ── metadata parse ─────────────────────────────────────────────

    def _parse_metadata(self) -> None:
        with open(self._path, "rb") as f:
            header = f.read(VIZ_HEADER_SIZE)
            if len(header) < VIZ_HEADER_SIZE:
                raise ValueError("Truncated .viz header")
            magic, self._version = struct.unpack("<I I", header)
            if magic != VIZ_MAGIC:
                raise ValueError(
                    f"Bad .viz magic: 0x{magic:08X} (expected 0x{VIZ_MAGIC:08X})"
                )

            if self._version == 2:
                self._parse_v2_metadata(f)
            elif self._version == 1:
                self._parse_v1_metadata(f)
            else:
                raise ValueError(f"Unsupported .viz version: {self._version}")

    def _parse_v2_metadata(self, f: BinaryIO) -> None:
        # Section table: 4 × (type:u32 + offset:u64 + size:u64) = 80 bytes
        table = f.read(NUM_EVENT_TYPES * 20)
        for i in range(NUM_EVENT_TYPES):
            off = i * 20
            type_tag = struct.unpack_from("<I", table, off)[0]
            section_off = struct.unpack_from("<Q", table, off + 4)[0]
            section_sz = struct.unpack_from("<Q", table, off + 12)[0]
            self._section_offsets[type_tag] = section_off
            self._section_sizes[type_tag] = section_sz

        # Footer at end
        f.seek(-12, 2)
        self._total_events = struct.unpack("<I", f.read(4))[0]
        # footer_start = struct.unpack("<Q", f.read(8))[0]

    def _parse_v1_metadata(self, f: BinaryIO) -> None:
        f.seek(-8, 2)
        (footer_start,) = struct.unpack("<Q", f.read(8))
        f.seek(footer_start)
        num_sections = struct.unpack("<I", f.read(4))[0]
        for _ in range(num_sections):
            type_tag = struct.unpack("<I", f.read(4))[0]
            byte_size = struct.unpack("<Q", f.read(8))[0]
            self._section_sizes[type_tag] = byte_size
        self._total_events = struct.unpack("<I", f.read(4))[0]

    # ── v2 O(1) random access (mmap) ───────────────────────────────

    def get_match_event(self, idx: int) -> MatchEvent | None:
        """O(1) single MatchEvent access via mmap (v2 only)."""
        if self._version < 2:
            raise RuntimeError("Random access requires .viz v2")
        off = self._section_offsets.get(0)
        if off is None:
            return None
        mm = self._ensure_mmap()
        pos = off + idx * 9
        if pos + 9 > off + self._section_sizes.get(0, 0):
            return None
        return MatchEvent(*MATCH_FMT.unpack(mm[pos:pos + 9]))

    def get_match_page(self, start_idx: int, page_bytes: int = MATCH_PAGE_BYTES) -> list[MatchEvent]:
        """Load a page of MatchEvents around *start_idx* via mmap (v2 only).

        Returns events in [start_idx, start_idx + N) where N ≈ page_bytes // 9.
        Always loads whole events (no partial reads).
        """
        if self._version < 2:
            raise RuntimeError("Random access requires .viz v2")
        off = self._section_offsets.get(0)
        size = self._section_sizes.get(0, 0)
        if off is None or size == 0:
            return []

        elem_sz = 9
        max_idx = size // elem_sz
        if start_idx >= max_idx:
            return []

        count = min(page_bytes // elem_sz, max_idx - start_idx)
        mm = self._ensure_mmap()
        data = mm[off + start_idx * elem_sz : off + (start_idx + count) * elem_sz]

        results: list[MatchEvent] = []
        for i in range(count):
            results.append(MatchEvent(*MATCH_FMT.unpack_from(data, i * elem_sz)))
        return results

    # ── v2 bulk load (still uses mmap, returns Python objects) ─────

    def load_match_events(self) -> list[MatchEvent]:
        if self._version >= 2:
            return self._load_fixed_section(0, 9, MATCH_FMT, MatchEvent)
        return self._scan_v1(0, MATCH_FMT_V1, MatchEvent)

    def load_block_boundaries(self) -> list[BlockBoundary]:
        if self._version >= 2:
            return self._load_fixed_section(1, 24, BLOCK_FMT, BlockBoundary)
        return self._scan_v1(1, BLOCK_FMT_V1, BlockBoundary)

    def load_huffman_trees(self) -> list[HuffmanTreeBuilt]:
        if self._version >= 2:
            return self._load_huffman_v2()
        return list(self._scan_v1_huffman())

    def load_dp_states(self) -> list[DPStateEvent]:
        if self._version >= 2:
            result = self._load_fixed_section(3, 17, DPSTATE_FMT, DPStateEvent)
            for r in result:
                r.is_chosen = bool(r.is_chosen)
            return result
        result = self._scan_v1(3, DPSTATE_FMT_V1, DPStateEvent)
        for r in result:
            r.is_chosen = bool(r.is_chosen)
        return result

    # ── v2 internal helpers ────────────────────────────────────────

    def _load_fixed_section(self, type_tag: int, elem_sz: int,
                            fmt: struct.Struct, cls: type) -> list:
        off = self._section_offsets.get(type_tag)
        size = self._section_sizes.get(type_tag, 0)
        if off is None or size == 0:
            return []

        count = size // elem_sz
        mm = self._ensure_mmap()
        data = mm[off : off + size]

        results: list = []
        for i in range(count):
            results.append(cls(*fmt.unpack_from(data, i * elem_sz)))
        return results

    def _load_huffman_v2(self) -> list[HuffmanTreeBuilt]:
        off = self._section_offsets.get(2)
        size = self._section_sizes.get(2, 0)
        if off is None or size == 0:
            return []

        mm = self._ensure_mmap()
        results: list[HuffmanTreeBuilt] = []
        pos = off
        end = off + size
        while pos + 7 <= end:
            block_idx = struct.unpack_from("<I", mm, pos)[0]
            tree_type = mm[pos + 4]
            alpha_sz = struct.unpack_from("<H", mm, pos + 5)[0]
            pos += 7
            lens = bytes(mm[pos : pos + alpha_sz])
            pos += alpha_sz
            results.append(HuffmanTreeBuilt(block_idx, tree_type, alpha_sz, lens))
        return results

    # ── v1 fallback (sequential scan) ──────────────────────────────

    def _scan_v1(self, type_tag: int, fmt: struct.Struct, cls: type) -> list:
        """Sequentially scan v1 file for events of *type_tag*."""
        payload_size = fmt.size  # already excludes type tag byte
        results: list = []
        with open(self._path, "rb") as f:
            f.seek(VIZ_HEADER_SIZE)
            footer_start = self._read_v1_footer_start(f)
            end = footer_start
            while f.tell() < end:
                tag = f.read(1)
                if not tag:
                    break
                if tag[0] == type_tag:
                    data = f.read(payload_size)
                    results.append(cls(*fmt.unpack(data)))
                elif tag[0] == 2:
                    # Huffman — variable size
                    hdr = f.read(7)
                    _, _, alpha_sz = struct.unpack("<I B H", hdr)
                    f.seek(alpha_sz, 1)
                else:
                    skip = EVENT_SIZES_V1.get(tag[0], 0)
                    if skip > 0:
                        f.seek(skip - 1, 1)  # -1 because we already read the tag
        return results

    def _read_v1_footer_start(self, f: BinaryIO) -> int:
        f.seek(-8, 2)
        (fs,) = struct.unpack("<Q", f.read(8))
        f.seek(VIZ_HEADER_SIZE)
        return fs

    def _scan_v1_huffman(self):
        results: list[HuffmanTreeBuilt] = []
        with open(self._path, "rb") as f:
            f.seek(VIZ_HEADER_SIZE)
            footer_start = self._read_v1_footer_start(f)
            end = footer_start
            while f.tell() < end:
                tag = f.read(1)
                if not tag:
                    break
                if tag[0] == 2:
                    block_idx = struct.unpack("<I", f.read(4))[0]
                    tree_type = f.read(1)[0]
                    alpha_sz = struct.unpack("<H", f.read(2))[0]
                    lens = f.read(alpha_sz)
                    results.append(HuffmanTreeBuilt(block_idx, tree_type, alpha_sz, lens))
                else:
                    skip = EVENT_SIZES_V1.get(tag[0], 0)
                    if skip > 0:
                        f.seek(skip - 1, 1)
        return results

    def __repr__(self) -> str:
        return (
            f"VizLoader({self._path!r}, v{self._version}, "
            f"events={self._total_events}, sizes={self._section_sizes})"
        )
