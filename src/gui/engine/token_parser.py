from __future__ import annotations

import logging
import math
from dataclasses import dataclass, field
from enum import Enum

from gui.models import AlgorithmType, LZDP_DP_VIZ_MAX_SIZE

logger = logging.getLogger(__name__)


class TokenType(Enum):
    LITERAL = "literal"
    MATCH = "match"
    LITERAL_RUN = "literal_run"


@dataclass
class Token:
    type: TokenType
    original_start: int
    original_length: int
    compressed_size: float

    match_offset: int = 0
    huffman_bits: float = 0.0
    huffman_detail: str = ""
    bit_offset: int = 0
    bit_length: int = 0

    @property
    def compression_ratio(self) -> float:
        if self.original_length == 0:
            return 1.0
        return self.compressed_size / self.original_length


@dataclass
class ParseResult:
    tokens: list[Token] = field(default_factory=list)
    original_size: int = 0
    compressed_payload_size: int = 0
    algorithm: str = ""
    dp_steps: list[dict] | None = None
    huffman_trees: list[HuffmanTreeData] | None = None

    @property
    def total_original(self) -> int:
        return sum(t.original_length for t in self.tokens)

    @property
    def total_compressed(self) -> int:
        return sum(t.compressed_size for t in self.tokens)


@dataclass
class HuffmanCodeEntry:
    symbol: int
    frequency: int
    code: str
    code_length: int


@dataclass
class HuffmanTreeNode:
    symbol: int | None = None
    frequency: int = 0
    left: HuffmanTreeNode | None = None
    right: HuffmanTreeNode | None = None

    def is_leaf(self) -> bool:
        return self.left is None and self.right is None


@dataclass
class HuffmanTreeData:
    tree: HuffmanTreeNode
    codes: list[HuffmanCodeEntry]
    tree_type: str = "literal/length"
    total_bits: int = 0


class TokenParser:

    SUPPORTED_ALGORITHMS: frozenset[AlgorithmType] = frozenset()

    def parse(
        self,
        compressed_data: bytes,
        raw_data: bytes | None = None,
        compression_params: dict[str, int] | None = None,
    ) -> ParseResult:
        raise NotImplementedError


class LZSSTokenParser(TokenParser):

    MIN_MATCH_LENGTH = 3

    SUPPORTED_ALGORITHMS = frozenset({AlgorithmType.LZSS})

    def parse(
        self,
        compressed_data: bytes,
        raw_data: bytes | None = None,
        compression_params: dict[str, int] | None = None,
    ) -> ParseResult:
        if len(compressed_data) < 4:
            return ParseResult(algorithm="lzss")

        original_size = (compressed_data[0] << 24) | (compressed_data[1] << 16) | \
                        (compressed_data[2] << 8) | compressed_data[3]

        tokens: list[Token] = []
        pos = 4
        cursor = 0

        while pos < len(compressed_data):
            if pos >= len(compressed_data):
                break
            flag_byte = compressed_data[pos]
            pos += 1

            for bit_idx in range(8):
                if pos >= len(compressed_data) or cursor >= original_size:
                    break

                is_literal = (flag_byte >> bit_idx) & 1

                if is_literal:
                    tokens.append(Token(
                        type=TokenType.LITERAL,
                        original_start=cursor,
                        original_length=1,
                        compressed_size=1,
                    ))
                    cursor += 1
                    pos += 1
                else:
                    if pos + 1 >= len(compressed_data):
                        break
                    token_word = (compressed_data[pos] << 8) | compressed_data[pos + 1]
                    pos += 2

                    match_offset = token_word >> 4
                    match_length = (token_word & 0x0F) + self.MIN_MATCH_LENGTH

                    tokens.append(Token(
                        type=TokenType.MATCH,
                        original_start=cursor,
                        original_length=match_length,
                        compressed_size=2,
                        match_offset=match_offset,
                    ))
                    cursor += match_length

        return ParseResult(
            tokens=tokens,
            original_size=original_size,
            compressed_payload_size=len(compressed_data) - 4,
            algorithm="lzss",
        )


