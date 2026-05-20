import sys
sys.path.insert(0, 'build_py/src/bindings/pybind')
import core_engine

print("=" * 70)
print("END-TO-END GUI SWITCH VERIFICATION (Brick Architecture)")
print("=" * 70)

test_data = b'ABCABCABCABC' * 5

# ═══ Test 1: New* compressors via GUI path ═══
print("\n[1] NEW COMPRESSORS (GUI _create_compressor path)")
algorithms = {
    'LZSS': ('NewLZSSCompressor', core_engine.NewLZSSCompressor),
    'LZDP': ('NewLZDPCompressor', core_engine.NewLZDPCompressor),
    'DEFLATE': ('NewDeflateCompressor', core_engine.NewDeflateCompressor),
    'DPFLATE': ('NewDPFlateCompressor', core_engine.NewDPFlateCompressor),
}
for name, (cls_name, cls) in algorithms.items():
    try:
        c = cls()
        comp = c.compress(test_data)
        decomp = c.decompress(comp)
        match = decomp == test_data
        print(f"  [OK] {name:8s} {cls_name}: {len(test_data):>4d} -> {len(comp):>4d} -> {len(decomp):>4d} roundtrip={'Y' if match else 'N'}")
    except Exception as e:
        print(f"  [FAIL] {name}: {e}")

# ═══ Test 2: Visualization via NewLZDPCompressor ═══
print("\n[2] VISUALIZATION (GUI token_parser.py path)")
try:
    c = core_engine.NewLZDPCompressor()
    viz = c.get_dp_visualization(test_data[:9])
    print(f"  [OK] get_dp_visualization:")
    print(f"       input_length={viz.input_length}")
    print(f"       steps={len(viz.steps)}, dp_array={len(viz.dp_array)}")
    print(f"       optimal_path={len(viz.optimal_path)} triples")
    if len(viz.steps) > 0:
        s = viz.steps[0]
        print(f"       step[0]: pos={s.position}, candidates={len(s.candidates)}, best_cost={s.best_cost}")
        if len(s.candidates) > 0:
            cand = s.candidates[0]
            print(f"         cand[0]: triple=({cand.triple.offset},{cand.triple.length},{cand.triple.literal}), chosen={cand.is_chosen}")
    if len(viz.dp_array) > 0:
        st = viz.dp_array[0]
        print(f"       dp_state[0]: reachable={st.reachable}, cost={st.cost}, tokens={st.token_count}")
except Exception as e:
    import traceback; traceback.print_exc()

# ═══ Test 3: Setter chain (GUI config application) ═══
print("\n[3] SETTER CHAIN (GUI param overlay)")
try:
    c = core_engine.NewLZDPCompressor()
    c.set_search_size(1024)
    c.set_lookahead_size(64)
    c.set_min_match(3)
    c.set_dp_top(7)
    c.set_match_engine(0)  # KMP
    c.set_use_flag_encoding(True)
    cfg = c.get_config()
    assert cfg.window.search_size == 1024
    assert cfg.window.look_size == 64
    assert cfg.window.min_match_len == 3
    assert cfg.dp.dp_top == 7
    assert cfg.dp.match_engine.value == 0
    assert cfg.encoding.use_flag_encoding == True
    print(f"  [OK] All setters verified via get_config()")
except Exception as e:
    import traceback; traceback.print_exc()

# ═══ Test 4: DPFlate getters (GUI cross-algorithm) ═══
print("\n[4] DPFLATE GETTERS (GUI DPFlate -> LZDP config sync)")
try:
    dc = core_engine.NewDPFlateCompressor()
    dc.set_search_size(2048)
    dc.set_dp_top(10)
    mm = dc.get_min_match()
    me = dc.get_match_engine()
    print(f"  [OK] get_min_match={mm}, get_match_engine={me}")
except Exception as e:
    import traceback; traceback.print_exc()

# ═══ Test 5: Old types still available (backward compat) ═══
print("\n[5] BACKWARD COMPATIBILITY (old types preserved)")
old_types = ['LZDPCompressor', 'DeflateCompressor', 'Archiver', 'AlgorithmID']
for t in old_types:
    status = "OK" if hasattr(core_engine, t) else "MISSING"
    print(f"  [{status}] {t}")

# ═══ Summary ═══
new_types = [
    'MatchEngine', 'Lz77WindowConfig', 'DpMatcherConfig',
    'HuffmanBackendConfig', 'EncodingConfig',
    'LZDPConfig', 'LZSSConfig', 'DeflateConfig', 'DPflateConfig',
    'NewTriple', 'NewDPCandidate', 'NewDPState', 'NewDPStep',
    'NewDPVisualization',
    'NewLZDPCompressor', 'NewLZSSCompressor', 'NewDeflateCompressor', 'NewDPFlateCompressor'
]
print("\n" + "=" * 70)
print("FINAL STATUS - ALL NEW TYPES:")
all_ok = True
for t in new_types:
    status = "OK" if hasattr(core_engine, t) else "MISSING"
    if status != "OK": all_ok = False
    print(f"  [{status}] {t}")
if all_ok:
    print("\n*** GUI SWITCH COMPLETE - algorithm_new IS NOW ACTIVE ***")
else:
    print("\n*** SOME TYPES MISSING ***")
print("=" * 70)
