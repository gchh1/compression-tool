import sys
sys.path.insert(0, 'src')
from gui.engine.token_parser import (
    _memory_compress_bytes_for_demo, _CanonicalHuffmanTree, _LSBBitReader
)
from gui.engine.compressor import AlgorithmType
from gui.engine.file_protocol import prepare_token_parse_payload

with open('resources/wow.html', 'rb') as f:
    raw_data = f.read()

compressed = _memory_compress_bytes_for_demo(AlgorithmType.DEFLATE, raw_data, None)
payload = prepare_token_parse_payload(compressed, AlgorithmType.DEFLATE)

triple_count = int.from_bytes(payload[0:4], 'little')
lit_sz = int.from_bytes(payload[4:6], 'little')
off_sz = int.from_bytes(payload[6:8], 'little')
len_sz = int.from_bytes(payload[8:10], 'little')
header_end = 10 + lit_sz + off_sz + len_sz

lit_tree = _CanonicalHuffmanTree(payload[10:10 + lit_sz])
off_tree = _CanonicalHuffmanTree(payload[10 + lit_sz:10 + lit_sz + off_sz])
len_tree = _CanonicalHuffmanTree(payload[10 + lit_sz + off_sz:header_end])

token_data = payload[header_end:]
reader = _LSBBitReader(token_data)

search_size = 4096
lookahead = 256
offset_bits = max(1, search_size.bit_length())
length_bits = max(1, lookahead.bit_length())
offset_chunk = 8
length_chunk = 8

cursor = 0
found_match = False
for ti in range(min(50, triple_count)):
    bit_start = reader.bit_position()
    if not reader.ensure(1):
        print(f'  ti={ti}: ensure(1) failed')
        break
    is_literal = reader.read_bit()
    if is_literal:
        sym = lit_tree.decode(reader)
        if sym is None:
            print(f'  ti={ti} cursor={cursor}: lit decode FAILED')
            break
        expected = raw_data[cursor] if cursor < len(raw_data) else -1
        match_str = 'OK' if sym == expected else f'MISMATCH(expected={expected})'
        cursor += 1
        if ti < 15 or not found_match:
            print(f'  ti={ti} cursor={cursor}: LIT sym={sym}(0x{sym:02x}="{chr(sym) if 32<=sym<127 else "?"}") {match_str}')
    else:
        found_match = True
        try:
            num_chunks = (offset_bits + offset_chunk - 1) // offset_chunk
            last_chunk_bits = offset_bits % offset_chunk or offset_chunk
            offset = 0
            for ci in range(num_chunks):
                chunk = off_tree.decode(reader)
                if chunk is None:
                    print(f'  ti={ti} cursor={cursor}: off chunk {ci} decode FAILED')
                    break
                if ci == num_chunks - 1 and last_chunk_bits < offset_chunk:
                    chunk &= (1 << last_chunk_bits) - 1
                offset |= chunk << (ci * offset_chunk)
            
            num_chunks = (length_bits + length_chunk - 1) // length_chunk
            last_chunk_bits = length_bits % length_chunk or length_chunk
            length = 0
            for ci in range(num_chunks):
                chunk = len_tree.decode(reader)
                if chunk is None:
                    print(f'  ti={ti} cursor={cursor}: len chunk {ci} decode FAILED')
                    break
                if ci == num_chunks - 1 and last_chunk_bits < length_chunk:
                    chunk &= (1 << last_chunk_bits) - 1
                length |= chunk << (ci * length_chunk)
            
            match_data = raw_data[cursor:cursor+min(length,20)] if cursor < len(raw_data) else b'???'
            ref_data = raw_data[cursor-offset:cursor-offset+min(length,20)] if 0 <= cursor-offset < len(raw_data) else b'???'
            print(f'  ti={ti} cursor={cursor}: MATCH offset={offset} length={length}')
            print(f'    match_data={match_data.hex(" ")}')
            print(f'    ref_data={ref_data.hex(" ")}')
            print(f'    match_str={match_data}')
            print(f'    ref_str={ref_data}')
            print(f'    match_ok={match_data[:min(length, len(ref_data))] == ref_data[:min(length, len(match_data))]}')
            cursor += length
        except Exception as e:
            print(f'  ti={ti} cursor={cursor}: MATCH decode ERROR: {e}')
            break

    if ti >= 20 and found_match:
        break

print(f'\nFinal cursor={cursor} raw_size={len(raw_data)}')