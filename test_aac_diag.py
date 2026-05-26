import sys
import struct
import math
import wave
import io
import time

sys.path.insert(0, r'd:\AAA_C\compression-tool\build_py\src\bindings\pybind_new')
import core_engine_new as ce

sr = 44100
duration = 2.0
freq = 440.0
num_samples = int(sr * duration)
amplitude = 20000

samples_int16 = [int(amplitude * math.sin(2 * math.pi * freq * i / sr))
                 for i in range(num_samples)]

raw_pcm = struct.pack('<' + 'h' * num_samples, *samples_int16)
buf = io.BytesIO()
with wave.open(buf, 'wb') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(sr)
    wf.writeframes(raw_pcm)
wav_data = buf.getvalue()

aac = ce.AudioAacCompressor()
aac.set_quality(8)
r = aac.compress(wav_data)
r2 = aac.decompress(r.data)

buf2 = io.BytesIO(r2.data)
with wave.open(buf2, 'rb') as wf2:
    nf = wf2.getnframes()
    pcm = wf2.readframes(nf)
    decoded = struct.unpack('<' + 'h' * nf, pcm[:nf * 2])

delay = 1024
print(f"Compressed: {len(wav_data)} -> {len(r.data)} bytes ({len(r.data)/len(wav_data)*100:.1f}%)")
print(f"Original len={num_samples}, Decoded len={len(decoded)}")

# Check at multiple positions
check_positions = [0, 100, 500, 1000, 2000, 5000, 10000, 20000, 50000]
print(f"\n{'pos':>6} {'orig':>8} {'decoded':>8} {'diff':>8} {'error':>8}")
for pos in check_positions:
    if pos + delay < len(decoded) and pos < len(samples_int16):
        orig = samples_int16[pos]
        dec = decoded[pos + delay]
        diff = abs(orig - dec)
        err_pct = diff / max(abs(orig), 1) * 100
        print(f"{pos:6d} {orig:8d} {dec:8d} {diff:8d} {err_pct:7.1f}%")

# Compute PSNR per frame (1024-sample blocks)
print(f"\n{'frame':>6} {'PSNR':>8}")
for frame_idx in range(0, len(samples_int16), 1024):
    frame_start = frame_idx
    frame_end = min(frame_start + 1024, len(samples_int16))
    frame_len = frame_end - frame_start
    if frame_len < 1024:
        break
    dec_start = frame_start + delay
    dec_end = dec_start + frame_len
    if dec_end > len(decoded):
        break
    mse = sum((samples_int16[i] - decoded[i+delay])**2 for i in range(frame_start, frame_end)) / frame_len
    psnr = 10 * math.log10(32767**2 / mse) if mse > 0 else float('inf')
    print(f"frame{frame_idx//1024:3d} {psnr:8.2f} dB")

# Check: does decoded[1024] correlate better with original[0] or original[some_other]?
print("\n--- Cross-correlation check ---")
for offset in range(0, 50, 5):
    corr_len = min(200, len(samples_int16), len(decoded) - delay - offset)
    if corr_len > 10:
        mse = sum((samples_int16[i] - decoded[i+delay+offset])**2 for i in range(corr_len)) / corr_len
        psnr = 10 * math.log10(32767**2 / mse) if mse > 0 else float('inf')
        print(f"  delay+{offset:3d}: PSNR={psnr:.2f} dB (over {corr_len} samples)")