#!/usr/bin/env python3
"""Test ADE Feature Extractor - Base Segment (20 dimensions)"""
import sys
import os
import math
import random
from pathlib import Path

os.environ['PYTHONIOENCODING'] = 'utf-8'

project_root = Path(__file__).resolve().parent
src_root = project_root / 'src'
sys.path.insert(0, str(src_root))
sys.path.insert(0, str(project_root))


def test_basic_functionality():
    """Test 1: Basic extraction functionality"""
    print("\n" + "=" * 70)
    print("TEST 1: Basic Functionality")
    print("=" * 70)
    
    from gui.core.feature_extractor import (
        FeatureExtractor, 
        BaseFeatures, 
        extract_base_features,
        get_compression_decision,
        FileType
    )
    
    # Test with simple data
    data = b"Hello World! " * 100
    
    extractor = FeatureExtractor()
    features = extractor.extract(data)
    
    assert isinstance(features, BaseFeatures), "Should return BaseFeatures instance"
    assert len(features.vector) == 20, f"Vector should have 20 dims, got {len(features.vector)}"
    
    print(f"  [PASS] Extracted 20-dimensional feature vector")
    print(f"  [INFO] Vector sample (first 5): {[f'{v:.4f}' for v in features.vector[:5]]}")
    
    return True


def test_empty_data():
    """Test 2: Handle empty/zero-length data"""
    print("\n" + "=" * 70)
    print("TEST 2: Empty Data Handling")
    print("=" * 70)
    
    from gui.core.feature_extractor import FeatureExtractor, BaseFeatures
    
    extractor = FeatureExtractor()
    features = extractor.extract(b"")
    
    assert isinstance(features, BaseFeatures), "Should return BaseFeatures even for empty"
    assert all(v == 0.0 for v in features.vector), "All features should be 0 for empty data"
    
    print(f"  [PASS] Empty data returns zero vector")
    
    return True


def test_repetitive_data():
    """Test 3: Highly repetitive data should show high compressibility"""
    print("\n" + "=" * 70)
    print("TEST 3: Repetitive Data Characteristics")
    print("=" * 70)
    
    from gui.core.feature_extractor import extract_base_features, get_compression_decision
    
    # Highly repetitive text
    repetitive = b"A" * 10000
    features, ftype = extract_base_features(repetitive)
    
    # Check expected characteristics
    print(f"  Shannon Entropy: {features.shannon_entropy:.4f} (expected: ~0.0)")
    print(f"  Unique Byte Ratio: {features.unique_byte_ratio:.4f} (expected: ~0.004)")
    print(f"  Longest Run: {features.longest_run_log2:.4f} (expected: high)")
    print(f"  RLE Potential: {features.rle_potential:.4f} (expected: low = good)")
    print(f"  Decision: {get_compression_decision(features)}")
    
    assert features.shannon_entropy < 0.1, "Repetitive data should have very low entropy"
    assert features.longest_run_log2 > 0.5, "Long run should be detected (normalized value)"
    
    print(f"  [PASS] Repetitive data correctly identified as highly compressible")
    
    return True


def test_random_data():
    """Test 4: Random data should show low compressibility"""
    print("\n" + "=" * 70)
    print("TEST 4: Random Data Characteristics")
    print("=" * 70)
    
    from gui.core.feature_extractor import extract_base_features, get_compression_decision
    
    random.seed(42)
    random_data = bytes(random.randint(0, 255) for _ in range(10000))
    features, ftype = extract_base_features(random_data)
    
    print(f"  Shannon Entropy: {features.shannon_entropy:.4f} (expected: ~7.9-8.0)")
    print(f"  Unique Byte Ratio: {features.unique_byte_ratio:.4f} (expected: ~0.95-1.0)")
    print(f"  Skewness: {features.skewness:.4f} (expected: near 0)")
    print(f"  Decision: {get_compression_decision(features)}")
    
    assert features.shannon_entropy > 7.5, "Random data should have high entropy"
    assert features.unique_byte_ratio > 0.9, "Random data should have many unique bytes"
    
    decision = get_compression_decision(features)
    if abs(features.skewness) < 0.1:
        assert decision == "SKIP", "High entropy + low skewness should SKIP"
        print(f"  [PASS] Random data correctly flagged as SKIP (encrypted/random-like)")
    else:
        print(f"  [INFO] Skewness not low enough for SKIP, decision: {decision}")
    
    return True


