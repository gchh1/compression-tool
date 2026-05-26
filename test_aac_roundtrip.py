import sys
import struct
import math
import wave
import io
import time

sys.path.insert(0, r'd:\AAA_C\compression-tool\build_py\src\bindings\pybind_new')
import core_engine_new as ce

# Generate mono 44100Hz 16-bit test tone: 440Hz sine, 2 seconds
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

print(f"Original WAV: {len(wav_data)} bytes, {num_samples} samples")
print(f"First 10 original: {samples_int16[:10]}")

# Compress with AAC-LC
aac = ce.AudioAacCompressor()
aac.set_quality(8)
t0 = time.time()
r = aac.compress(wav_data)
t1 = time.time()
ratio = len(r.data) / len(wav_data) * 100
print(f"Compressed: {len(wav_data)} -> {len(r.data)} bytes ({ratio:.1f}%), time={t1-t0:.3f}s, success={r.success}")

if not r.success:
    print(f"COMPRESS FAILED: {r.error_message}")
    sys.exit(1)

# Decompress
t2 = time.time()
r2 = aac.decompress(r.data)
t3 = time.time()
print(f"Decompressed: {len(r2.data)} bytes, time={t3-t2:.3f}s, success={r2.success}")

if not r2.success:
    print(f"DECOMPRESS FAILED: {r2.error_message}")
    sys.exit(1)

# Parse WAV
buf2 = io.BytesIO(r2.data)
with wave.open(buf2, 'rb') as wf2:
    nch = wf2.getnchannels()
    sw = wf2.getsampwidth()
    fs = wf2.getframerate()
    nf = wf2.getnframes()
    pcm = wf2.readframes(nf)
    decoded = struct.unpack('<' + 'h' * nf, pcm[:nf * 2])

print(f"Decoded: channels={nch}, sample_rate={fs}, frames={nf}")
print(f"First 10 decoded: {list(decoded[:10])}")

# PSNR - raw (mismatched alignment due to encoder delay)
min_len = min(len(samples_int16), len(decoded))
mse = sum((samples_int16[i] - decoded[i]) ** 2 for i in range(min_len)) / min_len
if mse > 0:
    psnr = 10 * math.log10(32767 ** 2 / mse)
else:
    psnr = float('inf')
print(f"MSE={mse:.2f}, PSNR={psnr:.2f} dB (misaligned)")

# Encoder delay = 1024 samples (1 frame), decoded[i+1024] ~= original[i]
delay = 1024
if len(decoded) > delay and len(samples_int16) > delay:
    aligned_len = min(len(samples_int16), len(decoded) - delay)
    mse2 = sum((samples_int16[i] - decoded[i + delay]) ** 2 for i in range(aligned_len)) / aligned_len
    if mse2 > 0:
        psnr2 = 10 * math.log10(32767 ** 2 / mse2)
    else:
        psnr2 = float('inf')
    print(f"After delay alignment: MSE={mse2:.2f}, PSNR={psnr2:.2f} dB")

# Show samples around the transition region
print(f"Original[0:10]:    {samples_int16[0:10]}")
print(f"Decoded[0:10]:     {list(decoded[0:10])}")
print(f"Decoded[{delay}:{delay+10}]:   {list(decoded[delay:delay+10])}")
print(f"Original[100:110]:  {samples_int16[100:110]}")
print(f"Decoded[{delay+100}:{delay+110}]: {list(decoded[delay+100:delay+110])}")