"""Per-character encoding-ratio computation from .viz v2 data.

Uses MatchEvent + HuffmanTreeBuilt events to compute the compression
ratio for each byte position in the original file:

    ratio = encoded_bits / 8.0

For literal bytes, encoded_bits is the Huffman code length of that
literal value.  For match bytes, the (length_code + distance_code)
cost is amortized over the match span.
"""

from __future__ import annotations

import bisect
import logging
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from gui.engine.viz_loader import VizLoader, MatchEvent, BlockBoundary, HuffmanTreeBuilt

logger = logging.getLogger(__name__)

# ── RFC 1951 length / distance tables ──────────────────────────────

# Length code → (base_length, extra_bits)
_LENGTH_TABLE: list[tuple[int, int]] = [
    (3, 0), (4, 0), (5, 0), (6, 0), (7, 0), (8, 0), (9, 0),
    (10, 1), (11, 1), (12, 1), (13, 1),
    (15, 2), (17, 2), (19, 2), (23, 2),
    (27, 3), (31, 3), (35, 3), (43, 3),
    (51, 4), (59, 4), (67, 4), (83, 4),
    (99, 5), (115, 5), (131, 5), (163, 5),
    (195, 6), (227, 6), (258, 0),
]

# Distance code → (base_distance, extra_bits)
_DIST_TABLE: list[tuple[int, int]] = [
    (1, 0), (2, 0), (3, 0), (4, 0),
    (5, 1), (7, 1),
    (9, 2), (13, 2),
    (17, 3), (25, 3),
    (33, 4), (49, 4),
    (65, 5), (97, 5),
    (129, 6), (193, 6),
    (257, 7), (385, 7),
    (513, 8), (769, 8),
    (1025, 9), (1537, 9),
    (2049, 10), (3073, 10),
    (4097, 11), (6145, 11),
    (8193, 12), (12289, 12),
    (16385, 13), (24577, 13),
]


def _length_code_for_length(match_length: int) -> int:
    """Map match length (3..258) to Deflate length code (257..285)."""
    for code_idx in range(len(_LENGTH_TABLE) - 1, -1, -1):
        base, _extra = _LENGTH_TABLE[code_idx]
        if match_length >= base:
            return 257 + code_idx
    return 257  # fallback: code for length 3


def _distance_code_for_distance(distance: int) -> int:
    """Map match distance (1..32768) to Deflate distance code (0..29)."""
    for code_idx in range(len(_DIST_TABLE) - 1, -1, -1):
        base, _extra = _DIST_TABLE[code_idx]
        if distance >= base:
            return code_idx
    return 0


# ── Canonical Huffman code builder ─────────────────────────────────

def _build_canonical_lengths(code_lengths: bytes, alphabet_size: int) -> list[int]:
    """Return code bit-lengths for each symbol in the alphabet."""
    lengths = list(code_lengths[:alphabet_size])
    # Pad to alphabet_size with zeros (unused symbols)
    if len(lengths) < alphabet_size:
        lengths.extend([0] * (alphabet_size - len(lengths)))
    return lengths


# ── CharEncodingRatioBuilder ───────────────────────────────────────

