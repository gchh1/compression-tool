import sys
sys.path.insert(0, 'src')
from importlib import import_module
core_engine = import_module('core_engine')

with open('tests/data/cmu445.html', 'rb') as f:
    full_data = f.read()
data = list(full_data[:50])

lz = core_engine.LZMineCompressor()
lz.set_search_size(4096)
lz.set_lookahead_size(256)

print(f"input: {bytes(data)}")

for mode_name, rng in [("Greedy", 1), ("DP", 3)]:
    lz.set_dp_range(rng)
    r = lz.compress(data)
    d = lz.decompress(list(r.data))
    out = bytes(d.data)
    ok = out == bytes(data)
    print(f"\n{mode_name}: compressed={r.compressed_size}B decompressed={len(out)}B match={ok}")
    if not ok:
        for i in range(min(len(data), len(out))):
            if i < len(data) and i < len(out) and data[i] != out[i]:
                print(f"  diff[{i}]: expected={data[i]:#04x} got={out[i]:#04x}")
        if len(data) != len(out):
            print(f"  length: expected={len(data)} got={len(out)}")
    else:
        print(f"  OK!")
