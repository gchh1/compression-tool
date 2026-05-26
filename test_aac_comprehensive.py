import sys
import struct
import math
import wave
import io
import time

sys.path.insert(0, r'd:\AAA_C\compression-tool\build_py\src\bindings\pybind_new')
import core_engine_new as ce

SR = 44100
DURATION = 1.5
AMPLITUDE = 20000


def make_wav(samples_2d, sample_rate=SR):
    """samples_2d: list of lists, one per channel"""
    num_channels = len(samples_2d)
    num_frames = len(samples_2d[0])
    interleaved = []
    for i in range(num_frames):
        for ch in range(num_channels):
            interleaved.append(samples_2d[ch][i])
    raw_pcm = struct.pack('<' + 'h' * (num_frames * num_channels), *interleaved)
    buf = io.BytesIO()
    with wave.open(buf, 'wb') as wf:
        wf.setnchannels(num_channels)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(raw_pcm)
    return buf.getvalue()


def test_roundtrip(name, wav_data, num_channels, num_samples, original_2d=None):
    print(f"\n{'='*60}")
    print(f"Test: {name} ({num_channels}ch, {num_samples} samples)")
    print(f"{'='*60}")

    aac = ce.AudioAacCompressor()
    aac.set_quality(8)

    t0 = time.time()
    r = aac.compress(wav_data)
    t1 = time.time()
    ratio = len(r.data) / len(wav_data) * 100
    print(f"Compress: {len(wav_data)} -> {len(r.data)} bytes ({ratio:.1f}%), time={t1-t0:.3f}s, success={r.success}")

    if not r.success:
        print(f"FAIL: {r.error_message}")
        return

    t2 = time.time()
    r2 = aac.decompress(r.data)
    t3 = time.time()
    print(f"Decompress: {len(r2.data)} bytes, time={t3-t2:.3f}s, success={r2.success}")

    if not r2.success:
        print(f"FAIL: {r2.error_message}")
        return

    buf2 = io.BytesIO(r2.data)
    with wave.open(buf2, 'rb') as wf2:
        nch = wf2.getnchannels()
        nf = wf2.getnframes()
        pcm = wf2.readframes(nf)
        decoded_flat = struct.unpack('<' + 'h' * (nf * nch), pcm[:nf * nch * 2])

    decoded_2d = []
    for ch in range(nch):
        decoded_2d.append([decoded_flat[i * nch + ch] for i in range(nf)])

    print(f"Decoded: {nch}ch, {nf} frames")

    if original_2d is None:
        print("  (no original reference for PSNR comparison)")
        return

    delay = 1024
    min_frames = min(nf - delay, num_samples)
    for ch in range(min(nch, len(original_2d))):
        mse = sum((original_2d[ch][i] - decoded_2d[ch][i + delay]) ** 2
                  for i in range(min_frames)) / min_frames
        if mse > 0:
            psnr = 10 * math.log10(32767 ** 2 / mse)
        else:
            psnr = float('inf')
        print(f"Ch{ch}: MSE={mse:.4f}, PSNR={psnr:.2f} dB (aligned, delay={delay})")

    total_mse = 0
    total_n = 0
    for ch in range(min(nch, len(original_2d))):
        for i in range(min_frames):
            diff = original_2d[ch][i] - decoded_2d[ch][i + delay]
            total_mse += diff * diff
            total_n += 1
    total_mse /= total_n
    if total_mse > 0:
        total_psnr = 10 * math.log10(32767 ** 2 / total_mse)
    else:
        total_psnr = float('inf')
    print(f"Overall: MSE={total_mse:.4f}, PSNR={total_psnr:.2f} dB")


# ── Test 1: Mono sine (baseline) ──
print("\n" + "=" * 70)
print("TEST SUITE: AAC-LC Comprehensive")
print("=" * 70)

num_samples = int(SR * DURATION)
freq = 440.0
samples_mono = [[int(AMPLITUDE * math.sin(2 * math.pi * freq * i / SR))
                  for i in range(num_samples)]]
wav_mono = make_wav(samples_mono)
test_roundtrip("Mono 440Hz Sine", wav_mono, 1, num_samples, samples_mono)

# ── Test 2: Stereo sine ──
freq_l, freq_r = 440.0, 554.37
samples_stereo = [
    [int(AMPLITUDE * math.sin(2 * math.pi * freq_l * i / SR)) for i in range(num_samples)],
    [int(AMPLITUDE * math.sin(2 * math.pi * freq_r * i / SR)) for i in range(num_samples)],
]
wav_stereo = make_wav(samples_stereo)
test_roundtrip("Stereo Sine (L=440Hz, R=554Hz)", wav_stereo, 2, num_samples, samples_stereo)

# ── Test 3: Transient (click/drum) ──
# Short sharp pulse to trigger short blocks
transient_mono = []
for i in range(num_samples):
    t = i / SR
    v = 0.0
    if 0.2 < t < 0.22:
        v = 32000 * (0.5 - 0.5 * math.cos(2 * math.pi * (t - 0.2) / 0.02))
        v *= math.exp(-50 * (t - 0.2))
    if 0.7 < t < 0.72:
        v = 32000 * (0.5 - 0.5 * math.cos(2 * math.pi * (t - 0.7) / 0.02))
        v *= math.exp(-50 * (t - 0.7))
    v += int(2000 * math.sin(2 * math.pi * 60 * t))
    transient_mono.append(int(max(-32768, min(32767, v))))
samples_transient = [transient_mono]
wav_transient = make_wav(samples_transient)
test_roundtrip("Mono Transient (clicks + 60Hz hum)", wav_transient, 1, num_samples, samples_transient)

# ── Test 4: Multi-tone (simulates music complexity) ──
multitone_mono = []
notes = [261.63, 329.63, 392.00, 523.25, 659.25, 783.99, 1046.5]
for i in range(num_samples):
    t = i / SR
    v = 0.0
    for note in notes:
        v += 2500 * math.sin(2 * math.pi * note * t)
    v += 1000 * math.sin(2 * math.pi * 1500 * t)
    multitone_mono.append(int(max(-32768, min(32767, v))))
samples_multitone = [multitone_mono]
wav_multitone = make_wav(samples_multitone)
test_roundtrip("Mono Multi-tone (7 notes Cmaj7 + 1.5kHz)", wav_multitone, 1, num_samples, samples_multitone)

# ── Test 5: Sweep (chirp 20Hz -> 18kHz) ──
sweep_mono = []
f_start, f_end = 20.0, 18000.0
for i in range(num_samples):
    t = i / SR
    freq = f_start * math.pow(f_end / f_start, t / DURATION)
    phase = 2 * math.pi * f_start * DURATION / math.log(f_end / f_start) * \
            (math.pow(f_end / f_start, t / DURATION) - 1)
    v = int(AMPLITUDE * math.sin(phase))
    sweep_mono.append(v)
samples_sweep = [sweep_mono]
wav_sweep = make_wav(samples_sweep)
test_roundtrip("Mono Frequency Sweep (20Hz-18kHz)", wav_sweep, 1, num_samples, samples_sweep)

print("\n" + "=" * 70)
print("ALL TESTS COMPLETE")
print("=" * 70)