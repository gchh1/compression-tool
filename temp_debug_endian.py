import sys
sys.path.insert(0, 'src')
from gui.engine.token_parser import (
    _memory_compress_bytes_for_demo,
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

# Literal tree data
lit_data = payload[10:10 + lit_sz]
count_le = int.from_bytes(lit_data[0:2], 'little')
count_be = int.from_bytes(lit_data[0:2], 'big')
print(f'lit_tree count: LE={count_le} BE={count_be} (expected ~110)')

# Show first few symbol bytes
print(f'lit_tree first entries raw hex: {lit_data[2:14].hex()}')
print(f'  entry0 sym LE={int.from_bytes(lit_data[2:4], "little")} BE={int.from_bytes(lit_data[2:4], "big")}')
print(f'  entry1 sym LE={int.from_bytes(lit_data[5:7], "little")} BE={int.from_bytes(lit_data[5:7], "big")}')

# Offset tree data
off_data = payload[10 + lit_sz:10 + lit_sz + off_sz]
count_off_le = int.from_bytes(off_data[0:2], 'little')
count_off_be = int.from_bytes(off_data[0:2], 'big')
print(f'\noff_tree count: LE={count_off_le} BE={count_off_be}')
print(f'off_tree first entries raw hex: {off_data[0:14].hex()}')

# Length tree data
len_data = payload[10 + lit_sz + off_sz:10 + lit_sz + off_sz + len_sz]
count_len_le = int.from_bytes(len_data[0:2], 'little')
count_len_be = int.from_bytes(len_data[0:2], 'big')
print(f'\nlen_tree count: LE={count_len_le} BE={count_len_be} (expected ~117)')

# Now show what symbols look like with LE vs BE for first few entries
for name, data, expected_count in [
    ('lit', lit_data, 110),
    ('off', off_data, None),
    ('len', len_data, 117),
]:
    cnt_le = int.from_bytes(data[0:2], 'little')
    cnt_be = int.from_bytes(data[0:2], 'big')
    print(f'\n{name}_tree:')
    print(f'  data size={len(data)} count LE={cnt_le} BE={cnt_be}')
    
    # Read with BE
    cnt = cnt_be
    pos = 2
    entries = []
    for _ in range(cnt):
        if pos + 3 > len(data):
            break
        sym = int.from_bytes(data[pos:pos + 2], 'big')
        length = data[pos + 2]
        pos += 3
        entries.append((sym, length))
    print(f'  BE read: {len(entries)} entries, first 10: {entries[:10]}')
    
    # Read with LE
    cnt = cnt_le
    pos = 2
    entries = []
    for _ in range(min(cnt, 5000)):
        if pos + 3 > len(data):
            break
        sym = int.from_bytes(data[pos:pos + 2], 'little')
        length = data[pos + 2]
        pos += 3
        entries.append((sym, length))
    print(f'  LE read: {len(entries)} entries, first 10: {entries[:10]}')