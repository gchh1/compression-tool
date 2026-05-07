import sys, time
sys.path.insert(0, 'src')
from importlib import import_module
core_engine = import_module('core_engine')

with open('resources/wow.html', 'rb') as f:
    data = list(f.read())

print(f"文件: wow.html ({len(data)} bytes)\n")

# LZSS的默认参数
# DICTIONARY_BUFFER_SIZE_ = 4096
# MAX_MATCH_LENGTH_ = 17
# MIN_MATCH_LENGTH_ = 3
# position占12位(0~4095), length占4位(3~18)

print("="*85)
print(f"{'算法':20s} | {'配置':25s} | {'压缩':8s} {'ratio':7s} | verify | 时间")
print("-"*85)

# === LZSS (对照组) ===
lzss = core_engine.LZSSCompressor()
t0 = time.perf_counter()
r_lzss = lzss.compress(data)
t1 = time.perf_counter()
d_lzss = lzss.decompress(list(r_lzss.data))
print(f"{'LZSS':20s} | dict=4096 maxMatch=17 minM=3    | "
      f"{r_lzss.compressed_size:>6d}B {r_lzss.compression_ratio:>6.4f} | "
      f"{'OK' if bytes(d_lzss.data)==bytes(data) else 'FAIL':>6s} | {(t1-t0)*1000:.1f}ms")

# === LZMine: 同样参数，DP退化到Greedy(range=1) ===
for la in [17, 18, 255]:
    for ss in [4096]:
        for dp_r in [1]:
            lz = core_engine.LZMineCompressor()
            lz.set_search_size(ss)
            lz.set_lookahead_size(la)
            lz.set_dp_range(dp_r)

            t0 = time.perf_counter()
            r = lz.compress(data)
            t1 = time.perf_counter()
            d = lz.decompress(list(r.data))
            ok = bytes(d.data) == bytes(data)

            def bl(v):
                if v <= 0xFF: return 1
                if v <= 0xFFFF: return 2
                return 2
            s_bl = bl(ss)
            la_bl = bl(la)

            # 算理论开销
            viz = lz.get_dp_visualization(data, dp_r)
            lits = sum(1 for t in viz.optimal_path if t.offset == 0)
            matches = sum(1 for t in viz.optimal_path if t.offset > 0)
            match_bytes = sum(t.length for t in viz.optimal_path if t.offset > 0)

            print(f"LZMine(Greedy)       | s={ss} la={la} S_BL={s_bl} LA_BL={la_bl} dp={dp_r} | "
                  f"{r.compressed_size:>6d}B {r.compression_ratio:>6.4f} | "
                  f"{'OK' if ok else 'FAIL':>6s} | {(t1-t0)*1000:.1f}ms"
                  f"  tokens={len(viz.optimal_path)} L={lits} M={matches}")

print()

# === 详细算账：LZMine vs LZSS 同条件 ===
print("="*85)
print("=== 同条件详细对比 (search=4096, lookahead=17, Greedy) ===")
print()

lz = core_engine.LZMineCompressor()
lz.set_search_size(4096)
lz.set_lookahead_size(17)
lz.set_dp_range(1)

r = lz.compress(data)
d = lz.decompress(list(r.data))
ok = bytes(d.data) == bytes(data)
viz = lz.get_dp_visualization(data, 1)

from collections import Counter

# 分析triple结构
triples = viz.optimal_path
lit_count = sum(1 for t in triples if t.offset == 0)
match_count = sum(1 for t in triples if t.offset > 0)
total_match_len = sum(t.length for t in triples if t.offset > 0)

print(f"总tokens: {len(triples)}")
print(f"  字面量(literal): {lit_count}")
print(f"  匹配(match):     {match_count}, 总匹配长度={total_match_len}")

# literal run分析
runs = []
run_len = 0
for t in triples:
    if t.offset == 0:
        run_len += 1
    else:
        if run_len > 0: runs.append(run_len)
        run_len = 0
if run_len > 0: runs.append(run_len)

