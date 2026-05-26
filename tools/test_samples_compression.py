"""Quick compression test for new audio samples using AAC-LC codec."""
import sys
import struct
import math
import wave
import io
import time
import os

sys.path.insert(0, r'd:\AAA_C\compression-tool\build_py\src\bindings\pybind_new')
import core_engine_new as ce

SAMPLES_DIR = r"d:\AAA_C\compression-tool\resources\audio_samples"

# Only test WAV files (skip MP3 and stereo for simplicity)
files = sorted([f for f in os.listdir(SAMPLES_DIR) if f.endswith('.wav')])

print(f"{'File':<30} {'Original':>10} {'Compressed':>10} {'Ratio':>8} {'PSNR':>8}")
print("-" * 72)

delay = 1024
aac = ce.AudioAacCompressor()
aac.set_quality(8)

for fname in files:
    path = os.path.join(SAMPLES_DIR, fname)
    with open(path, 'rb') as fh:
        wav_data = fh.read()

    # Check if stereo — skip PSNR for stereo files
    buf = io.BytesIO(wav_data)
    with wave.open(buf, 'rb') as wf:
        nch = wf.getnchannels()
        sw = wf.getsampwidth()
        fs = wf.getframerate()
        nf = wf.getnframes()
        pcm_raw = wf.readframes(nf)

    fmt = '<' + ('h' * (nf * nch))
    samples_flat = struct.unpack(fmt, pcm_raw[:nf * nch * 2])

    t0 = time.time()
    r = aac.compress(wav_data)
    t1 = time.time()

    if not r.success:
        print(f"{fname:<30} {'FAIL:'+r.error_message}")
        continue

    r2 = aac.decompress(r.data)

    if not r2.success or len(r2.data) < 44:
        print(f"{fname:<30} {'DECOMPRESS FAIL'}")
        continue

    buf2 = io.BytesIO(r2.data)
    with wave.open(buf2, 'rb') as wf2:
        nf2 = wf2.getnframes()
        pcm2 = wf2.readframes(nf2)
        decoded = struct.unpack('<' + 'h' * (nf2 * nch), pcm2[:nf2 * nch * 2])

    # PSNR (decode ch0 only, aligned)
    mse = 0.0
    n_compared = 0
    if nch == 1:
        min_len = min(nf, nf2 - delay)
        for i in range(min_len):
            diff = samples_flat[i] - decoded[(i + delay) * nch]
            mse += diff * diff
            n_compared += 1
    # For stereo, compare ch0 only
    elif nch == 2:
        min_len = min(nf, nf2 - delay)
        for i in range(min_len):
            diff = samples_flat[i * 2] - decoded[(i + delay) * 2]
            mse += diff * diff
            n_compared += 1

    if n_compared > 0:
        mse /= n_compared
        psnr = 10 * math.log10(32767**2 / mse) if mse > 0 else float('inf')
    else:
        psnr = float('nan')

    ratio = len(r.data) / len(wav_data) * 100
    psnr_str = f"{psnr:.1f} dB" if not math.isinf(psnr) else "inf"
    print(f"{fname:<30} {len(wav_data):>8} B {len(r.data):>8} B {ratio:>6.1f}% {psnr_str:>8}")

print("-" * 72)
print("Done!")