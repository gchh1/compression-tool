import numpy as np, struct, sys
sys.path.insert(0, 'build/src/bindings/pybind_new')
import core_engine_new as ce

results = []
results.append('=== H.264 Roundtrip Test ===')

# Test 1: Solid color
w = h = 64
rgb = np.full((h, w, 3), [128, 64, 192], dtype=np.uint8)
hdr = struct.pack('<IIII', w, h, 1, 1)
ind = hdr + rgb.tobytes()
c = ce.VideoH264Compressor()
comp = bytes(c.compress(ind).data)
decomp = bytes(c.decompress(comp).data)
out = np.frombuffer(decomp[16:], dtype=np.uint8).reshape((h, w, 3))
mse = np.mean((rgb.astype(np.float64) - out.astype(np.float64)) ** 2)
psnr = 10 * np.log10(255 ** 2 / mse) if mse > 0 else 99
results.append(f'Test1 Solid 64x64:  PSNR={psnr:.2f} dB, size={len(comp)}/{len(ind)}, ratio={len(comp)/len(ind)*100:.1f}%')

# Test 2: Gradient
w = h = 64
rgb2 = np.zeros((h, w, 3), dtype=np.uint8)
for y in range(h):
    for x in range(w):
        rgb2[y, x, 0] = (x * 4) % 256
        rgb2[y, x, 1] = (y * 4) % 256
        rgb2[y, x, 2] = ((x + y) * 2) % 256
hdr2 = struct.pack('<IIII', w, h, 1, 1)
ind2 = hdr2 + rgb2.tobytes()
comp2 = bytes(c.compress(ind2).data)
decomp2 = bytes(c.decompress(comp2).data)
out2 = np.frombuffer(decomp2[16:], dtype=np.uint8).reshape((h, w, 3))
mse2 = np.mean((rgb2.astype(np.float64) - out2.astype(np.float64)) ** 2)
psnr2 = 10 * np.log10(255 ** 2 / mse2) if mse2 > 0 else 99
results.append(f'Test2 Grad 64x64:   PSNR={psnr2:.2f} dB, size={len(comp2)}/{len(ind2)}, ratio={len(comp2)/len(ind2)*100:.1f}%')

# Test 3: Solid with lower QP
c2 = ce.VideoH264Compressor()
c2.set_quality(10)
comp3 = bytes(c2.compress(ind).data)
decomp3 = bytes(c2.decompress(comp3).data)
out3 = np.frombuffer(decomp3[16:], dtype=np.uint8).reshape((h, w, 3))
mse3 = np.mean((rgb.astype(np.float64) - out3.astype(np.float64)) ** 2)
psnr3 = 10 * np.log10(255 ** 2 / mse3) if mse3 > 0 else 99
results.append(f'Test3 QP=10 64x64:  PSNR={psnr3:.2f} dB, first pixel in={rgb[0,0]} out={out3[0,0]}')

# Test 4: Solid QP=26 (default)
c3 = ce.VideoH264Compressor()
c3.set_quality(26)
comp4 = bytes(c3.compress(ind).data)
decomp4 = bytes(c3.decompress(comp4).data)
out4 = np.frombuffer(decomp4[16:], dtype=np.uint8).reshape((h, w, 3))
mse4 = np.mean((rgb.astype(np.float64) - out4.astype(np.float64)) ** 2)
psnr4 = 10 * np.log10(255 ** 2 / mse4) if mse4 > 0 else 99
results.append(f'Test4 QP=26 64x64:  PSNR={psnr4:.2f} dB')

# Test 5: Check pixel correctness (solid color should decode close to original)
results.append(f'')
results.append(f'Test1 max_diff per channel: R={np.max(np.abs(rgb[:,:,0].astype(int)-out[:,:,0].astype(int)))}, G={np.max(np.abs(rgb[:,:,1].astype(int)-out[:,:,1].astype(int)))}, B={np.max(np.abs(rgb[:,:,2].astype(int)-out[:,:,2].astype(int)))}')

result_text = '\n'.join(results)
print(result_text)

with open('_debug_video_result.txt', 'w', encoding='utf-8') as f:
    f.write(result_text)