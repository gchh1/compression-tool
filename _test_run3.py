import numpy as np, struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'build/src/bindings/pybind_new'))

lines = []
try:
    import core_engine_new as ce
    lines.append('import OK')

    w = h = 64
    rgb = np.full((h, w, 3), [128, 64, 192], dtype=np.uint8)
    hdr = struct.pack('<IIII', w, h, 1, 1)
    ind = hdr + rgb.tobytes()
    lines.append(f'input size: {len(ind)}')

    c = ce.VideoH264Compressor()
    lines.append('compressor created')

    comp_result = c.compress(ind)
    lines.append(f'compress returned type: {type(comp_result)}')

    comp = bytes(comp_result.data)
    lines.append(f'compressed size: {len(comp)}')

    decomp_result = c.decompress(comp)
    lines.append(f'decompress returned type: {type(decomp_result)}')

    decomp = bytes(decomp_result.data)
    lines.append(f'decompressed size: {len(decomp)}')

    out = np.frombuffer(decomp[16:], dtype=np.uint8).reshape((h, w, 3))
    mse = np.mean((rgb.astype(np.float64) - out.astype(np.float64)) ** 2)
    psnr = 10 * np.log10(255 ** 2 / mse) if mse > 0 else 99
    lines.append(f'PSNR={psnr:.2f} dB')

    max_diff_r = int(np.max(np.abs(rgb[:,:,0].astype(int)-out[:,:,0].astype(int))))
    max_diff_g = int(np.max(np.abs(rgb[:,:,1].astype(int)-out[:,:,1].astype(int))))
    max_diff_b = int(np.max(np.abs(rgb[:,:,2].astype(int)-out[:,:,2].astype(int))))
    lines.append(f'max_diff: R={max_diff_r} G={max_diff_g} B={max_diff_b}')

except Exception as e:
    lines.append(f'ERROR: {e}')
    import traceback
    lines.append(traceback.format_exc())

result = '\n'.join(lines)
outpath = os.path.join(os.path.dirname(__file__), '_test_result.txt')
with open(outpath, 'w', encoding='utf-8') as f:
    f.write(result)