class LZDPTokenParser(TokenParser):

    SEARCH_BYTELENGTH = 2
    LOOKAHEAD_BYTELENGTH = 2

    SUPPORTED_ALGORITHMS = frozenset({AlgorithmType.LZDP})

    def parse(
        self,
        compressed_data: bytes,
        raw_data: bytes | None = None,
        compression_params: dict[str, int] | None = None,
    ) -> ParseResult:
        if raw_data is not None:
            dp_result = self._parse_via_dp(raw_data, compression_params)
            if dp_result is not None:
                return dp_result
        from gui.engine.file_protocol import prepare_token_parse_payload

        compressed_data = prepare_token_parse_payload(
            compressed_data, AlgorithmType.LZDP
        )
        return self._parse_reverse(compressed_data)

    def _parse_via_dp(
        self,
        raw_data: bytes,
        compression_params: dict[str, int] | None = None,
    ) -> ParseResult | None:
        from gui.engine.bridge import get_core_engine

        core_engine = get_core_engine()
        if core_engine is None:
            return None

        if len(raw_data) > LZDP_DP_VIZ_MAX_SIZE:
            logger.info("[LZDPTokenParser] raw_data too large (%d) for DP viz, skipping", len(raw_data))
            return None

        try:
            from gui.engine.compressor import CompressionEngine
            from gui.ade.explorer import SilentExplorer

            eng = CompressionEngine()
            if eng.available:
                with SilentExplorer.user_compression_priority():
                    comp = eng.create_compressor_for_visualization(
                        AlgorithmType.LZDP, compression_params
                    )
                    # 0 => C++ 使用压缩器上的 dp_range_（与算法配置里「DP优化深度」一致）
                    viz = comp.get_dp_visualization(raw_data, 0)
            else:
                comp = core_engine.LZDPCompressor()
                if compression_params:
                    for key, val in compression_params.items():
                        setter = getattr(comp, f"set_{key}", None)
                        if setter:
                            setter(int(val))
                with SilentExplorer.user_compression_priority():
                    viz = comp.get_dp_visualization(raw_data, 0)

            tokens: list[Token] = []
            cursor = 0

            for triple in viz.optimal_path:
                if triple.offset == 0 and triple.length == 0:
                    tokens.append(Token(
                        type=TokenType.LITERAL,
                        original_start=cursor,
                        original_length=1,
                        compressed_size=self.SEARCH_BYTELENGTH + self.LOOKAHEAD_BYTELENGTH + 1,
                    ))
                    cursor += 1
                elif triple.offset == 0:
                    tokens.append(Token(
                        type=TokenType.LITERAL_RUN,
                        original_start=cursor,
                        original_length=triple.length,
                        compressed_size=self.SEARCH_BYTELENGTH + self.LOOKAHEAD_BYTELENGTH + triple.length,
                    ))
                    cursor += triple.length
                else:
                    tokens.append(Token(
                        type=TokenType.MATCH,
                        original_start=cursor,
                        original_length=triple.length + 1,
                        compressed_size=self.SEARCH_BYTELENGTH + self.LOOKAHEAD_BYTELENGTH + 1,
                        match_offset=triple.offset,
                    ))
                    cursor += triple.length + 1

            return ParseResult(
                tokens=tokens,
                original_size=cursor,
                compressed_payload_size=len(raw_data),
                algorithm="lzdp",
                dp_steps=self._extract_dp_steps(viz),
            )
        except Exception as e:
            logger.warning("[LZDPTokenParser] DP parse failed: %s", e, exc_info=True)
            return None

    def _extract_dp_steps(self, viz) -> list[dict]:
        steps = []
        for step in viz.steps:
            candidates = []
            for c in step.candidates:
                candidates.append({
                    "offset": c.offset,
                    "length": c.length,
                    "literal": c.literal,
                    "is_chosen": c.is_chosen,
                })
            steps.append({
                "position": step.position,
                "candidates": candidates,
                "best_token_count": step.best_token_count,
            })
        return steps

    def _parse_reverse(self, compressed_data: bytes) -> ParseResult:
        tokens: list[Token] = []
        pos = 0
        cursor = 0

        while pos < len(compressed_data):
            if pos + self.SEARCH_BYTELENGTH + self.LOOKAHEAD_BYTELENGTH > len(compressed_data):
                remaining = len(compressed_data) - pos
                if remaining > 0:
                    tokens.append(Token(
                        type=TokenType.LITERAL,
                        original_start=cursor,
                        original_length=remaining,
                        compressed_size=remaining,
                    ))
                    cursor += remaining
                break

            offset = int.from_bytes(compressed_data[pos:pos + self.SEARCH_BYTELENGTH], 'little')
            pos += self.SEARCH_BYTELENGTH

            length = int.from_bytes(compressed_data[pos:pos + self.LOOKAHEAD_BYTELENGTH], 'little')
            pos += self.LOOKAHEAD_BYTELENGTH

            if offset == 0:
                if length == 0:
                    if pos < len(compressed_data):
                        next_byte = compressed_data[pos]
                        pos += 1
                        tokens.append(Token(
                            type=TokenType.LITERAL,
                            original_start=cursor,
                            original_length=1,
                            compressed_size=self.SEARCH_BYTELENGTH + self.LOOKAHEAD_BYTELENGTH + 1,
                        ))
                        cursor += 1
                    continue

                literal_data_size = min(length, len(compressed_data) - pos)
                overhead = self.SEARCH_BYTELENGTH + self.LOOKAHEAD_BYTELENGTH
                tokens.append(Token(
                    type=TokenType.LITERAL_RUN,
                    original_start=cursor,
                    original_length=literal_data_size,
                    compressed_size=overhead + literal_data_size,
                ))
                pos += literal_data_size
                cursor += literal_data_size
            else:
                if pos < len(compressed_data):
                    next_byte = compressed_data[pos]
                    pos += 1
                else:
                    next_byte = 0

                tokens.append(Token(
                    type=TokenType.MATCH,
                    original_start=cursor,
                    original_length=length + 1,
                    compressed_size=self.SEARCH_BYTELENGTH + self.LOOKAHEAD_BYTELENGTH + 1,
                    match_offset=offset,
                ))
                cursor += length + 1

        return ParseResult(
            tokens=tokens,
            original_size=cursor,
            compressed_payload_size=len(compressed_data),
            algorithm="lzdp",
        )


