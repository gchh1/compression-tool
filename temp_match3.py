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

cursor = 0
for ti in range(40):
    bit_start = reader.bit_position()
    is_literal = reader.read_bit()
    if is_literal:
        sym = lit_tree.decode(reader)
        cursor += 1
        if ti == 38:
            print(f'  ti={ti}: LIT sym={sym} bit_pos after={reader.bit_position()}')
        continue
    
    print(f'\n=== MATCH at ti={ti} cursor={cursor} ===')
    print(f'  bit_position before flag={bit_start}')
    print(f'  bit_context (bits starting from {bit_start}):')
    
    byte_start = bit_start // 8
    bit_offset = bit_start % 8
    ctx_bytes = token_data[byte_start:byte_start+10]
    ctx_bits = ''.join(format(b, '08b') for b in ctx_bytes)
    print(f'  bytes[{byte_start}..{byte_start+9}]: {ctx_bytes.hex(" ")}')
    for j in range(len(ctx_bits)):
        if j % 8 == 0 and j > 0:
            print(' ', end='')
        if j < bit_offset:
            print('x', end='')
        else:
            print(ctx_bits[j], end='')
    print()
    print(f'  marked above: x=already consumed')
    
    print(f'\n  --- offset chunks ---')
    offset = 0
    for ci in range(2):
        before = reader.bit_position()
        chunk = off_tree.decode(reader)
        after = reader.bit_position()
        chunk_raw = chunk if chunk is not None else -1
        if ci == 1 and (13 % 8 or 8) < 8:
            chunk_raw = chunk_raw & ((1 << (13 % 8)) - 1) if chunk_raw >= 0 else -1
        offset |= (chunk_raw if chunk_raw >= 0 else 0) << (ci * 8)
        bits_consumed = after - before
        ctx = token_data[before//8:before//8+3].hex(' ')
        print(f'    chunk_{ci}: raw={chunk_raw}, bits_consumed={bits_consumed}, bit_pos={before}->{after}, bytes_around={ctx}')
    
    print(f'  offset_reconstructed={offset}')
    
    print(f'\n  --- length chunks ---')
    length = 0
    for ci in range(2):
        before = reader.bit_position()
        chunk = len_tree.decode(reader)
        after = reader.bit_position()
        chunk_raw = chunk if chunk is not None else -1
        if ci == 1 and (9 % 8 or 8) < 8:
            last_mask = (1 << (9 % 8)) - 1
            chunk_raw = (chunk_raw & last_mask) if chunk_raw >= 0 else -1
            print(f'    chunk_{ci}: raw_before_mask={chunk}, last_chunk_mask={last_mask}, after_mask={chunk_raw}')
        length |= (chunk_raw if chunk_raw >= 0 else 0) << (ci * 8)
        bits_consumed = after - before
        ctx = token_data[before//8:before//8+3].hex(' ')
        print(f'    chunk_{ci}: raw={chunk_raw}, bits_consumed={bits_consumed}, bit_pos={before}->{after}, bytes_around={ctx}')
    
    print(f'  length_reconstructed={length}')
    
    print(f'\n  MATCH: offset={offset} length={length}')
    print(f'  cursor={cursor} ref_pos={cursor-offset}')
    print(f'  expected first bytes from original: ', end='')
    if cursor < len(raw_data):
        print(raw_data[cursor:cursor+min(length,12)].hex(' '), repr(raw_data[cursor:cursor+min(length,12)]))
    print(f'  reference first bytes: ', end='')
    ref_start = cursor - offset
    if 0 <= ref_start < len(raw_data):
        print(raw_data[ref_start:ref_start+min(length,12)].hex(' '), repr(raw_data[ref_start:ref_start+min(length,12)]))
    
    break

print(f'\nThe len_tree reverse_map has keys:')
print(f'  key for sym=0 clen=1: 0x{(0 << 8) | 1:04x}')
for sym, clen in len_tree.entries[:5]:
    print(f'  sym={sym} clen={clen}: key=0x{((0) << 8) | clen:04x} (code not computed here, just showing format)')