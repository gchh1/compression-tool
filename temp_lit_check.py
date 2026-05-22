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

lit_sz = int.from_bytes(payload[4:6], 'little')
lit_tree = _CanonicalHuffmanTree(payload[10:10 + lit_sz])

print('lit_tree entries with sym=60, 104, 116, 109, 108:')
for sym, clen in lit_tree.entries:
    if sym in (60, 104, 108, 109, 116):
        print(f'  sym={sym} clen={clen}')

print('\nCode length distribution:')
clen_counts = {}
for sym, clen in lit_tree.entries:
    clen_counts[clen] = clen_counts.get(clen, 0) + 1
for clen in sorted(clen_counts):
    print(f'  clen={clen}: {clen_counts[clen]} symbols')

print(f'\nTotal entries: {len(lit_tree.entries)}')
print(f'Reverse map size: {len(lit_tree.reverse_map)}')

print('\nFirst 10 entries sorted by (clen, sym):')
entries = sorted(lit_tree.entries, key=lambda x: (x[1], x[0]))
for sym, clen in entries[:10]:
    print(f'  sym={sym} clen={clen}')