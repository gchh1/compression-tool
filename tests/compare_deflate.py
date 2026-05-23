"""
compare_deflate.py - 对比新旧版 Deflate 的分步压缩结果

用法:
  python compare_deflate.py old_output.txt new_output.txt

解析 test_deflate_step_old.exe 和 test_deflate_step_new.exe 的输出,
逐项比较:
  1) LZ 匹配阶段: triples 数量和内容
  2) Flate (Huffman) 编码阶段: 压缩字节流
"""

import sys
import os
from collections import OrderedDict


def parse_output(filepath):
    """解析测试输出为 {key: {triples, triple_count, compressed, compressed_size}} 的记录列表"""
    with open(filepath, 'r', encoding='utf-8-sig', errors='replace') as f:
        text = f.read()

    records = []
    current = None
    in_corpus = False

    for line in text.split('\n'):
        line = line.strip()
        if line == '===CORPUS_START===':
            current = {}
            in_corpus = True
            continue
        if line == '===CORPUS_END===':
            if current:
                records.append(current)
            current = None
            in_corpus = False
            continue
        if not in_corpus or current is None:
            continue
        if line.startswith('NAME '):
            current['name'] = line[5:]
        elif line.startswith('SEARCH '):
            current['search'] = int(line[7:])
        elif line.startswith('MM '):
            current['mm'] = int(line[3:])
        elif line.startswith('CHAIN '):
            current['chain'] = int(line[6:])
        elif line.startswith('LA '):
            current['la'] = int(line[3:])
        elif line.startswith('FLAG '):
            current['flag'] = int(line[5:])
        elif line.startswith('TRIPLES '):
            current['triple_count'] = int(line[8:])
        elif line.startswith('COMPRESSED '):
            current['compressed_size'] = int(line[11:])
        elif line.startswith('TRIPLES_DATA '):
            current['triples'] = line[13:]
        elif line.startswith('COMPRESSED_DATA '):
            current['compressed'] = line[16:]

    return records


def make_key(rec, source):
    return (rec['name'], rec['search'], rec['mm'], rec['chain'], rec['la'], rec['flag'], source)


def compare():
    if len(sys.argv) < 3:
        print("Usage: python compare_deflate.py <old_output.txt> <new_output.txt>")
        sys.exit(1)

    old_file = sys.argv[1]
    new_file = sys.argv[2]

    old_records = parse_output(old_file)
    new_records = parse_output(new_file)

    print(f"Loaded {len(old_records)} old records from {old_file}")
    print(f"Loaded {len(new_records)} new records from {new_file}")

    # index by corpus+params
    old_index = OrderedDict()
    for r in old_records:
        k = make_key(r, 'OLD')
        old_index[k] = r

    new_index = OrderedDict()
    for r in new_records:
        k = make_key(r, 'NEW')
        new_index[k] = r

    # join on common keys
    all_keys = set(k[:-1] for k in old_index.keys()) | set(k[:-1] for k in new_index.keys())

    print(f"\n{'='*80}")
    print(f"{'COMPARISON RESULTS':^80}")
    print(f"{'='*80}\n")

    total_cases = 0
    lz_match = 0
    lz_mismatch = 0
    flate_match = 0
    flate_mismatch = 0

    for key in sorted(all_keys):
        total_cases += 1
        old = old_index.get(tuple(key) + ('OLD',))
        new = new_index.get(tuple(key) + ('NEW',))

        name, search, mm, chain, la, flag = key
        print(f"\n--- Corpus: {name}, search={search}, mm={mm}, chain={chain}, la={la}, flag={flag} ---")

        if not old:
            print("  OLD: MISSING")
        if not new:
            print("  NEW: MISSING")
        if not old or not new:
            continue

        # 1) LZ triple comparison
        old_tc = old.get('triple_count', -1)
        new_tc = new.get('triple_count', -1)
        old_t = old.get('triples', '')
        new_t = new.get('triples', '')

        print(f"  LZ Triples: OLD={old_tc}, NEW={new_tc}")

        if old_t == new_t:
            print("  LZ Triples: IDENTICAL ✓")
            lz_match += 1
        else:
            print("  LZ Triples: DIFFERENT ✗")
            lz_mismatch += 1
            # show diff details
            old_parts = old_t.split()
            new_parts = new_t.split()
            print(f"    OLD parts: {len(old_parts)}, NEW parts: {len(new_parts)}")
            for i in range(min(len(old_parts), len(new_parts))):
                if old_parts[i] != new_parts[i]:
                    print(f"    First diff at index {i}: OLD={old_parts[i]} NEW={new_parts[i]}")
                    # show context
                    start = max(0, i - 3)
                    for j in range(start, min(i + 4, min(len(old_parts), len(new_parts)))):
                        marker = " <<<" if j == i else ""
                        old_val = old_parts[j] if j < len(old_parts) else "EOF"
                        new_val = new_parts[j] if j < len(new_parts) else "EOF"
                        print(f"      [{j}] OLD={old_val} NEW={new_val}{marker}")
                    break
            if len(old_parts) != len(new_parts):
                shorter = min(len(old_parts), len(new_parts))
                if shorter < len(old_parts):
                    print(f"    OLD has {len(old_parts) - shorter} extra entries: {' '.join(old_parts[shorter:shorter+6])}...")
                if shorter < len(new_parts):
                    print(f"    NEW has {len(new_parts) - shorter} extra entries: {' '.join(new_parts[shorter:shorter+6])}...")

        # 2) Flate (compressed bytes) comparison
        old_cs = old.get('compressed_size', -1)
        new_cs = new.get('compressed_size', -1)
        old_c = old.get('compressed', '')
        new_c = new.get('compressed', '')

        ratio = 0
        if old_cs > 0 and new_cs > 0:
            ratio = (new_cs - old_cs) / old_cs * 100 if old_cs > 0 else 0

        print(f"  Compressed: OLD={old_cs}B, NEW={new_cs}B (delta={new_cs - old_cs:+d}B, {ratio:+.1f}%)")

        if old_c == new_c:
            print("  Compressed bytes: IDENTICAL ✓")
            flate_match += 1
        else:
            print("  Compressed bytes: DIFFERENT ✗")
            flate_mismatch += 1
            # show first few bytes
            print(f"    OLD first 20 bytes: {old_c[:40]}")
            print(f"    NEW first 20 bytes: {new_c[:40]}")

    # Summary
    print(f"\n{'='*80}")
    print(f"{'SUMMARY':^80}")
    print(f"{'='*80}")
    print(f"Total test cases: {total_cases}")
    print(f"LZ triples match: {lz_match}/{total_cases} (mismatch: {lz_mismatch})")
    print(f"Flate bytes match: {flate_match}/{total_cases} (mismatch: {flate_mismatch})")

    if lz_mismatch == 0 and flate_mismatch > 0:
        print(f"\n→ LZSS matching is IDENTICAL, but Huffman/Flate encoding differs.")
        print(f"→ This means the compression difference is in the entropy coding, not the LZ search.")

    return 0


if __name__ == '__main__':
    sys.exit(compare())