class _LSBBitReader:
    def __init__(self, data: bytes):
        self._data = data
        self._byte_pos = 0
        self._buffer = 0
        self._bits_in_buffer = 0

    def _fill(self) -> None:
        while self._bits_in_buffer <= 56 and self._byte_pos < len(self._data):
            self._buffer = (self._buffer << 8) | self._data[self._byte_pos]
            self._bits_in_buffer += 8
            self._byte_pos += 1

    def ensure(self, n: int) -> bool:
        if self._bits_in_buffer < n:
            self._fill()
        return self._bits_in_buffer >= n

    def read_bits(self, n: int) -> int:
        if not self.ensure(n):
            raise ValueError(f"Not enough bits: need {n}, have {self._bits_in_buffer}")
        shift = self._bits_in_buffer - n
        val = (self._buffer >> shift) & ((1 << n) - 1)
        self._bits_in_buffer -= n
        self._buffer &= (1 << self._bits_in_buffer) - 1
        return val

    def read_bit(self) -> int:
        return self.read_bits(1)

    def bit_position(self) -> int:
        return self._byte_pos * 8 - self._bits_in_buffer


class _CanonicalHuffmanTree:

    def __init__(self, data: bytes):
        self.reverse_map: dict[int, int] = {}
        self.code_lengths: dict[int, int] = {}
        self.entries: list[tuple[int, int]] = []
        if len(data) < 2:
            return
        count = int.from_bytes(data[0:2], 'big')
        pos = 2
        for _ in range(count):
            if pos + 3 > len(data):
                break
            sym = int.from_bytes(data[pos:pos + 2], 'big')
            length = data[pos + 2]
            pos += 3
            if length > 0:
                self.entries.append((sym, length))
                self.code_lengths[sym] = length
        self._build_reverse_map()

    def _build_reverse_map(self):
        self.reverse_map.clear()
        if not self.entries:
            return
        entries = sorted(self.entries, key=lambda x: (x[1], x[0]))
        max_len = max(e[1] for e in entries)
        bl_count = [0] * (max_len + 1)
        for _, l in entries:
            bl_count[l] += 1
        next_code = [0] * (max_len + 1)
        code = 0
        for i in range(1, max_len + 1):
            code = (code + bl_count[i - 1]) << 1
            next_code[i] = code
        for sym, length in entries:
            self.reverse_map[(next_code[length] << 8) | length] = sym
            next_code[length] += 1

    def decode(self, reader: _LSBBitReader) -> int | None:
        code = 0
        for length in range(1, 33):
            if not reader.ensure(1):
                return None
            code = (code << 1) | reader.read_bit()
            key = (code << 8) | length
            if key in self.reverse_map:
                return self.reverse_map[key]
        return None

    def to_tree_node(self) -> HuffmanTreeNode:
        if not self.reverse_map:
            return HuffmanTreeNode()
        entries: list[tuple[str, int, int]] = []
        for key, sym in self.reverse_map.items():
            length = key & 0xFF
            code_bits = key >> 8
            code_str = format(code_bits, f'0{length}b')
            entries.append((code_str, length, sym))
        root = HuffmanTreeNode()
        for code_str, length, sym in sorted(entries, key=lambda x: (x[1], x[0])):
            node = root
            for bit in code_str:
                if bit == '0':
                    if node.left is None:
                        node.left = HuffmanTreeNode()
                    node = node.left
                else:
                    if node.right is None:
                        node.right = HuffmanTreeNode()
                    node = node.right
            node.symbol = sym
            node.frequency = 1
        return root

    def to_code_table(self) -> list[HuffmanCodeEntry]:
        entries: list[tuple[str, int, int]] = []
        for key, sym in self.reverse_map.items():
            length = key & 0xFF
            code_bits = key >> 8
            code_str = format(code_bits, f'0{length}b')
            entries.append((code_str, length, sym))
        entries.sort(key=lambda x: (x[1], x[2]))
        return [HuffmanCodeEntry(symbol=sym, frequency=1, code=code, code_length=length)
                for code, length, sym in entries]


