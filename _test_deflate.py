"""测试 deflate 压缩解压对称性 + web_dict 对称性"""
import sys
sys.path.insert(0, 'src')

from gui.engine.file_protocol import unpack_compressed_file
from gui.engine.web_dict import encode, decode, load_phrases, postprocess_after_codec

# ---- 0. Check phrases ----
phrases = load_phrases()
print(f'=== Phrases ===')
print(f'  count: {len(phrases)}')
print(f'  phrases: {[p.decode("utf-8", errors="replace") for p in phrases]}')

# 1. 检查 wcx header
fn = 'Package/compressed/wow.html.wcx'
raw = open(fn, 'rb').read()
hdr, payload = unpack_compressed_file(raw)
print(f'\n=== WCX Header ===')
print(f'  original_size: {hdr.original_size}')
print(f'  compressed_size: {hdr.compressed_size}')
print(f'  web_dict_preprocess: {hdr.web_dict_preprocess}')
print(f'  payload bytes: {len(payload)}')

# 2. Find original file
import os
for root, dirs, files in os.walk('.'):
    for f in files:
        if f == 'wow.html' and '.wcx' not in f:
            original_path = os.path.join(root, f)
            break
    else:
        continue
    break
print(f'\n=== Original: {original_path} ===')
original_data = open(original_path, 'rb').read()
print(f'  size: {len(original_data)}')

# 3. Web dict roundtrip
encoded = encode(original_data)
decoded = decode(encoded)
pp = postprocess_after_codec(encoded, web_dict_preprocess=True)
print(f'\n=== Web Dict Roundtrip (fallback phrases) ===')
print(f'  original -> encode: {len(original_data)} -> {len(encoded)}')
print(f'  encode -> decode: {len(encoded)} -> {len(decoded)}')
print(f'  postprocess_after_codec(encoded, True): {len(pp)}')
print(f'  decode == original: {decoded == original_data}')
print(f'  postproc == original: {pp == original_data}')

# 4. Check raw deflate output
from gui.engine.compressor import CompressionEngine
if hasattr(CompressionEngine, 'get_instance'):
    engine = CompressionEngine.get_instance()
else:
    try:
        engine = CompressionEngine()
    except:
        engine = None
        print('Cannot create engine')

if engine and hasattr(engine, 'available') and engine.available:
    decomp_result = engine.decompress(payload)
    raw_out = bytes(decomp_result.data)
    print(f'\n=== Deflate Decompress ===')
    print(f'  raw output: {len(raw_out)}')
    print(f'  matches encoded: {raw_out == encoded}')
    
    if raw_out != encoded:
        for i in range(min(len(raw_out), len(encoded))):
            if raw_out[i] != encoded[i]:
                ctx_s = max(0, i-20)
                ctx_e = min(len(raw_out), i+30)
                print(f'\n  First diff at byte {i}:')
                print(f'    encoded[{i}] = 0x{encoded[i]:02X} ({chr(encoded[i]) if 32<=encoded[i]<127 else "?"})')
                print(f'    raw_out[{i}] = 0x{raw_out[i]:02X} ({chr(raw_out[i]) if 32<=raw_out[i]<127 else "?"})')
                print(f'    encoded  context: {encoded[ctx_s:ctx_e]}')
                print(f'    raw_out  context: {raw_out[ctx_s:ctx_e]}')
                # Count total diffs
                diffs = sum(1 for j in range(len(raw_out)) if raw_out[j] != encoded[j])
                print(f'    total different bytes: {diffs}')
                break
    
    final = postprocess_after_codec(raw_out, web_dict_preprocess=True)
    print(f'\n=== Full Decompress ===')
    print(f'  deflate + web_dict: {len(raw_out)} -> {len(final)}')
    print(f'  expected: {hdr.original_size}')
    print(f'  matches: {len(final) == hdr.original_size}')
else:
    print('\nEngine not available for deflate test')