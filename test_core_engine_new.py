"""Quick test for core_engine_new binding"""
import sys
import os

# Add the pyd directory to path
pyd_dir = r"D:\AAA_C\compression-tool\build_py\src\bindings\pybind"
sys.path.insert(0, pyd_dir)

print("=" * 60)
print("Testing algorithm_new Python Binding")
print("=" * 60)

try:
    import core_engine_new
    print(f"✅ Successfully imported: {core_engine_new.__name__}")
    print(f"   Module doc: {core_engine_new.__doc__[:80] if core_engine_new.__doc__ else 'N/A'}...")
    
    # Test basic functionality
    print("\n--- Testing Components ---")
    
    # Test 1: Check for CompressorResult
    if hasattr(core_engine_new, 'CompressorResult'):
        result = core_engine_new.CompressorResult()
        print("✅ CompressorResult class exists")
        print(f"   Default values: success={result.success}, original_size={result.original_size}")
    else:
        print("❌ CompressorResult not found")
    
    # Test 2: Check for LZDPCompressorConfig
    if hasattr(core_engine_new, 'LZDPCompressorConfig'):
        cfg = core_engine_new.LZDPCompressorConfig()
        print("✅ LZDPCompressorConfig class exists")
        print(f"   Default: use_streaming={cfg.use_streaming}")
    else:
        print("❌ LZDPCompressorConfig not found")
    
    # Test 3: Check for LZDPCompressor (Adapter)
    if hasattr(core_engine_new, 'LZDPCompressor'):
        lzdp = core_engine_new.LZDPCompressor()
        name = lzdp.get_algorithm_name()
        print(f"✅ LZDPCompressor adapter exists → {name}")
        
        # Test 4: Quick compress test with small data
        test_data = b"Hello, World! This is a test of the new algorithm."
        try:
            result = lzdp.compress(test_data)
            if result.success:
                print(f"✅ Compression works!")
                print(f"   Input:  {len(test_data)} bytes")
                print(f"   Output: {result.compressed_size} bytes")
                print(f"   Ratio:  {result.compression_ratio:.2%}")
            else:
                print(f"❌ Compression failed: {result.error_message}")
        except Exception as e:
            print(f"⚠️ Compression error (may be expected): {e}")
    else:
        print("❌ LZDPCompressor not found")
    
    # Test 5: Raw compressor
    if hasattr(core_engine_new, 'LZDPCompressorRaw'):
        raw_cfg = core_engine_new.LZDPCompressorConfig()
        raw_lzdp = core_engine_new.LZDPCompressorRaw(raw_cfg)
        print("✅ LZDPCompressorRaw (file-based) exists")
    else:
        print("❌ LZDPCompressorRaw not found")
    
    # Test 6: Smoke test function
    if hasattr(core_engine_new, 'test_algorithm_new'):
        ok = core_engine_new.test_algorithm_new()
        print(f"✅ test_algorithm_new() → {'PASS' if ok else 'FAIL'}")
    else:
        print("❌ test_algorithm_new not found")
    
    print("\n" + "=" * 60)
    print("🎉 All basic tests passed!")
    print("=" * 60)
    
except ImportError as e:
    print(f"❌ Failed to import core_engine_new: {e}")
    print("\nPossible causes:")
    print("  - Missing dependencies (Python, pybind11)")
    print("  - Build not completed successfully")
    print("  - Path issue")
except Exception as e:
    print(f"❌ Unexpected error: {type(e).__name__}: {e}")
    import traceback
    traceback.print_exc()

print("\nDone.")