class DeflateTokenParser(TokenParser):

    SUPPORTED_ALGORITHMS = frozenset({AlgorithmType.DEFLATE, AlgorithmType.DPFLATE})

    def parse(
        self,
        compressed_data: bytes,
        raw_data: bytes | None = None,
        compression_params: dict[str, int] | None = None,
    ) -> ParseResult:
        from gui.engine.file_protocol import prepare_token_parse_payload

        compressed_data = prepare_token_parse_payload(
            compressed_data, AlgorithmType.DPFLATE
        )
        tokens: list[Token] = []
        cursor = 0
        huffman_trees: list[HuffmanTreeData] = []

        if len(compressed_data) < 10:
            return ParseResult(
                tokens=tokens, original_size=cursor,
                compressed_payload_size=len(compressed_data),
                algorithm="deflate/dpflate",
            )

        triple_count = int.from_bytes(compressed_data[0:4], 'little')
        lit_sz = int.from_bytes(compressed_data[4:6], 'little')
        off_sz = int.from_bytes(compressed_data[6:8], 'little')
        len_sz = int.from_bytes(compressed_data[8:10], 'little')

        header_end = 10 + lit_sz + off_sz + len_sz
        logger.info(
            "[DeflateTokenParser] triple_count=%d lit_sz=%d off_sz=%d len_sz=%d "
            "header_end=%d total_data=%d",
            triple_count, lit_sz, off_sz, len_sz, header_end, len(compressed_data),
        )
        if len(compressed_data) < header_end:
            return ParseResult(
                tokens=tokens, original_size=cursor,
                compressed_payload_size=len(compressed_data),
                algorithm="deflate/dpflate",
            )

        lit_tree = _CanonicalHuffmanTree(compressed_data[10:10 + lit_sz])
        off_tree = _CanonicalHuffmanTree(
            compressed_data[10 + lit_sz:10 + lit_sz + off_sz])
        len_tree = _CanonicalHuffmanTree(
            compressed_data[10 + lit_sz + off_sz:header_end])

        if compression_params:
            search_size = int(compression_params.get('search_size', 4096))
            lookahead = int(compression_params.get('lookahead_size', 256))
            offset_chunk = int(compression_params.get(
                'huffman_offset_chunk_bits',
                compression_params.get('huffman_offset_bitwidth', 8)))
            length_chunk = int(compression_params.get(
                'huffman_length_chunk_bits',
                compression_params.get('huffman_length_bitwidth', 8)))
        else:
            search_size = 4096
            lookahead = 256
            offset_chunk = 8
            length_chunk = 8
        offset_bits = max(1, search_size.bit_length())
        length_bits = max(1, lookahead.bit_length())

        lit_tree_node = lit_tree.to_tree_node()
        if lit_tree_node.left or lit_tree_node.right or lit_tree_node.symbol is not None:
            huffman_trees.append(HuffmanTreeData(
                tree=lit_tree_node,
                codes=lit_tree.to_code_table(),
                tree_type="literal(8bit)",
                total_bits=lit_sz * 8,
            ))
        off_tree_node = off_tree.to_tree_node()
        if off_tree_node.left or off_tree_node.right or off_tree_node.symbol is not None:
            huffman_trees.append(HuffmanTreeData(
                tree=off_tree_node,
                codes=off_tree.to_code_table(),
                tree_type="offset(%d x %d)" % (
                    math.ceil(offset_bits / offset_chunk), offset_chunk),
                total_bits=off_sz * 8,
            ))
        len_tree_node = len_tree.to_tree_node()
        if len_tree_node.left or len_tree_node.right or len_tree_node.symbol is not None:
            huffman_trees.append(HuffmanTreeData(
                tree=len_tree_node,
                codes=len_tree.to_code_table(),
                tree_type="length(%d x %d)" % (
                    math.ceil(length_bits / length_chunk), length_chunk),
                total_bits=len_sz * 8,
            ))

        token_data = compressed_data[header_end:]
        reader = _LSBBitReader(token_data)

        logger.info(
            "[DeflateTokenParser] lit_tree entries=%d off_tree entries=%d len_tree entries=%d "
            "token_data=%d bytes offset_bits=%d length_bits=%d "
            "offset_chunk=%d length_chunk=%d",
            len(lit_tree.reverse_map), len(off_tree.reverse_map),
            len(len_tree.reverse_map), len(token_data),
            offset_bits, length_bits, offset_chunk, length_chunk,
        )

        def _decode_multi_level(tree: _CanonicalHuffmanTree,
                                total_bits: int, chunk_bits: int) -> int | None:
            num_chunks = (total_bits + chunk_bits - 1) // chunk_bits
            last_chunk_bits = total_bits % chunk_bits
            if last_chunk_bits == 0:
                last_chunk_bits = chunk_bits
            value = 0
            for i in range(num_chunks):
                chunk = tree.decode(reader)
                if chunk is None:
                    return None
                if i == num_chunks - 1 and last_chunk_bits < chunk_bits:
                    chunk &= (1 << last_chunk_bits) - 1
                value |= chunk << (i * chunk_bits)
            return value

        try:
            for ti in range(triple_count):
                bit_start = reader.bit_position()
                if not reader.ensure(1):
                    logger.info(
                        "[DeflateTokenParser] break at ti=%d/%d: ensure(1) failed at bit=%d",
                        ti, triple_count, bit_start,
                    )
                    break
                is_literal = reader.read_bit()
                if is_literal:
                    lit_bit_start = reader.bit_position()
                    sym = lit_tree.decode(reader)
                    if sym is None:
                        logger.info(
                            "[DeflateTokenParser] break at ti=%d/%d: lit_tree.decode failed at bit=%d cursor=%d",
                            ti, triple_count, lit_bit_start, cursor,
                        )
                        break
                    lit_bits = reader.bit_position() - lit_bit_start
                    tokens.append(Token(
                        type=TokenType.LITERAL,
                        original_start=cursor,
                        original_length=1,
                        compressed_size=(reader.bit_position() - bit_start) / 8.0,
                        huffman_bits=float(lit_bits),
                        huffman_detail="LIT(%d)=%.1fbit" % (
                            sym, float(lit_bits)),
                        bit_offset=bit_start,
                        bit_length=reader.bit_position() - bit_start,
                    ))
                    cursor += 1
                else:
                    offset = _decode_multi_level(
                        off_tree, offset_bits, offset_chunk)
                    if offset is None:
                        logger.info(
                            "[DeflateTokenParser] break at ti=%d/%d: offset decode failed at bit=%d cursor=%d",
                            ti, triple_count, reader.bit_position(), cursor,
                        )
                        break
                    length = _decode_multi_level(
                        len_tree, length_bits, length_chunk)
                    if length is None:
                        logger.info(
                            "[DeflateTokenParser] break at ti=%d/%d: length decode failed at bit=%d cursor=%d",
                            ti, triple_count, reader.bit_position(), cursor,
                        )
                        break
                    token_bits = reader.bit_position() - bit_start
                    tokens.append(Token(
                        type=TokenType.MATCH,
                        original_start=cursor,
                        original_length=length,
                        compressed_size=token_bits / 8.0,
                        match_offset=offset,
                        huffman_bits=float(token_bits),
                        huffman_detail=(
                            "MATCH(off=%d len=%d %.1fbit)" % (
                                offset, length, float(token_bits))),
                        bit_offset=bit_start,
                        bit_length=token_bits,
                    ))
                    cursor += length
        except Exception as e:
            logger.warning(
                "[DeflateTokenParser] parse error: %s", e, exc_info=True)

        logger.info(
            "[DeflateTokenParser] loop finished: decoded %d tokens, cursor=%d, "
            "total_bits_read=%d",
            len(tokens), cursor, reader.bit_position(),
        )
        return ParseResult(
            tokens=tokens,
            original_size=cursor,
            compressed_payload_size=len(compressed_data),
            algorithm="deflate/dpflate",
            huffman_trees=huffman_trees if huffman_trees else None,
        )


