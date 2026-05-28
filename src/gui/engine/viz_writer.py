"""Pure-Python .viz v2 binary file writer for DEFLATE process replay.

Format matches C++ DiskVizObserver v2 format:
  magic(4B)=0x305A4956 "VIZ0"
  version(4B)=2
  table: 5 entries × (type_id:u32 + offset:u64 + size:u64) = 100 bytes
  section data: per-type event payloads
  footer: total_events:u32 + footer_start:u64

Event types and payload sizes:
  0 = MatchEvent     — 9 bytes  (input_pos:u32 + offset:u16 + length:u16 + literal:u8)
  1 = BlockBoundary  — 24 bytes (block_index:u32 + input_start:u32 + input_bytes:u32
                                   + literal_count:u32 + match_count:u32 + output_bytes:u32)
  2 = HuffmanTreeBuilt — variable
  3 = DPStateEvent   — 18 bytes
  4 = DPCandidateEvent — 10 bytes
"""

from __future__ import annotations

import logging
import struct
from pathlib import Path

logger = logging.getLogger(__name__)

VIZ_MAGIC = 0x305A4956
VIZ_VERSION = 2
NUM_EVENT_TYPES = 5
TABLE_ENTRY_STRUCT = struct.Struct("<IQQ")
HEADER_STRUCT = struct.Struct("<II")
MATCH_EVENT_STRUCT = struct.Struct("<IHHB")
BLOCK_BOUNDARY_STRUCT = struct.Struct("<IIIIII")


