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

print('=== off_tree code_length distribution ===')
clen_counts = {}
for sym, clen in sorted(off_tree.entries, key=lambda x: (x[1], x[0])):
    clen_counts[clen] = clen_counts.get(clen, 0) + 1
for clen in sorted(clen_counts):
    print(f'  clen={clen}: {clen_counts[clen]} symbols')
print(f'  total entries: {len(off_tree.entries)}\n')

print('=== off_tree canonical codes (first 10 sorted by clen,sym) ===')
entries_sorted = sorted(off_tree.entries, key=lambda x: (x[1], x[0]))
bl_count = {}
for _, clen in entries_sorted:
    bl_count[clen] = bl_count.get(clen, 0) + 1
code = 0
next_code = {}
for bits in range(1, max(bl_count.keys())+1):
    code = (code + bl_count.get(bits-1, 0)) << 1
    next_code[bits] = code
for sym, clen in entries_sorted[:10]:
    c = next_code[clen]
    next_code[clen] += 1
    print(f'  sym={sym:3d} clen={clen} code={c:0{clen}b}')

print('\n=== len_tree entries for syms 0,1,6,9 ===')
for sym, clen in len_tree.entries:
    if sym in (0, 1, 6, 9):
        print(f'  sym={sym} clen={clen}')

print('\n=== len_tree canonical codes for syms 0,1,6,9 ===')
entries_sorted = sorted(len_tree.entries, key=lambda x: (x[1], x[0]))
bl_count = {}
for _, clen in entries_sorted:
    bl_count[clen] = bl_count.get(clen, 0) + 1
code_val = 0
next_codes = {}
for bits in range(1, max(bl_count.keys())+1):
    code_val = (code_val + bl_count.get(bits-1, 0)) << 1
    next_codes[bits] = code_val
assigned = {}
for sym, clen in entries_sorted:
    c = next_codes[clen]
    next_codes[clen] += 1
    assigned[sym] = (c, clen)
for sym in (0, 1, 6, 9):
    if sym in assigned:
        c, clen = assigned[sym]
        print(f'  sym={sym:3d} clen={clen} code={c:0{clen}b}')
    else:
        print(f'  sym={sym:3d} NOT IN TREE')

# Now let's decode the first match manually
reader = _LSBBitReader(payload[header_end:])
cursor = 0
for ti in range(40):
    bit_start = reader.bit_position()
    flag = reader.read_bit()
    if flag == 1:
        sym = lit_tree.decode(reader)
        cursor += 1
        continue
    
    print(f'\n=== MATCH at ti={ti}, bit_start={bit_start} ===')
    print(f'Flag: {flag} (0=MATCH)')
    
    # Print actual bits for offset chunk 0
    byte_off = reader.bit_position() // 8
    bit_off = reader.bit_position() % 8
    ctx = payload[header_end + byte_off:header_end + byte_off + 3]
    ctx_bits = ''.join(format(b, '08b') for b in ctx)
    print(f'Before off_chunk_0: bit_pos={reader.bit_position()}, bytes={ctx.hex(" ")}')
    print(f'Bits remaining in byte: {ctx_bits[bit_off:]}')
    
    # Decode offset chunks
    for ci in range(2):
        before = reader.bit_position()
        chunk = off_tree.decode(reader)
        after = reader.bit_position()
        print(f'  off_chunk_{ci}: before={before} after={after} bits={after-before} value={chunk}')
    
    # Print actual bits for length chunk 0
    byte_off = reader.bit_position() // 8
    bit_off = reader.bit_position() % 8
    ctx = payload[header_end + byte_off:header_end + byte_off + 3]
    ctx_bits = ''.join(format(b, '08b') for b in ctx)
    print(f'Before len_chunk_0: bit_pos={reader.bit_position()}, bytes={ctx.hex(" ")}')
    print(f'Bits remaining: {ctx_bits[bit_off:]}')
    
    # Decode length chunks
    for ci in range(2):
        before = reader.bit_position()
        chunk = len_tree.decode(reader)
        after = reader.bit_position()
        mask_info = ''
        if ci == 1:
            last_chunk_bits = 9 % 8  # = 1
            raw = chunk
            chunk = chunk & 0x01 if chunk else 0
            mask_info = f' (raw={raw}, masked to {chunk})'
        print(f'  len_chunk_{ci}: before={before} after={after} bits={after-before} value={chunk}{mask_info}')
    
    break