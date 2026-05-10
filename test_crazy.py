import sys, time
sys.path.insert(0, 'src')
from importlib import import_module
core_engine = import_module('core_engine')

with open('resources/wow.html', 'rb') as f:
    data = list(f.read())

print(f"文件: wow.html ({len(data)} bytes)\n")
print(f"{'算法':25s} | {'配置':35s} | {'压缩':>8s} {'ratio':>7s} | {'verify':>6s} | {'时间':>8s}")
print("-" * 105)

def test(comp, name, config):
    r = comp.compress(data)
    d = comp.decompress(list(r.data))
    ok = bytes(d.data) == bytes(data)
    print(f"{name:25s} | {config:35s} | {r.compressed_size:>7d}B {r.compression_ratio:.4f} | {'OK' if ok else 'FAIL':>6s} | {r.time_ms:.1f}ms")
    return r, ok

# LZSS
lzss = core_engine.LZSSCompressor()
test(lzss, "LZSS", "dict=4096 maxMatch=17 minM=3")

# LZCrazy
lzc1 = core_engine.LZCrazyCompressor()
lzc1.set_search_size(4096); lzc1.set_lookahead_size(18); lzc1.set_min_match(3)
test(lzc1, "LZCrazy", "search=4096 lookahead=18 minM=3")

lzc2 = core_engine.LZCrazyCompressor()
lzc2.set_search_size(32768); lzc2.set_lookahead_size(258); lzc2.set_min_match(3)
test(lzc2, "LZCrazy", "search=32768 lookahead=258 minM=3")

# CrazyFlate
cf1 = core_engine.CrazyFlateCompressor()
cf1.set_search_size(4096); cf1.set_lookahead_size(18); cf1.set_min_match(3)
test(cf1, "CrazyFlate", "search=4096 lookahead=18 minM=3")

cf2 = core_engine.CrazyFlateCompressor()
cf2.set_search_size(32768); cf2.set_lookahead_size(258); cf2.set_min_match(3)
test(cf2, "CrazyFlate", "search=32768 lookahead=258 minM=3")

# LZMine
lz = core_engine.LZMineCompressor()
lz.set_search_size(4096); lz.set_lookahead_size(17); lz.set_dp_range(1)
test(lz, "LZMine(Greedy)", "s=4096 la=17 dp=1")

# Deflate
df = core_engine.DeflateCompressor()
test(df, "Deflate", "default")

# DPFlate
mf = core_engine.DPFlateCompressor()
test(mf, "DPFlate", "default")