class CharEncodingRatioBuilder:
    """Compute per-byte encoding ratio from .viz v2 events.

    Uses MatchEvent (character-level match/literal status) and
    HuffmanTreeBuilt (exact code lengths) to compute:

        ratio = encoded_bits / original_bits

    for each byte position.  Results are cached per Deflate block for
    efficient repeated queries.
    """

    def __init__(self, viz_loader: VizLoader | None) -> None:
        self._viz = viz_loader
        self._blocks: list[BlockBoundary] = []
        self._huffman_trees: list[HuffmanTreeBuilt] = []
        self._matches: list[MatchEvent] = []
        self._match_count: int = 0

        # Per-block cache: block_index → (start_offset, end_offset, [ratios])
        self._block_cache: dict[int, tuple[int, int, list[float]]] = {}
        self._match_positions: list[int] = []  # input_pos of loaded MatchEvent slice

        # Track the byte range for which MatchEvents are currently loaded
        self._matches_loaded_start: int = -1
        self._matches_loaded_end: int = -1

        if viz_loader is not None:
            self._load_data()

    # ── data loading ───────────────────────────────────────────────

    def _load_data(self) -> None:
        try:
            self._blocks = self._viz.load_block_boundaries()
        except Exception:
            self._blocks = []

        try:
            self._huffman_trees = self._viz.load_huffman_trees()
        except Exception:
            self._huffman_trees = []

        # DO NOT load all MatchEvents — for large files (1GB+) there can be
        # millions of events.  Instead, use _ensure_matches() to mmap-load
        # only the events covering the query range on demand.
        self._match_count = self._viz.match_count if self._viz else 0

    @property
    def has_viz(self) -> bool:
        return self._match_count > 0

    @property
    def block_count(self) -> int:
        return len(self._blocks)

    @property
    def total_input_bytes(self) -> int:
        if self._blocks:
            last = self._blocks[-1]
            return last.input_start + last.input_bytes
        return 0

    # ── block-level compression ratio ──────────────────────────────

    def block_compression_ratios(self) -> list[dict]:
        """Return per-block compression stats for Tier 1 / Tier 2.

        Each dict: ``{block_index, input_start, input_bytes, output_bytes, ratio}``
        """
        results: list[dict] = []
        for b in self._blocks:
            ratio = b.output_bytes / max(b.input_bytes, 1)
            results.append({
                "block_index": b.block_index,
                "input_start": b.input_start,
                "input_bytes": b.input_bytes,
                "output_bytes": b.output_bytes,
                "ratio": round(ratio, 4),
            })
        return results

    def overall_compression_ratio(self) -> float:
        """Aggregate compression ratio across all blocks."""
        total_in = sum(b.input_bytes for b in self._blocks)
        total_out = sum(b.output_bytes for b in self._blocks)
        if total_in == 0:
            return 1.0
        return total_out / total_in

    # ── lazy MatchEvent loading ────────────────────────────────────

    def _ensure_matches(self, range_start: int, range_end: int) -> None:
        """Load MatchEvents covering [*range_start*, *range_end*) from mmap.

        Uses binary search via O(1) mmap reads — only the events actually
        needed for the query range are materialised into Python objects.
        Results are cached in ``_matches`` / ``_match_positions`` so
        subsequent calls within (or near) the same range are no-ops.
        """
        if self._match_count == 0:
            self._matches = []
            self._match_positions = []
            return

        # Already loaded for this range (with 10 % margin)
        margin = max(1024, int((range_end - range_start) * 0.1))
        if (self._matches_loaded_start <= range_start - margin and
                self._matches_loaded_end >= range_end + margin and
                self._matches):
            return

        count = self._match_count

        # ── Binary search for first event with input_pos >= range_start ──
        lo, hi = 0, count
        while lo < hi:
            mid = (lo + hi) // 2
            ev = self._viz.get_match_event(mid)
            if ev is None:
                hi = mid
            elif ev.input_pos < range_start:
                lo = mid + 1
            else:
                hi = mid

        # Start a bit earlier to catch events that extend into the range
        idx = max(0, lo - 512)

        self._matches = []
        self._match_positions = []

        while idx < count:
            ev = self._viz.get_match_event(idx)
            if ev is None:
                break
            self._matches.append(ev)
            self._match_positions.append(ev.input_pos)
            if ev.input_pos >= range_end:
                break
            idx += 1

        self._matches_loaded_start = range_start
        self._matches_loaded_end = range_end

    # ── per-character ratio computation ────────────────────────────

    def _find_block_index(self, offset: int) -> int:
        """Find the Deflate block covering byte *offset*."""
        for i, b in enumerate(self._blocks):
            if b.input_start <= offset < b.input_start + b.input_bytes:
                return i
        # Fallback to last block if offset beyond known range
        return len(self._blocks) - 1 if self._blocks else 0

    def _find_match_index(self, byte_offset: int) -> int:
        """Binary-search MatchEvent list for the event covering *byte_offset*."""
        if not self._match_positions:
            return -1
        idx = bisect.bisect_right(self._match_positions, byte_offset) - 1
        if idx < 0:
            return -1

        m = self._matches[idx]
        if m.offset == 0 and m.length == 0:
            # Literal: covers single byte at input_pos
            if m.input_pos == byte_offset:
                return idx
        elif m.offset == 0 and m.length > 0:
            # Literal run: covers [input_pos, input_pos + length)
            if m.input_pos <= byte_offset < m.input_pos + m.length:
                return idx
        else:
            # Match: covers [input_pos, input_pos + length)
            match_len = m.length + 1  # MatchEvent offset stores length-1
            if m.input_pos <= byte_offset < m.input_pos + match_len:
                return idx
        return -1

    def _get_block_huffman(self, block_index: int) -> tuple[list[int], list[int]] | None:
        """Get (lit_len_lengths, dist_lengths) for the given block index.

        Returns None if Huffman data is unavailable (fallback to estimation).
        """
        for ht in self._huffman_trees:
            if ht.block_index == block_index:
                if ht.tree_type == 0:  # lit/len tree
                    lit_len = _build_canonical_lengths(ht.code_lengths, ht.alphabet_size)
                    # Find matching distance tree for same block
                    dist = []
                    for ht2 in self._huffman_trees:
                        if ht2.block_index == block_index and ht2.tree_type == 1:
                            dist = _build_canonical_lengths(ht2.code_lengths, ht2.alphabet_size)
                            break
                    return (lit_len, dist)
        return None

    def _estimate_ratio_heuristic(self, match_event: MatchEvent) -> float:
        """Estimate encoding ratio without Huffman tree data.

        Uses simple heuristics based on match/literal status:
        - Literal → 1.0 (uncompressed)
        - Short distance match → 0.2 (good compression)
        - Far distance match → 0.5 (more expensive distance code)
        """
        if match_event.offset == 0:
            return 1.0  # literal — no compression
        # Match: shorter distance = cheaper encoding
        dist = match_event.offset
        if dist <= 256:
            return 0.15
        elif dist <= 4096:
            return 0.25
        elif dist <= 16384:
            return 0.4
        else:
            return 0.55

    def compute_ratio(self, byte_offset: int) -> float:
        """Compute encoding ratio for a single byte position."""
        match_idx = self._find_match_index(byte_offset)
        if match_idx < 0:
            return 1.0  # no data → assume uncompressed

        m = self._matches[match_idx]
        block_idx = self._find_block_index(byte_offset)
        huff = self._get_block_huffman(block_idx)

        if huff is None:
            return self._estimate_ratio_heuristic(m)

        lit_len_lengths, dist_lengths = huff

        if m.offset == 0 and m.length == 0:
            # Single literal
            literal_byte = m.literal
            if literal_byte < len(lit_len_lengths):
                code_len = lit_len_lengths[literal_byte]
                if code_len > 0:
                    return min(1.2, code_len / 8.0)
            return 1.0

        elif m.offset == 0 and m.length > 0:
            # Literal run: each byte gets the same treatment
            return 1.0  # approximate — could use the literal code lengths

        else:
            # Match: the match covers m.length bytes (note: from .viz, offset>0 means match
            # and the MatchEvent.length is the match length)
            match_len = m.length
            if match_len < 1:
                return 1.0

            # Length code
            length_code = _length_code_for_length(match_len)
            _, length_extra = _LENGTH_TABLE[length_code - 257]
            length_bits = lit_len_lengths[length_code] if length_code < len(lit_len_lengths) else 8
            total_bits = length_bits + length_extra

            # Distance code
            dist_code = _distance_code_for_distance(m.offset)
            _, dist_extra = _DIST_TABLE[dist_code]
            dist_bits = dist_lengths[dist_code] if dist_code < len(dist_lengths) else 8
            total_bits += dist_bits + dist_extra

            per_char_bits = total_bits / match_len
            return max(0.01, min(1.0, per_char_bits / 8.0))

    def build_range(self, start_offset: int, end_offset: int) -> list[float]:
        """Return encoding ratios for every byte in [start_offset, end_offset).

        Uses block-level caching to avoid recomputing for the same range.
        MatchEvents are loaded on demand via mmap for the query range only.
        """
        if start_offset >= end_offset:
            return []

        # Lazily load MatchEvents for this range (mmap + binary search)
        self._ensure_matches(start_offset, end_offset)

        # Compute which blocks intersect [start_offset, end_offset)
        result: list[float] = []
        current = start_offset

        while current < end_offset:
            bi = self._find_block_index(current)
            if bi not in self._block_cache:
                # Compute entire block's ratios at once and cache
                b = self._blocks[bi] if bi < len(self._blocks) else None
                if b is None:
                    # No block data: fall back to per-byte heuristic
                    for pos in range(current, end_offset):
                        result.append(self.compute_ratio(pos))
                    return result

                block_start = b.input_start
                block_end = b.input_start + b.input_bytes
                ratios = [self.compute_ratio(pos) for pos in range(block_start, block_end)]
                self._block_cache[bi] = (block_start, block_end, ratios)
            else:
                block_start, block_end, ratios = self._block_cache[bi]

            # Take the needed portion from the cached block
            take_start = max(current, block_start)
            take_end = min(end_offset, block_end)
            if take_start < take_end:
                result.extend(ratios[take_start - block_start : take_end - block_start])

            current = block_end
            if current <= 0:
                current = end_offset  # safety: prevent infinite loop

        return result

    def build_range_downsampled(self, start_offset: int, end_offset: int,
                                 max_bins: int) -> list[float]:
        """Like build_range() but returns at most *max_bins* averaged values."""
        ratios = self.build_range(start_offset, end_offset)
        n = len(ratios)
        if n == 0:
            return []
        if n <= max_bins:
            return ratios

        result: list[float] = []
        bin_size = n / max_bins
        for i in range(max_bins):
            lo = int(i * bin_size)
            hi = max(lo + 1, int((i + 1) * bin_size))
            result.append(sum(ratios[lo:hi]) / (hi - lo))
        return result
