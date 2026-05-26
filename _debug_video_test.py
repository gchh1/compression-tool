import numpy as np, struct, sys
sys.path.insert(0, 'build/src/bindings/pybind_new')
import core_engine_new as ce

# Test 1: Solid color
w = h = 16
rgb = np.full((h, w, 3), [128, 64, 192], dtype=np.uint8)
hdr = struct.pack('<IIII', w, h, 1, 1)
ind = hdr + rgb.tobytes()
c = ce.VideoH264Compressor()
comp = bytes(c.compress(ind).data)
decomp = bytes(c.decompress(comp).data)
out = np.frombuffer(decomp[16:], dtype=np.uint8).reshape((h, w, 3))
mse = np.mean((rgb.astype(np.float64) - out.astype(np.float64)) ** 2)
psnr = 10 * np.log10(255 ** 2 / mse) if mse > 0 else 99
print(f'Test1 Solid:  PSNR={psnr:.2f} dB, size={len(comp)}/{len(ind)}, first pixel in={rgb[0,0]} out={out[0,0]}')

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
print(f'Test2 Grad:   PSNR={psnr2:.2f} dB, size={len(comp2)}/{len(ind2)}')

# Test 3: Solid with lower QP
c.set_quality(10)
comp3 = bytes(c.compress(ind).data)
decomp3 = bytes(c.decompress(comp3).data)
out3 = np.frombuffer(decomp3[16:], dtype=np.uint8).reshape((h, w, 3))
mse3 = np.mean((rgb.astype(np.float64) - out3.astype(np.float64)) ** 2)
psnr3 = 10 * np.log10(255 ** 2 / mse3) if mse3 > 0 else 99
print(f'Test3 QP=10:  PSNR={psnr3:.2f} dB, first pixel in={rgb[0,0]} out={out3[0,0]}')