def test_text_data():
    """Test 5: English text should show good compressibility"""
    print("\n" + "=" * 70)
    print("TEST 5: English Text Characteristics")
    print("=" * 70)
    
    from gui.core.feature_extractor import extract_base_features, get_compression_decision
    
    text_data = """
    The quick brown fox jumps over the lazy dog. This is a sample text that contains
    common English words and phrases. Compression algorithms typically work well on natural
    language text because of the statistical properties of human languages.
    """.encode('utf-8') * 50
    
    features, ftype = extract_base_features(text_data)
    
    print(f"  Shannon Entropy: {features.shannon_entropy:.4f} (expected: 4.5-5.5)")
    print(f"  Printable Ratio: {features.printable_ratio:.4f} (expected: >0.9)")
    print(f"  Bigram Concentration: {features.bigram_topk_concentration:.4f}")
    print(f"  Decision: {get_compression_decision(features)}")
    
    assert 4.0 < features.shannon_entropy < 6.5, "Text should have medium entropy"
    assert features.printable_ratio > 0.85, "Text should be mostly printable"
    
    decision = get_compression_decision(features)
    assert decision == "COMPRESS", "Text should be marked as COMPRESS"
    
    print(f"  [PASS] Text data correctly identified as good compression candidate")
    
    return True


def test_magic_bytes_detection():
    """Test 6: Magic bytes detection for various formats"""
    print("\n" + "=" * 70)
    print("TEST 6: Magic Bytes Detection")
    print("=" * 70)
    
    from gui.core.feature_extractor import extract_base_features, FileType
    
    test_cases = [
        (b'\x89PNG\r\n\x1a\n' + b'\x00' * 100, FileType.IMAGE_PNG, "PNG image"),
        (b'\xff\xd8\xff\xe0' + b'\x00' * 100, FileType.IMAGE_JPEG, "JPEG image"),
        (b'PK\x03\x04' + b'\x00' * 100, FileType.ARCHIVE_ZIP, "ZIP archive"),
        (b'%PDF-1.4' + b'\x00' * 100, FileType.DOC_PDF, "PDF document"),
        (b'GIF89a' + b'\x00' * 100, FileType.IMAGE_GIF, "GIF image"),
    ]
    
    for data, expected_type, description in test_cases:
        features, detected_type = extract_base_features(data)
        
        status = "✓" if detected_type == expected_type else "✗"
        print(f"  {status} {description}: {detected_type.name} (confidence: {features.magic_confidence:.2f})")
        
        assert detected_type == expected_type, f"Failed to detect {description}"
        assert features.magic_confidence > 0.85, f"Low confidence for {description}"
    
    print(f"\n  [PASS] All magic byte patterns correctly detected")
    
    return True


def test_feature_ranges():
    """Test 7: Verify all features are within expected ranges"""
    print("\n" + "=" * 70)
    print("TEST 7: Feature Value Range Validation")
    print("=" * 70)
    
    from gui.core.feature_extractor import FeatureExtractor, BaseFeatures
    
    # Generate mixed test data
    data = bytes(range(256)) * 10 + b"Hello World!" * 100
    extractor = FeatureExtractor()
    features = extractor.extract(data)
    
    # Define expected ranges for each dimension
    ranges = {
        'file_size_log2': (0.0, 1.5),
        'magic_confidence': (0.0, 1.0),
        'printable_ratio': (0.0, 1.0),
        'shannon_entropy': (0.0, 8.0),
        'min_entropy': (0.0, 8.0),
        'unique_byte_ratio': (0.0039, 1.0),
        'mean_byte_normalized': (0.0, 1.0),
        'std_byte_normalized': (0.0, 1.0),
        'longest_run_log2': (0.0, 1.0),
        'zero_byte_ratio': (0.0, 1.0),
        'high_bit_ratio': (0.0, 1.0),
        'header_entropy': (0.0, 8.0),
        'local_entropy_variance': (0.0, 10.0),
        'block_boundary_density': (0.0, 1.0),
        'skewness': (-1.0, 1.0),
        'kurtosis': (-2.0, 10.0),
        'unique_bigram_ratio': (0.0, 1.0),
        'bigram_topk_concentration': (0.0, 1.0),
        'rle_potential': (0.0, 2.0),
        'dict_potential': (0.0, 1.0),
    }
    
    all_valid = True
    vec = features.to_dict()
    
    for name, (low, high) in ranges.items():
        value = vec[name]
        valid = low <= value <= high
        
        if not valid:
            print(f"  ✗ {name}: {value:.4f} OUT OF RANGE [{low}, {high}]")
            all_valid = False
            
    if all_valid:
        print(f"  [PASS] All 20 features within expected ranges")
        for name, value in list(vec.items())[:10]:
            print(f"    {name}: {value:.4f}")
        print(f"    ... ({len(vec) - 10} more features)")
    
    assert all_valid, "Some features out of range"
    
    return True