_PARSER_MAP: dict[AlgorithmType, type[TokenParser]] = {
    AlgorithmType.LZSS: LZSSTokenParser,
    AlgorithmType.LZDP: LZDPTokenParser,
    AlgorithmType.DEFLATE: DeflateTokenParser,
    AlgorithmType.DPFLATE: DeflateTokenParser,
}


def get_parser(algorithm: AlgorithmType) -> TokenParser | None:
    cls = _PARSER_MAP.get(algorithm)
    if cls is None:
        return None
    return cls()


def can_parse(algorithm: AlgorithmType) -> bool:
    return algorithm in _PARSER_MAP


def _memory_compress_bytes_for_demo(
    algorithm: AlgorithmType,
    raw_data: bytes,
    compression_params: dict[str, int] | None,
) -> bytes:
    """Memory one-shot compress for demo/heatmap (same params as ``compression_config_snapshot``)."""
    from gui.engine.compressor import CompressionEngine
    from gui.ade.explorer import SilentExplorer

    eng = CompressionEngine()
    if not eng.available:
        raise RuntimeError("C++ core_engine 不可用，无法运行压缩演示")
    with SilentExplorer.user_compression_priority():
        comp = eng.create_compressor_for_visualization(algorithm, compression_params)
        if algorithm in (AlgorithmType.DPFLATE, AlgorithmType.DEFLATE):
            cr = comp.compress_for_demo(raw_data)
        else:
            cr = comp.compress(raw_data)
    if getattr(cr, "success", True) is False:
        em = (getattr(cr, "error_message", None) or "").strip() or "内存压缩失败"
        raise RuntimeError(em)
    data = cr.data
    if isinstance(data, list):
        return bytes(data)
    if isinstance(data, (bytes, bytearray)):
        return bytes(data)
    return bytes(data)


