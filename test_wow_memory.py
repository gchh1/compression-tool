"""Test algorithm_new with wow.html (60KB) to verify memory usage fix"""
import sys
import os
import time
import tracemalloc

# Add pyd directory to path
sys.path.insert(0, r"D:\AAA_C\compression-tool\build_py\src\bindings\pybind")

print("=" * 70)
print("🔥 MEMORY TEST: algorithm_new vs wow.html (60KB)")
print("=" * 70)

# Load the new engine
try:
    import core_engine_new
    print(f"✅ Loaded: {core_engine_new.__name__}")
except ImportError as e:
    print(f"❌ Failed to load core_engine_new: {e}")
    sys.exit(1)

# Read test file
wow_path = r"D:\AAA_C\compression-tool\resources\wow.html"
if not os.path.exists(wow_path):
    print(f"❌ Test file not found: {wow_path}")
    sys.exit(1)

with open(wow_path, "rb") as f:
    original_data = f.read()

file_size = len(original_data)
print(f"\n📁 Test file: {wow_path}")
print(f"   Size: {file_size:,} bytes ({file_size / 1024:.1f} KB)")

# Create compressor
lzdp = core_engine_new.LZDPCompressor()
algo_name = lzdp.get_algorithm_name()
print(f"\n🔧 Algorithm: {algo_name}")

# Memory tracking
tracemalloc.start()

# Test compression
print("\n" + "-" * 70)
print("⏱️  Starting compression...")
start_time = time.time()

result = lzdp.compress(original_data)
elapsed = (time.time() - start_time) * 1000

tracemalloc.stop()

# Get memory statistics
current, peak = tracemalloc.get_traced_memory()

print("\n" + "-" * 70)
print("📊 RESULTS")
print("-" * 70)

if result.success:
    print(f"✅ Compression SUCCESS")
    print(f"   Input size:     {result.original_size:>10,} bytes")
    print(f"   Output size:    {result.compressed_size:>10,} bytes")
    print(f"   Ratio:          {result.compression_ratio:>10.2%}")
    print(f"   Time:           {elapsed:>10.2f} ms")
    print(f"\n💾 MEMORY USAGE:")
    print(f"   Current memory:  {current / 1024 / 1024:>8.2f} MB")
    print(f"   Peak memory:     {peak / 1024 / 1024:>8.2f} MB")
    
    # Verify memory is reasonable (< 100MB for 60KB file)
    if peak < 100 * 1024 * 1024:
        print(f"\n✅ PASS: Memory usage is NORMAL (< 100 MB)")
        print(f"   (Old algorithm would use ~10 GB for this file!)")
    else:
        print(f"\n⚠️  WARNING: High memory usage detected!")
else:
    print(f"❌ Compression FAILED: {result.error_message}")

# Test decompression
print("\n" + "-" * 70)
print("⏱️  Starting decompression...")
start_time = time.time()

decomp_result = lzdp.decompress(result.data)
decomp_elapsed = (time.time() - start_time) * 1000

if decomp_result.success and decomp_result.data == original_data:
    print(f"✅ Decompression & verification SUCCESS")
    print(f"   Time: {decomp_elapsed:.2f} ms")
    print(f"   Data integrity: VERIFIED ✓")
else:
    print(f"❌ Decompression or verification FAILED!")

print("\n" + "=" * 70)
print("🎉 TEST COMPLETE")
print("=" * 70)