rc = Counter(runs)
print(f"\nLiteral runs: {len(runs)}个, 平均长度={sum(runs)/max(len(runs),1):.1f}")
print(f"  分布: ", end="")
for length, cnt in rc.most_common(10):
    print(f"len={length}({cnt}) ", end="")
print()

# === 计算两种编码格式的理论大小 ===
S_BL = 2
LA_BL = 1  # lookahead=17 -> calcByteLength(17)=1

print(f"\n{'='*60}")
print("=== 编码格式开销计算 ===")
print(f"S_BL={S_BL} LA_BL={LA_BL}")

# 格式A: 旧triple + literalrun
#   match: S_BL + LA_BL + 1(next_byte) = 4B
#   literal run: S_BL + LA_BL + run_data = 3 + run_len B
format_a_matches = match_count * (S_BL + LA_BL + 1)
format_a_literals = len(runs) * (S_BL + LA_BL) + lit_count  # 头+数据
format_a_total = format_a_matches + format_a_literals + 2  # +2 header

# 格式B: 修正后flag-based (1 token/match, next_byte嵌入match数据)
#   每8个token一组: 1 flag_byte + 数据
#   literal: 1B data
#   match: S_BL + LA_BL + 1(next_byte) B data
total_tokens = lit_count + match_count
num_groups = (total_tokens + 7) // 8
format_b_flags = num_groups  # flag bytes
format_b_literals = lit_count * 1  # literal data
format_b_matches = match_count * (S_BL + LA_BL + 1)  # match数据含next_byte
format_b_total = format_b_flags + format_b_literals + format_b_matches + 2  # +2 header

# 格式C: LZSS风格
#   每8个token一组: 1 flag_byte + 数据
#   literal: 1B data
#   match: 2B (12bit pos + 4bit len), 无next_byte概念
format_c_flags = num_groups
format_c_literals = lit_count * 1
format_c_matches = match_count * 2
format_c_total = format_c_flags + format_c_literals + format_c_matches + 4  # +4 size header

# 格式D: 如果LZMine也打包成2B token (去掉next_byte, 像LZSS一样)
format_d_total = format_c_flags + format_c_literals + format_c_matches + 2  # +2 header

print(f"\n{'格式':15s} | {'match开销':>10s} | {'literal开销':>12s} | {'flag/头':>8s} | {'总计':>8s} | ratio")
print("-"*75)
print(f"{'A:triple+litrun':15s} | {format_a_matches:>10d}B | {format_a_literals:>11d}B | {'6':>8s} | {format_a_total:>7d}B | {format_a_total/len(data):.4f}")
print(f"{'B:flag-fixed':15s} | {format_b_matches:>10d}B | {format_b_literals:>11d}B | {format_b_flags+2:>7d}B | {format_b_total:>7d}B | {format_b_total/len(data):.4f}")
print(f"{'C:LZSS-style':15s} | {format_c_matches:>10d}B | {format_c_literals:>11d}B | {format_c_flags+4:>7d}B | {format_c_total:>7d}B | {format_c_total/len(data):.4f}")
print(f"{'D:LZMine+pack2B':15s}| {format_c_matches:>10d}B | {format_b_literals:>11d}B | {format_b_flags+2:>7d}B | {format_d_total:>7d}B | {format_d_total/len(data):.4f}")
print(f"{'实际LZMine':15s} | {'-':>10s} | {'-':>12s} | {'-':>8s} | {r.compressed_size:>7d}B | {r.compression_ratio:.4f}")
print(f"{'实际LZSS':15s} | {'-':>10s} | {'-':>12s} | {'-':>8s} | {r_lzss.compressed_size:>7d}B | {r_lzss.compression_ratio:.4f}")

print(f"\n差距分析:")
print(f"  LZMine vs LZSS 实际差距:     {r.compressed_size - r_lzss.compressed_size}B")
print(f"  格式A(旧) vs C(LZSS) 理论:   {format_a_total - format_c_total}B")
print(f"  格式B(flag修正) vs C 理论:    {format_b_total - format_c_total}B  ← 每match多{(S_BL+LA_BL+1)-2}B(next_byte+未打包)")
print(f"  格式D(打包2B) vs C(LZSS) 理论: {format_d_total - format_c_total}B  ← 仅差header")
