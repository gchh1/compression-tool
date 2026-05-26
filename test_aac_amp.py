import sys
import struct
import math
import wave
import io

sys.path.insert(0, r'd:\AAA_C\compression-tool\build_py\src\bindings\pybind_new')
import core_engine_new as ce

sr = 44100
duration = 2.0

for amp, lbl in [(20000, "loud"), (5000, "mid"), (100, "quiet"), (0, "dc")]:
    num_samples = int(sr * duration)
    if amp == 0:
        samples_int16 = [10000] * num_samples  # DC
    else:
        samples_int16 = [int(amp * math.sin(2 * math.pi * 440 * i / sr)) for i in range(num_samples)]

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
    aligned_len = min(len(samples_int16), len(decoded) - delay)
    if aligned_len > 0:
        mse = sum((samples_int16[i] - decoded[i + delay])**2 for i in range(aligned_len)) / aligned_len
        psnr = 10 * math.log10(32767**2 / mse) if mse > 0 else float('inf')
    else:
        psnr = 0

    ratio = len(r.data) / len(wav_data) * 100
    print(f"{lbl:7s} (amp={amp:5d}): PSNR={psnr:.2f} dB, ratio={ratio:.1f}%, "
          f"decoded[1024..1034]={list(decoded[1024:1034])}, "
          f"orig[0..5]={samples_int16[:5]}")