def parse_for_demo(
    algorithm: AlgorithmType,
    raw_data: bytes,
    compression_params: dict[str, int] | None = None,
) -> ParseResult:
    """演示专用：始终在原始文件上重跑内存 DP / 压缩，不解析流式 WCX 产物。"""
    parser = get_parser(algorithm)
    if parser is None:
        return ParseResult(algorithm=algorithm.value if algorithm else "")

    if algorithm == AlgorithmType.LZDP:
        dp_result = LZDPTokenParser()._parse_via_dp(raw_data, compression_params)
        return dp_result if dp_result is not None else ParseResult(algorithm="lzdp")

    if compression_params is None:
        from gui.engine.compressor import CompressionEngine

        compression_params = CompressionEngine.snapshot_for_algorithm(algorithm)

    if algorithm in (AlgorithmType.DPFLATE, AlgorithmType.DEFLATE):
        compression_params = dict(compression_params)
        compression_params['use_3hfmtree'] = 0

    compressed = _memory_compress_bytes_for_demo(algorithm, raw_data, compression_params)

    if algorithm in (AlgorithmType.DPFLATE, AlgorithmType.DEFLATE):
        payload = compressed
    else:
        from gui.engine.file_protocol import prepare_token_parse_payload
        payload = prepare_token_parse_payload(compressed, algorithm)
    return parser.parse(payload, raw_data, compression_params=compression_params)


