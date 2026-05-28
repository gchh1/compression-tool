"""
Test script for OpenH264 codec via pybind - using VideoOpenH264Compressor directly.
"""
import sys
import os
import struct

# Find the build directory
pyd_path = None
for root, dirs, files in os.walk(r'd:\AAA_C\compression-tool\build_debug'):
    for f in files:
        if f.endswith('.pyd') and 'core_engine' in f:
            pyd_path = os.path.join(root, f)
            sys.path.insert(0, os.path.dirname(pyd_path))
            break
    if pyd_path:
        break

print(f"Found pyd at: {pyd_path}")
import core_engine_new as ce

# Create test video data (2 frames of 80x64 RGB)
w, h = 80, 64
num_frames = 2
frame_size = w * h * 3
total_size = 16 + num_frames * frame_size

raw = bytearray(total_size)
struct.pack_into('<IIII', raw, 0, w, h, num_frames, 30)

for fnum in range(num_frames):
    offset = 16 + fnum * frame_size
    for y in range(h):
        for x in range(w):
            idx = offset + (y * w + x) * 3
            r = (x + fnum * 10) % 256
            g = (y + fnum * 10) % 256
            b = (x + y + fnum * 10) % 256
            raw[idx] = b
            raw[idx+1] = g
            raw[idx+2] = r

raw_bytes = bytes(raw)
print(f"Input raw video: {len(raw_bytes)} bytes, {w}x{h}x{num_frames}")

# Test VideoOpenH264Compressor directly
comp = ce.VideoOpenH264Compressor()
comp.set_quality(23)
print("Created VideoOpenH264Compressor, quality=23")

result = comp.compress(raw_bytes)
print(f"Compress result type: {type(result)}")
print(f"Compress result success: {result.success}")
print(f"Compress result data len: {len(result.data) if result.data else 0}")
print(f"Compress result error: {result.error_message}")

if result.data and len(result.data) > 0:
    # Verify it starts with H.264 NAL start code
    print(f"First 16 bytes hex: {result.data[:16].hex()}")
    
    # Test decompress
    decomp_result = comp.decompress(result.data)
    print(f"Decompress result success: {decomp_result.success}")
    print(f"Decompress result data len: {len(decomp_result.data) if decomp_result.data else 0}")

print("Done")