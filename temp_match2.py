import sys
sys.path.insert(0, 'src')
from gui.engine.token_parser import (
    _memory_compress_bytes_for_demo, _CanonicalHuffmanTree, _LSBBitReader
)
from gui.engine.compressor import AlgorithmType
from gui.engine.file_protocol import prepare_token_parse_payload

with open('resources/wow.html', 'rb') as f:
    raw_data = f.read()

print(f'File starts: {raw_data[:80]}')
print(f'Pos 17 (s=0x{raw_data[17]:02x}="{chr(raw_data[17]) if 32<=raw_data[17]<127 else "?"}"): ...{raw_data[17:60]}')
print(f'Pos 39 (s=0x{raw_data[39]:02x}="{chr(raw_data[39]) if 32<=raw_data[39]<127 else "?"}"):')
print(f'  bytes [39:50]: {raw_data[39:50].hex(" ")}')
print(f'  text [39:50]: {raw_data[39:50]}')
print()

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

print(f'search_size=4096, offset_bits=13, lookahead=256, length_bits=9')
print(f'offset_chunk=8, length_chunk=8')

token_data = payload[header_end:]
reader = _LSBBitReader(token_data)

offset_chunk = 8
length_chunk = 8

cursor = 0
for ti in range(40):
    bit_start = reader.bit_position()
    if not reader.ensure(1):
        print(f'  ti={ti}: ensure(1) failed')
        break
    is_literal = reader.read_bit()
    if is_literal:
        sym = lit_tree.decode(reader)
        if sym is None:
            print(f'  ti={ti} cursor={cursor}: lit decode FAILED at bit_pos={bit_start}')
            break
        expected = raw_data[cursor] if cursor < len(raw_data) else -1
        ok = 'OK' if sym == expected else f'MISMATCH(expected={expected})'
        cursor += 1
        if ti < 39:
            continue
        print(f'  ti={ti} cursor={cursor}: LIT sym={sym}({chr(sym) if 32<=sym<127 else "?"}) {ok}')
    else:
        offset = 0
        num_chunks = (13 + 8 - 1) // 8
        last_chunk_bits = 13 % 8 or 8
        for ci in range(num_chunks):
            chunk = off_tree.decode(reader)
            if chunk is None:
                print(f'  ti={ti}: OFF CHUNK {ci} FAILED at bit_pos={reader.bit_position()}')
                break
            if ci == num_chunks - 1 and last_chunk_bits < 8:
                chunk &= (1 << last_chunk_bits) - 1
            offset |= chunk << (ci * 8)
            print(f'    offset chunk {ci}: raw={off_tree.decode(reader) if False else chunk} masked_chunk={chunk}, running_offset={offset}')

        length = 0
        num_chunks = (9 + 8 - 1) // 8
        last_chunk_bits = 9 % 8 or 8
        for ci in range(num_chunks):
            chunk = len_tree.decode(reader)
            if chunk is None:
                print(f'  ti={ti}: LEN CHUNK {ci} FAILED at bit_pos={reader.bit_position()}')
                break
            if ci == num_chunks - 1 and last_chunk_bits < 8:
                chunk &= (1 << last_chunk_bits) - 1
            length |= chunk << (ci * 8)
            print(f'    length chunk {ci}: chunk={chunk}, running_length={length}')

        ref_start = cursor - offset
        print(f'  ti={ti} cursor={cursor}: MATCH offset={offset} length={length}')
        print(f'    ref_start={ref_start} (cursor-offset)')
        
        match_bytes = raw_data[cursor:cursor+min(length, 20)] if cursor < len(raw_data) else b'?'
        ref_bytes = raw_data[ref_start:ref_start+min(length, 20)] if 0 <= ref_start else b'?'
        
        print(f'    cursor area: {raw_data[cursor-2:cursor+30].hex(" ")}')
        print(f'    cursor text: {raw_data[cursor-2:cursor+30]}')
        print(f'    ref area:    {raw_data[ref_start-2:ref_start+30].hex(" ")}')
        print(f'    ref text:    {raw_data[ref_start-2:ref_start+30]}')
        cursor += length
        break

print(f'\ncursor={cursor} raw_size={len(raw_data)}')