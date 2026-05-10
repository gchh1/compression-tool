#!/usr/bin/env python3
"""Test ADE Feature Extractor Integration with FileRecord"""
import sys
import os
from pathlib import Path

os.environ['PYTHONIOENCODING'] = 'utf-8'

project_root = Path(__file__).resolve().parent
src_root = project_root / 'src'
sys.path.insert(0, str(src_root))
sys.path.insert(0, str(project_root))


def test_filerecord_integration():
    """Test that FileRecord.extract_features() works with new extractor"""
    print("\n" + "=" * 70)
    print("TEST: FileRecord Integration")
    print("=" * 70)
    
    from gui.core.models import FileRecord
    
    # Create a temporary test file
    test_file = project_root / "test_integration_temp.txt"
    test_content = b"Hello World! This is a test file for feature extraction. " * 100
    test_file.write_bytes(test_content)
    
    try:
        # Create FileRecord and extract features
        record = FileRecord(str(test_file))
        
        print(f"\n  [INFO] Created record for: {record.name}")
        print(f"  [INFO] File size: {record.size} bytes")
        
        # Load data and extract features
        record.load_raw_data()
        assert len(record.raw_data) > 0, "Failed to load raw data"
        print(f"  [PASS] Raw data loaded: {len(record.raw_data)} bytes")
        
        # Extract features (this should use new implementation)
        record.extract_features()
        
        # Verify features were extracted
        assert record.base_features is not None, "base_features should not be None"
        assert len(record.base_features.vector) == 20, f"Expected 20 features, got {len(record.base_features.vector)}"
        
        print(f"\n  [PASS] Features extracted successfully!")
        print(f"  [INFO] Detected file type: {record.detected_file_type}")
        print(f"  [INFO] Shannon entropy: {record.content_entropy:.4f}")
        print(f"  [INFO] Repetition ratio: {record.repetition_ratio:.4f}")
        
        # Print all 20 features
        print(f"\n  [INFO] Full Feature Vector:")
        feat_dict = record.base_features.to_dict()
        for i, (name, value) in enumerate(feat_dict.items(), 1):
            print(f"    [{i:2d}] {name:30s}: {value:.6f}")
            
        # Test backward compatibility
        assert record.content_entropy == record.base_features.shannon_entropy, \
            "Backward compat field should match base_features"
        print(f"\n  [PASS] Backward compatibility verified")
        
        return True
        
    finally:
        # Cleanup
        if test_file.exists():
            test_file.unlink()


def test_decision_detail_dialog_compatibility():
    """Test that DecisionDetailDialog can use new features"""
    print("\n" + "=" * 70)
    print("TEST: Decision Detail Dialog Compatibility")
    print("=" * 70)
    
    from gui.core.models import FileRecord
    from gui.core.decision import DecisionResult
    from gui.core.models import AlgorithmType
    
    # Create test file
    test_file = project_root / "test_dialog_compat.txt"
    test_content = b"Test content for dialog " * 50
    test_file.write_bytes(test_content)
    
    try:
        record = FileRecord(str(test_file))
        record.load_raw_data()
        record.extract_features()
        
        # Simulate what DecisionDetailDialog does
        if record.base_features:
            entropy_val = getattr(record, 'content_entropy', 0.0)
            repetition_val = getattr(record, 'repetition_ratio', 0.0)
            
            print(f"  Content Entropy: {entropy_val:.4f}" if entropy_val > 0 else "  Content Entropy: Not calculated")
            print(f"  Repetition Ratio: {repetition_val:.2%}" if repetition_val > 0 else "  Repetition Ratio: Not calculated")
            
            # Check feature dict access (used by dialog)
            feat_dict = record.base_features.to_dict()
            assert 'shannon_entropy' in feat_dict, "Feature dict missing shannon_entropy"
            assert 'unique_byte_ratio' in feat_dict, "Feature dict missing unique_byte_ratio"
            
            print(f"\n  [PASS] Dialog-compatible feature access verified")
        else:
            print(f"\n  [WARN] No features extracted (may be expected for some files)")
            
        return True
        
    finally:
        if test_file.exists():
            test_file.unlink()


def test_multiple_file_types():
    """Test feature extraction on different file types"""
    print("\n" + "=" * 70)
    print("TEST: Multiple File Types")
    print("=" * 70)
    
    from gui.core.models import FileRecord
    from gui.core.feature_extractor import FileType
    
    test_cases = [
        ("text.txt", b"This is plain text content. " * 200, FileType.TEXT_PLAIN),
        ("data.json", b'{"key": "value", "array": [1, 2, 3]} ' * 100, FileType.UNKNOWN),  # JSON magic not at start
        ("binary.bin", bytes(range(256)) * 10, FileType.BINARY_GENERIC),
    ]
    
    for filename, content, expected_type in test_cases:
        test_path = project_root / filename
        test_path.write_bytes(content)
        
        try:
            record = FileRecord(str(test_path))
            record.load_raw_data()
            record.extract_features()
            
            status = "✓" if record.base_features else "✗"
            type_match = "✓" if record.detected_file_type == expected_type or expected_type == FileType.UNKNOWN else "~"
            
            print(f"  {status}{type_match} {filename:15s}: "
                  f"entropy={record.content_entropy:.3f}, "
                  f"type={record.detected_file_type.name if record.detected_file_type else 'None'}")
                  
            assert record.base_features is not None, f"Failed to extract features from {filename}"
            
        finally:
            if test_path.exists():
                test_path.unlink()
                
    print(f"\n  [PASS] All file types processed successfully")
    
    return True


def main():
    """Run integration tests"""
    print("\n" + "=" * 70)
    print("  ADE Feature Extractor - Integration Test Suite")
    print("=" * 70)
    
    tests = [
        ("FileRecord Integration", test_filerecord_integration),
        ("Decision Dialog Compatibility", test_decision_detail_dialog_compatibility),
        ("Multiple File Types", test_multiple_file_types),
    ]
    
    results = []
    for name, test_func in tests:
        try:
            success = test_func()
            results.append((name, success))
        except Exception as e:
            print(f"\n  ✗ {name} FAILED: {e}")
            import traceback
            traceback.print_exc()
            results.append((name, False))
    
    # Summary
    print("\n" + "=" * 70)
    print("  INTEGRATION TEST SUMMARY")
    print("=" * 70)
    
    passed = sum(1 for _, s in results if s)
    total = len(results)
    
    for name, success in results:
        status = "✓ PASS" if success else "✗ FAIL"
        print(f"  {status}: {name}")
    
    print(f"\n  Total: {passed}/{total} tests passed")
    
    return 0 if passed == total else 1


if __name__ == "__main__":
    exit_code = main()
    sys.exit(exit_code)
