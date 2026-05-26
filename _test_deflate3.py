"""Size threshold test for deflate roundtrip bug"""
import sys
sys.path.insert(0, 'src')

from gui.engine.web_dict import encode
from gui.engine.compressor import CompressionEngine
engine = CompressionEngine()

# Test 1: Try with the exact encoded data but different sizes
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
N = len(encoded)

# Test slices of encoded data
for size in [1000, 4096, 8192, 16384, 26540, 30000, 40000, N]:
    test = encoded[:size]
    comp = engine.compress(test)
    decomp = engine.decompress(bytes(comp.data))
    decomp_bytes = bytes(decomp.data)
    match = decomp_bytes == test
    if not match:
        diffs = sum(1 for j in range(min(len(test), len(decomp_bytes))) if test[j] != decomp_bytes[j])
        for j in range(min(len(test), len(decomp_bytes))):
            if test[j] != decomp_bytes[j]:
                ctx_s = max(0, j-10)
                ctx_e = min(len(test), j+20)
                print(f'  size={size}: FAIL - {diffs} diffs, first at byte {j}')
                print(f'    expected[{j}]=0x{test[j]:02X} actual=0x{decomp_bytes[j]:02X}')
                break
    else:
        print(f'  size={size}: PASS')

print()

# Test 2: Random data of various sizes
import struct, random
random.seed(42)
for size in [1000, 4096, 8192, 16384, 32768, 60000]:
    test = bytes([random.randint(0, 255) for _ in range(size)])
    comp = engine.compress(test)
    decomp = engine.decompress(bytes(comp.data))
    decomp_bytes = bytes(decomp.data)
    match = decomp_bytes == test
    if not match:
        diffs = sum(1 for j in range(min(len(test), len(decomp_bytes))) if test[j] != decomp_bytes[j])
        print(f'  random size={size}: FAIL - {diffs} diffs')
    else:
        print(f'  random size={size}: PASS')