def test_serialization():
    """Test 8: Test dict serialization for JSON storage"""
    print("\n" + "=" * 70)
    print("TEST 8: JSON Serialization")
    print("=" * 70)
    
    from gui.core.feature_extractor import extract_base_features
    import json
    
    data = b"Test data for serialization! " * 100
    features, _ = extract_base_features(data)
    
    # Convert to dict
    feat_dict = features.to_dict()
    
    # Serialize to JSON
    json_str = json.dumps(feat_dict, indent=2)
    
    # Deserialize
    restored = json.loads(json_str)
    
    # Verify integrity
    assert len(restored) == 20, f"Expected 20 features, got {len(restored)}"
    
    max_diff = max(abs(restored[k] - v) for k, v in feat_dict.items())
    assert max_diff < 1e-6, f"Serialization error: max diff {max_diff}"
    
    print(f"  [PASS] JSON serialization/deserialization successful")
    print(f"  [INFO] JSON size: {len(json_str)} bytes for 20 features")
    
    return True


def test_performance():
    """Test 9: Performance benchmark on larger files"""
    print("\n" + "=" * 70)
    print("TEST 9: Performance Benchmark")
    print("=" * 70)
    
    import time
    from gui.core.feature_extractor import FeatureExtractor
    
    sizes = [
        (1 * 1024, "1 KB"),
        (100 * 1024, "100 KB"),
        (1 * 1024 * 1024, "1 MB"),
    ]
    
    extractor = FeatureExtractor()
    
    for size_bytes, label in sizes:
        random.seed(123)
        data = bytes(random.randint(0, 255) for _ in range(size_bytes))
        
        start = time.perf_counter()
        features = extractor.extract(data)
        elapsed_ms = (time.perf_counter() - start) * 1000
        
        throughput_mb_s = (size_bytes / (1024 * 1024)) / (elapsed_ms / 1000)
        
        print(f"  {label:>6s}: {elapsed_ms:>8.2f} ms ({throughput_mb_s:>8.1f} MB/s)")
    
    print(f"\n  [INFO] Target: >10 MB/s for practical use")
    print(f"  [PASS] Performance test completed")
    
    return True


def main():
    """Run all tests"""
    print("\n" + "=" * 70)
    print("  ADE Feature Extractor - Base Segment Test Suite")
    print("  Version: 3.0 Final (20-dimensions)")
    print("=" * 70)
    
    tests = [
        ("Basic Functionality", test_basic_functionality),
        ("Empty Data Handling", test_empty_data),
        ("Repetitive Data", test_repetitive_data),
        ("Random Data", test_random_data),
        ("English Text", test_text_data),
        ("Magic Bytes Detection", test_magic_bytes_detection),
        ("Feature Range Validation", test_feature_ranges),
        ("JSON Serialization", test_serialization),
        ("Performance Benchmark", test_performance),
    ]
    
    results = []
    for name, test_func in tests:
        try:
            success = test_func()
            results.append((name, success))
        except Exception as e:
            print(f"\n  ✗ {name} FAILED with exception: {e}")
            import traceback
            traceback.print_exc()
            results.append((name, False))
    
    # Summary
    print("\n" + "=" * 70)
    print("  TEST SUMMARY")
    print("=" * 70)
    
    passed = sum(1 for _, s in results if s)
    total = len(results)
    
    for name, success in results:
        status = "✓ PASS" if success else "✗ FAIL"
        print(f"  {status}: {name}")
    
    print(f"\n  Total: {passed}/{total} tests passed")
    
    if passed == total:
        print(f"\n  🎉 ALL TESTS PASSED!")
        return 0
    else:
        print(f"\n  ⚠️  {total - passed} test(s) failed")
        return 1


if __name__ == "__main__":
    exit_code = main()
    sys.exit(exit_code)
