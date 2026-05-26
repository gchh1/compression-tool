import sys
import struct
import math
import wave
import io
import time
import os

sys.path.insert(0, r'd:\AAA_C\compression-tool\build_py\src\bindings\pybind_new')
import core_engine_new as ce

cpp_file = r'd:\AAA_C\compression-tool\src\algorithm_new\AudioCodecAac.cpp'
build_dir = r'd:\AAA_C\compression-tool\build_py'

sr = 44100
duration = 0.5
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

base_values = [0.02, 0.05, 0.1, 0.2, 0.35, 0.5, 0.75]

print(f"{'base':>8} {'PSNR':>8} {'bytes':>8} {'ratio':>7} {'max_q':>8}")
print("-" * 50)

for base in base_values:
    # Modify C++ base value
    with open(cpp_file, 'r', encoding='utf-8') as f:
        content = f.read()

    import re
    new_content = re.sub(r'double base = [\d.]+;', f'double base = {base};', content)
    with open(cpp_file, 'w', encoding='utf-8') as f:
        f.write(new_content)

    # Rebuild
    cmd = f'cmake --build {build_dir} --config Release --target core_engine_new 2>&1'
    ret = os.system(cmd)
    if ret != 0:
        print(f"{base:8.3f}  BUILD FAILED")
        continue

    # Reload module
    import importlib
    importlib.reload(ce)

    aac = ce.AudioAacCompressor()
    aac.set_quality(8)
    r = aac.compress(wav_data)
    if not r.success:
        print(f"{base:8.3f}  COMPRESS FAILED: {r.error_message}")
        continue

    r2 = aac.decompress(r.data)
    if not r2.success:
        print(f"{base:8.3f}  DECOMPRESS FAILED: {r2.error_message}")
        continue

    buf2 = io.BytesIO(r2.data)
    with wave.open(buf2, 'rb') as wf2:
        nf = wf2.getnframes()
        pcm = wf2.readframes(nf)
        decoded = struct.unpack('<' + 'h' * nf, pcm[:nf * 2])

    delay = 1024
    aligned_len = min(len(samples_int16), len(decoded) - delay)
    if aligned_len > 0:
        mse = sum((samples_int16[i] - decoded[i + delay]) ** 2 for i in range(aligned_len)) / aligned_len
        if mse > 0:
            psnr = 10 * math.log10(32767 ** 2 / mse)
        else:
            psnr = float('inf')
    else:
        psnr = 0

    ratio = len(r.data) / len(wav_data) * 100
    print(f"{base:8.3f} {psnr:8.2f} {len(r.data):8d} {ratio:6.1f}%")

print("\nDone!")