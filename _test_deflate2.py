"""Direct deflate roundtrip test"""
import sys
sys.path.insert(0, 'src')

from gui.engine.web_dict import encode
from gui.engine.file_protocol import unpack_compressed_file

# Load the WCX payload (compressed data)
fn = 'Package/compressed/wow.html.wcx'
raw = open(fn, 'rb').read()
hdr, payload = unpack_compressed_file(raw)

# Load original file
import os
for root, dirs, files in os.walk('.'):
    for f in files:
        if f == 'wow.html' and '.wcx' not in f:
            original_path = os.path.join(root, f)
            break
    else:
        continue
    break

original_data = open(original_path, 'rb').read()
encoded = encode(original_data)

print(f'Original size: {len(original_data)}')
print(f'Encoded size: {len(encoded)}')
print(f'WCX payload size: {len(payload)}')

# Direct compress+decompress test: compress the encoded data, then decompress
from gui.engine.compressor import CompressionEngine
engine = CompressionEngine()

print(f'\n=== Test 1: compress(encoded) -> decompress(compressed) ===')
comp_result = engine.compress(encoded)
print(f'  compress: {len(encoded)} -> {comp_result.compressed_size}')
print(f'  ratio: {comp_result.compression_ratio:.4f}')

decomp_result = engine.decompress(bytes(comp_result.data))
decomp_data = bytes(decomp_result.data)
print(f'  decompress: {comp_result.compressed_size} -> {len(decomp_data)}')
print(f'  matches original: {decomp_data == encoded}')

if decomp_data != encoded:
    diffs = sum(1 for j in range(min(len(encoded), len(decomp_data))) if encoded[j] != decomp_data[j])
    print(f'  different bytes: {diffs}')
    for i in range(min(len(encoded), len(decomp_data))):
        if encoded[i] != decomp_data[i]:
            ctx_s = max(0, i-20)
            ctx_e = min(len(encoded), i+30)
            print(f'  First diff at byte {i}:')
            print(f'    expected[{i}] = 0x{encoded[i]:02X} ({chr(encoded[i]) if 32<=encoded[i]<127 else "?"})')
            print(f'    actual[{i}]   = 0x{decomp_data[i]:02X} ({chr(decomp_data[i]) if 32<=decomp_data[i]<127 else "?"})')
            print(f'    expected context: {encoded[ctx_s:ctx_e]}')
            print(f'    actual   context: {decomp_data[ctx_s:ctx_e]}')
            break

print(f'\n=== Test 2: simple ASCII data ===')
test_data = b'The quick brown fox jumps over the lazy dog. ' * 100
comp2 = engine.compress(test_data)
decomp2 = engine.decompress(bytes(comp2.data))
decomp2_data = bytes(decomp2.data)
print(f'  compress: {len(test_data)} -> {comp2.compressed_size}')
print(f'  decompress: {comp2.compressed_size} -> {len(decomp2_data)}')
print(f'  matches: {decomp2_data == test_data}')

print(f'\n=== Test 3: 4096 bytes of random-ish data ===')
import struct
test3 = bytearray(4096)
for i in range(4096):
    test3[i] = (i * 37 + 13) & 0xFF
test3 = bytes(test3)
comp3 = engine.compress(test3)
decomp3 = engine.decompress(bytes(comp3.data))
decomp3_data = bytes(decomp3.data)
print(f'  compress: {len(test3)} -> {comp3.compressed_size}')
print(f'  decompress: {comp3.compressed_size} -> {len(decomp3_data)}')
print(f'  matches: {decomp3_data == test3}')
if decomp3_data != test3:
    diffs = sum(1 for j in range(min(len(test3), len(decomp3_data))) if test3[j] != decomp3_data[j])
    print(f'  different bytes: {diffs}')