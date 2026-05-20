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
            self._buffer |= self._data[self._byte_pos] << self._bits_in_buffer
            self._bits_in_buffer += 8
            self._byte_pos += 1

    def ensure(self, n: int) -> bool:
        if self._bits_in_buffer < n:
            self._fill()
        return self._bits_in_buffer >= n

    def read_bits(self, n: int) -> int:
        if not self.ensure(n):
            raise ValueError(f"Not enough bits: need {n}, have {self._bits_in_buffer}")
        val = self._buffer & ((1 << n) - 1)
        self._buffer >>= n
        self._bits_in_buffer -= n
        return val

    def read_bit(self) -> int:
        return self.read_bits(1)

    def bit_position(self) -> int:
        return self._byte_pos * 8 - self._bits_in_buffer


class _HuffmanNode:
    __slots__ = ('symbol', 'left', 'right')

    def __init__(self, symbol: int | None = None):
        self.symbol = symbol
        self.left: _HuffmanNode | None = None
        self.right: _HuffmanNode | None = None

    def is_leaf(self) -> bool:
        return self.left is None and self.right is None


class DeflateTokenParser(TokenParser):

    DEFLATE_SYMBOL_BITS = 9
    DISTANCE_SYMBOL_BITS = 5

    LENGTH_BASES = [
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
    ]
    LENGTH_EXTRA = [
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
    ]
    DIST_BASES = [
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
    ]
    DIST_EXTRA = [
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
    ]

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
        reader = _LSBBitReader(compressed_data)
        tokens: list[Token] = []
        cursor = 0
        huffman_trees: list[HuffmanTreeData] = []

        try:
            while True:
                tree_start = reader.bit_position()

                if not reader.ensure(1):
                    break
                lit_root = self._read_tree(reader, self.DEFLATE_SYMBOL_BITS)
                if lit_root is None:
                    break

                if not reader.ensure(1):
                    break
                dist_root = self._read_tree(reader, self.DISTANCE_SYMBOL_BITS)
                if dist_root is None:
                    break

                tree_bits = reader.bit_position() - tree_start

                lit_freq = self._collect_frequencies(lit_root)
                dist_freq = self._collect_frequencies(dist_root)
                lit_codes = self._build_code_table(lit_root, lit_freq)
                dist_codes = self._build_code_table(dist_root, dist_freq)

                lit_tree_node = self._convert_tree(lit_root)
                dist_tree_node = self._convert_tree(dist_root)

                huffman_trees.append(HuffmanTreeData(
                    tree=lit_tree_node,
                    codes=lit_codes,
                    tree_type="literal/length",
                    total_bits=tree_bits // 2,
                ))
                huffman_trees.append(HuffmanTreeData(
                    tree=dist_tree_node,
                    codes=dist_codes,
                    tree_type="distance",
                    total_bits=tree_bits - tree_bits // 2,
                ))

                while True:
                    bit_start = reader.bit_position()
                    symbol = self._decode_symbol(reader, lit_root)
                    if symbol is None:
                        break

                    if symbol == 256:
                        break

                    if symbol < 256:
                        lit_bits = reader.bit_position() - bit_start
                        tokens.append(Token(
                            type=TokenType.LITERAL,
                            original_start=cursor,
                            original_length=1,
                            compressed_size=lit_bits / 8.0,
                            huffman_bits=float(lit_bits),
                            huffman_detail="LIT(%d)=%.1fbit" % (symbol, float(lit_bits)),
                        ))
                        cursor += 1
                        continue

                    idx = symbol - 257
                    if idx >= len(self.LENGTH_BASES):
                        break
                    base_len = self.LENGTH_BASES[idx]
                    extra_bits_count = self.LENGTH_EXTRA[idx]

                    len_symbol_end = reader.bit_position()
                    length = base_len
                    if extra_bits_count > 0:
                        if not reader.ensure(extra_bits_count):
                            break
                        length += reader.read_bits(extra_bits_count)

                    dist_bit_start = reader.bit_position()
                    dist_symbol = self._decode_symbol(reader, dist_root)
                    if dist_symbol is None or dist_symbol >= len(self.DIST_BASES):
                        break
                    dist_symbol_end = reader.bit_position()

                    base_dist = self.DIST_BASES[dist_symbol]
                    dist_extra_count = self.DIST_EXTRA[dist_symbol]
                    distance = base_dist
                    if dist_extra_count > 0:
                        if not reader.ensure(dist_extra_count):
                            break
                        distance += reader.read_bits(dist_extra_count)

                    token_bits = reader.bit_position() - bit_start
                    len_code_bits = len_symbol_end - bit_start
                    dist_code_bits = dist_symbol_end - dist_bit_start
                    detail = "LEN(sym%d+%db)+DIST(sym%d+%db)=%.1fbit" % (
                        idx, extra_bits_count, dist_symbol, dist_extra_count, float(token_bits))
                    tokens.append(Token(
                        type=TokenType.MATCH,
                        original_start=cursor,
                        original_length=length,
                        compressed_size=token_bits / 8.0,
                        match_offset=distance,
                        huffman_bits=float(token_bits),
                        huffman_detail=detail,
                    ))
                    cursor += length

        except Exception as e:
            logger.warning("[DeflateTokenParser] parse error: %s", e, exc_info=True)

        algo_name = "deflate/dpflate"
        return ParseResult(
            tokens=tokens,
            original_size=cursor,
            compressed_payload_size=len(compressed_data),
            algorithm=algo_name,
            huffman_trees=huffman_trees if huffman_trees else None,
        )

    def _collect_frequencies(self, root: _HuffmanNode) -> dict[int, int]:
        freq: dict[int, int] = {}
        stack = [root]
        while stack:
            node = stack.pop()
            if node is None:
                continue
            if node.is_leaf():
                freq[node.symbol] = freq.get(node.symbol, 0) + 1
            else:
                stack.append(node.right)
                stack.append(node.left)
        return freq

    def _build_code_table(self, root: _HuffmanNode, freq: dict[int, int]) -> list[HuffmanCodeEntry]:
        codes: list[HuffmanCodeEntry] = []
        self._traverse_for_codes(root, "", codes, freq)
        codes.sort(key=lambda e: (e.code_length, e.symbol))
        return codes

    def _traverse_for_codes(self, node: _HuffmanNode, prefix: str,
                            codes: list[HuffmanCodeEntry], freq: dict[int, int]) -> None:
        if node is None:
            return
        if node.is_leaf():
            code = prefix if prefix else "0"
            codes.append(HuffmanCodeEntry(
                symbol=node.symbol,
                frequency=freq.get(node.symbol, 0),
                code=code,
                code_length=len(code),
            ))
            return
        self._traverse_for_codes(node.left, prefix + "0", codes, freq)
        self._traverse_for_codes(node.right, prefix + "1", codes, freq)

    def _convert_tree(self, node: _HuffmanNode) -> HuffmanTreeNode:
        if node is None:
            return HuffmanTreeNode()
        if node.is_leaf():
            return HuffmanTreeNode(symbol=node.symbol, frequency=1)
        left = self._convert_tree(node.left)
        right = self._convert_tree(node.right)
        return HuffmanTreeNode(
            frequency=left.frequency + right.frequency,
            left=left,
            right=right,
        )

    def _read_tree(self, reader: _LSBBitReader, symbol_bits: int) -> _HuffmanNode | None:
        if not reader.ensure(1):
            return None
        is_leaf = reader.read_bit()
        if is_leaf:
            if not reader.ensure(symbol_bits):
                return None
            sym = reader.read_bits(symbol_bits)
            return _HuffmanNode(symbol=sym)
        node = _HuffmanNode()
        node.left = self._read_tree(reader, symbol_bits)
        node.right = self._read_tree(reader, symbol_bits)
        if node.left is None or node.right is None:
            return None
        return node

    def _decode_symbol(self, reader: _LSBBitReader, root: _HuffmanNode) -> int | None:
        node = root
        while not node.is_leaf():
            if not reader.ensure(1):
                return None
            bit = reader.read_bit()
            node = node.right if bit else node.left
            if node is None:
                return None
        return node.symbol


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

    compressed = _memory_compress_bytes_for_demo(algorithm, raw_data, compression_params)
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
