"""
Additional audio samples — noise types, pure tones, chord progressions.
Useful for compression testing across different entropy levels.
"""
import struct
import math
import wave
import os
import random

OUT_DIR = r"d:\AAA_C\compression-tool\resources\audio_samples"
SR = 44100
AMP16 = 32767
os.makedirs(OUT_DIR, exist_ok=True)


def write_wav(path, samples_mono):
    raw = struct.pack('<' + 'h' * len(samples_mono),
                      *[max(-32768, min(32767, int(s))) for s in samples_mono])
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(raw)
    size_kb = os.path.getsize(path) / 1024
    dur = len(samples_mono) / SR
    print(f"  OK: {os.path.basename(path)} — {dur:.1f}s, {size_kb:.0f} KB")


def write_stereo(path, left, right):
    """Write stereo 16-bit WAV"""
    n = min(len(left), len(right))
    interleaved = []
    for i in range(n):
        interleaved.append(max(-32768, min(32767, int(left[i]))))
        interleaved.append(max(-32768, min(32767, int(right[i]))))
    raw = struct.pack('<' + 'h' * (n * 2), *interleaved)
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(raw)
    size_kb = os.path.getsize(path) / 1024
    dur = n / SR
    print(f"  OK: {os.path.basename(path)} — {dur:.1f}s, {size_kb:.0f} KB (stereo)")


# ── 1. White Noise (maximum entropy — hard to compress) ──
print("[1] White noise (uniform, 3s)")
n = int(SR * 3.0)
white = [AMP16 * 0.5 * random.uniform(-1, 1) for _ in range(n)]
write_wav(os.path.join(OUT_DIR, "white_noise.wav"), white)

# ── 2. Pink Noise (1/f spectrum, more natural) ──
print("[2] Pink noise (1/f, 3s)")
n = int(SR * 3.0)
pink = [0.0] * n
b = [0.0] * 7
for i in range(n):
    white_sample = random.uniform(-1, 1)
    b[0] = 0.99886 * b[0] + white_sample * 0.0555179
    b[1] = 0.99332 * b[1] + white_sample * 0.0750759
    b[2] = 0.96900 * b[2] + white_sample * 0.1538520
    b[3] = 0.86650 * b[3] + white_sample * 0.3104856
    b[4] = 0.55000 * b[4] + white_sample * 0.5329522
    b[5] = -0.7616 * b[5] - white_sample * 0.0168980
    pink[i] = sum(b) * 0.11
pink_max = max(abs(x) for x in pink)
if pink_max > 0:
    pink = [AMP16 * 0.6 * x / pink_max for x in pink]
write_wav(os.path.join(OUT_DIR, "pink_noise.wav"), pink)

# ── 3. Sine 440Hz (pure tone — easy to compress) ──
print("[3] Pure 440Hz sine (2s)")
n = int(SR * 2.0)
sine = [int(AMP16 * 0.9 * math.sin(2 * math.pi * 440 * i / SR)) for i in range(n)]
write_wav(os.path.join(OUT_DIR, "sine_440hz.wav"), sine)

# ── 4. Chord progression I-V-vi-IV in C (pop music pattern) ──
print("[4] Pop chord progression (I-V-vi-IV in C, 8 bars, 120 BPM)")
# C major, G major, A minor, F major
chords = [
    [(261.63, 329.63, 392.00)],   # C: C-E-G
    [(392.00, 493.88, 587.33)],   # G: G-B-D
    [(220.00, 261.63, 329.63)],   # Am: A-C-E
    [(174.61, 261.63, 349.23)],   # F: F-A-C
]
bar_len = int(SR * 2.0)  # 2 seconds per bar at 120 BPM = 4 beats
samples = []
for _ in range(2):  # 2 repetitions = 8 bars
    for chord in chords:
        for i in range(bar_len):
            t = i / SR
            env = math.exp(-0.3 * (i % (bar_len // 4)) / SR) * 0.8
            y = 0.0
            for freq in chord[0]:
                y += math.sin(2 * math.pi * freq * t)
            samples.append(AMP16 * 0.2 * y * (0.5 + 0.5 * env))
write_wav(os.path.join(OUT_DIR, "chord_progression.wav"), samples)

# ── 5. Silence (maximum compressibility) ──
print("[5] Digital silence (1s)")
silence = [0] * SR
write_wav(os.path.join(OUT_DIR, "silence.wav"), silence)

# ── 6. Bell/Sine arpeggio — high-frequency pure tones ──
print("[6] Bell arpeggio (5kHz-15kHz)")
freqs = [523.25, 659.25, 783.99, 1046.5, 1318.5, 1568.0, 2093.0, 2637.0, 3136.0]
note_len = int(SR * 0.2)
samples = []
for freq in freqs:
    for i in range(note_len):
        t = i / SR
        env = math.exp(-4.0 * t)
        y = math.sin(2 * math.pi * freq * t)
        y += 0.5 * math.sin(2 * math.pi * freq * 2 * t)
        y += 0.25 * math.sin(2 * math.pi * freq * 3 * t)
        samples.append(AMP16 * 0.5 * y * env)
write_wav(os.path.join(OUT_DIR, "bell_arpeggio.wav"), samples)

# ── 7. Stereo panning sweep ──
print("[7] Stereo panning sweep (440Hz L→R)")
n = int(SR * 3.0)
left_ch = []
right_ch = []
for i in range(n):
    t = i / SR
    pan = 0.5 + 0.5 * math.sin(2 * math.pi * 0.5 * t)
    freq = 440.0
    sig = int(AMP16 * 0.8 * math.sin(2 * math.pi * freq * t))
    left_ch.append(int(sig * (1 - pan)))
    right_ch.append(int(sig * pan))
write_stereo(os.path.join(OUT_DIR, "stereo_panning.wav"), left_ch, right_ch)

# ── List results ──
print(f"\nAll files in {OUT_DIR}:")
for f in sorted(os.listdir(OUT_DIR)):
    path = os.path.join(OUT_DIR, f)
    size_kb = os.path.getsize(path) / 1024
    print(f"  {f} — {size_kb:.0f} KB")

print(f"\nTotal: {len(os.listdir(OUT_DIR))} files")