def fetch_demo_dp_visualization(
    algorithm: AlgorithmType,
    raw_data: bytes,
    compression_params: dict[str, int] | None = None,
):
    """``get_dp_visualization`` for LZDP / DPFlate LZ layer (memory path only)."""
    if algorithm not in (AlgorithmType.LZDP, AlgorithmType.DPFLATE):
        return None
    if len(raw_data) > LZDP_DP_VIZ_MAX_SIZE:
        logger.info(
            "[demo] raw_data too large (%d) for dp_viz, max=%d",
            len(raw_data),
            LZDP_DP_VIZ_MAX_SIZE,
        )
        return None
    try:
        from gui.engine.compressor import CompressionEngine
        from gui.ade.explorer import SilentExplorer

        with SilentExplorer.user_compression_priority():
            eng = CompressionEngine()
            if not eng.available:
                return None
            if algorithm == AlgorithmType.LZDP:
                comp = eng.create_compressor_for_visualization(
                    AlgorithmType.LZDP, compression_params
                )
                return comp.get_dp_visualization(raw_data, 0)
            comp = eng.create_compressor_for_visualization(
                AlgorithmType.DPFLATE, compression_params
            )
            lzdp_comp = eng.create_compressor_for_visualization(
                AlgorithmType.LZDP, compression_params
            )
            lzdp_comp.set_min_match(comp.get_min_match())
            lzdp_comp.set_match_engine(comp.get_match_engine())
            return lzdp_comp.get_dp_visualization(raw_data, 0)
    except Exception as e:
        logger.warning("[fetch_demo_dp_visualization] failed: %s", e, exc_info=True)
        return None