class VizWriter:
    """Write .viz v2 binary file from parsed compression tokens."""

    def __init__(self, output_path: str):
        self._path = Path(output_path)
        self._match_payloads: list[bytes] = []
        self._block_payloads: list[bytes] = []
        self._huffman_payloads: list[bytes] = []
        self._dp_state_payloads: list[bytes] = []
        self._dp_candidate_payloads: list[bytes] = []
        self._total_events = 0

    def add_match(self, input_pos: int, offset: int, length: int, literal: int = 0) -> None:
        if offset > 0xFFFF:
            offset = 0xFFFF
        if length > 0xFFFF:
            length = 0xFFFF
        payload = MATCH_EVENT_STRUCT.pack(
            input_pos & 0xFFFFFFFF,
            offset & 0xFFFF,
            length & 0xFFFF,
            literal & 0xFF,
        )
        self._match_payloads.append(payload)
        self._total_events += 1

    def add_block_boundary(
        self,
        block_index: int,
        input_start: int,
        input_bytes: int,
        literal_count: int,
        match_count: int,
        output_bytes: int,
    ) -> None:
        payload = BLOCK_BOUNDARY_STRUCT.pack(
            block_index & 0xFFFFFFFF,
            input_start & 0xFFFFFFFF,
            input_bytes & 0xFFFFFFFF,
            literal_count & 0xFFFFFFFF,
            match_count & 0xFFFFFFFF,
            output_bytes & 0xFFFFFFFF,
        )
        self._block_payloads.append(payload)
        self._total_events += 1

    def add_huffman_tree(
        self,
        block_index: int,
        tree_type: int,
        code_lengths: list[int],
    ) -> None:
        alphabet_size = len(code_lengths)
        payload = struct.pack("<IBH", block_index & 0xFFFFFFFF, tree_type & 0xFF, alphabet_size & 0xFFFF)
        payload += struct.pack(f"<{alphabet_size}B", *[cl & 0xFF for cl in code_lengths])
        self._huffman_payloads.append(payload)
        self._total_events += 1

    def add_dp_state(
        self,
        position: int,
        token_count: int,
        predecessor: int,
        match_offset: int,
        match_length: int,
        is_chosen: int,
        reachable: int,
    ) -> None:
        payload = struct.pack(
            "<IIIHHBB",
            position & 0xFFFFFFFF,
            token_count & 0xFFFFFFFF,
            predecessor & 0xFFFFFFFF,
            match_offset & 0xFFFF,
            match_length & 0xFFFF,
            is_chosen & 0xFF,
            reachable & 0xFF,
        )
        self._dp_state_payloads.append(payload)
        self._total_events += 1

    def add_dp_candidate(
        self,
        position: int,
        offset: int,
        length: int,
        literal: int,
        is_chosen: int,
    ) -> None:
        payload = struct.pack(
            "<IHHBB",
            position & 0xFFFFFFFF,
            offset & 0xFFFF,
            length & 0xFFFF,
            literal & 0xFF,
            is_chosen & 0xFF,
        )
        self._dp_candidate_payloads.append(payload)
        self._total_events += 1

    def write(self) -> None:
        sections: list[tuple[int, list[bytes]]] = [
            (0, self._match_payloads),
            (1, self._block_payloads),
            (2, self._huffman_payloads),
            (3, self._dp_state_payloads),
            (4, self._dp_candidate_payloads),
        ]

        header_size = 8
        table_size = NUM_EVENT_TYPES * TABLE_ENTRY_STRUCT.size
        data_start = header_size + table_size

        section_offsets: list[int] = []
        section_sizes: list[int] = []
        offset = data_start
        for _, payloads in sections:
            section_offsets.append(offset)
            size = sum(len(p) for p in payloads)
            section_sizes.append(size)
            offset += size

        with open(self._path, "wb") as f:
            f.write(HEADER_STRUCT.pack(VIZ_MAGIC, VIZ_VERSION))

            for type_id in range(NUM_EVENT_TYPES):
                i = type_id
                f.write(TABLE_ENTRY_STRUCT.pack(
                    type_id,
                    section_offsets[i],
                    section_sizes[i],
                ))

            for _, payloads in sections:
                for p in payloads:
                    f.write(p)

            footer_start = f.tell()
            f.write(struct.pack("<IQ", self._total_events & 0xFFFFFFFF, footer_start))

    def generate_from_tokens(
        self,
        tokens: list[object],
        raw_size: int,
        compressed_size: int,
        raw_data: bytes | None = None,
        block_index: int = 0,
        block_start: int = 0,
    ) -> None:
        """Generate MatchEvents and BlockBoundary from parsed tokens.

        Each token has:
          - type: TokenType (LITERAL / MATCH / LITERAL_RUN)
          - original_start: int — byte offset in original data
          - original_length: int — bytes covered by this token
          - match_offset: int — match distance (0 = literal)

        For LITERAL / LITERAL_RUN tokens, the literal bytes are read from raw_data.
        """
        pos = block_start
        literal_count = 0
        match_count = 0
        input_bytes = 0
        try:
            for token in tokens:
                tok_type = getattr(token, "type", None)
                t_name = str(tok_type.value) if hasattr(tok_type, "value") else str(tok_type)
                off = getattr(token, "match_offset", 0)
                lng = getattr(token, "original_length", 0)
                start = getattr(token, "original_start", 0)

                if t_name == "match" and off > 0 and lng > 0:
                    match_count += 1
                    if lng > 0xFFFF:
                        lng = 0xFFFF
                    self.add_match(pos, off, lng, 0)
                    pos += lng
                    input_bytes += lng
                elif t_name in ("literal", "literal_run"):
                    if raw_data and start < len(raw_data):
                        lit = raw_data[start]
                    else:
                        lit = 0
                    literal_count += 1
                    self.add_match(pos, 0, 0, lit)
                    pos += 1
                    input_bytes += 1
                else:
                    lit_count = max(1, lng)
                    literal_count += lit_count
                    for i in range(lit_count):
                        if raw_data and start + i < len(raw_data):
                            lit = raw_data[start + i]
                        else:
                            lit = 0
                        self.add_match(pos + i, 0, 0, lit)
                    pos += lit_count
                    input_bytes += lit_count
        except Exception as e:
            logger.warning("[VizWriter] token parse error at pos=%d: %s", pos, e)

        if input_bytes == 0:
            input_bytes = raw_size

        self.add_block_boundary(
            block_index=block_index,
            input_start=block_start,
            input_bytes=input_bytes,
            literal_count=literal_count,
            match_count=match_count,
            output_bytes=compressed_size,
        )

    def close(self) -> None:
        self.write()