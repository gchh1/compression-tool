import sys
sys.path.insert(0, 'src')
import core_engine
import time

print("=" * 70)
print("LZMine 性能回归测试")
print("=" * 70)

with open('tests/data/cmu445.html', 'rb') as f:
    data = f.read()
print(f"\n--- cmu445.html ({len(data)} bytes) ---")

for name, range_val in [("Greedy(range=1)", 1), ("DP(range=3)", 3)]:
    lz = core_engine.LZMineCompressor()
    lz.set_search_size(4096)
    lz.set_lookahead_size(256)
    lz.set_dp_range(range_val)
    
    t0 = time.perf_counter()
    r = lz.compress(list(data))
    t1 = time.perf_counter()
    
    d = lz.decompress(list(r.data))
    ok = bytes(d.data) == data
    
    print(f"  {name:15s}: {r.compressed_size:6d}B  ratio={r.compression_ratio:.4f}  "
          f"time={(t1-t0)*1000:.1f}ms  verify={ok}")

lzss = core_engine.LZSSCompressor()
t0 = time.perf_counter()
r_lzss = lzss.compress(list(data))
t1 = time.perf_counter()
print(f"  {'LZSS':15s}: {r_lzss.compressed_size:6d}B  ratio={r_lzss.compression_ratio:.4f}  "
      f"time={(t1-t0)*1000:.1f}ms")

deflate = core_engine.DeflateCompressor()
t0 = time.perf_counter()
r_def = deflate.compress(list(data))
t1 = time.perf_counter()
print(f"  {'Deflate':15s}: {r_def.compressed_size:6d}B  ratio={r_def.compression_ratio:.4f}  "
      f"time={(t1-t0)*1000:.1f}ms")
