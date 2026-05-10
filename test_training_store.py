#!/usr/bin/env python3
"""Test ADE Training Data Collection Framework (V3)"""
import sys
import os
import json
import tempfile
from pathlib import Path

os.environ['PYTHONIOENCODING'] = 'utf-8'

project_root = Path(__file__).resolve().parent
src_root = project_root / 'src'
sys.path.insert(0, str(src_root))
sys.path.insert(0, str(project_root))


def test_sample_creation():
    """Test 1: TrainingSampleV3 creation and validation"""
    print("\n" + "=" * 70)
    print("TEST 1: Sample Creation & Validation")
    print("=" * 70)
    
    from gui.core.training_store import TrainingSampleV3, FEATURE_DIM
    
    sample = TrainingSampleV3(
        file_name="test.txt",
        file_size=1024,
        file_extension=".txt",
        detected_type="TEXT_PLAIN",
        features_vector=[0.5] * FEATURE_DIM,
        algorithm_used="deflate",
        algorithm_id=1,
        compression_ratio=0.35,
        compression_time_ms=12.5,
        success=True,
    )
    
    assert sample.sample_id, "Should auto-generate sample_id"
    assert sample.timestamp > 0, "Should auto-generate timestamp"
    assert sample.data_version == "3.0", "Should use v3.0"
    assert len(sample.features_vector) == FEATURE_DIM, f"Expected {FEATURE_DIM} features"
    
    is_valid = sample.validate()
    assert is_valid, f"Valid sample should pass validation: {sample.validation_message}"
    
    print(f"  [PASS] Sample created: id={sample.sample_id}, version={sample.data_version}")
    print(f"  [INFO] Features: {len(sample.features_vector)} dims")
    print(f"  [INFO] Validation: {sample.validation_message}")
    
    return True


def test_invalid_sample():
    """Test 2: Invalid sample detection"""
    print("\n" + "=" * 70)
    print("TEST 2: Invalid Sample Detection")
    print("=" * 70)
    
    from gui.core.training_store import TrainingSampleV3
    
    # Wrong feature dimension
    bad_sample = TrainingSampleV3(
        file_name="bad.bin",
        file_size=100,
        features_vector=[0.5] * 10,  # Wrong: 10 instead of 20
        algorithm_used="deflate",
        compression_ratio=0.5,
    )
    
    is_valid = bad_sample.validate()
    assert not is_valid, "Invalid sample should fail validation"
    assert "10 dims" in bad_sample.validation_message, "Should report dimension mismatch"
    
    print(f"  [PASS] Invalid sample correctly rejected: {bad_sample.validation_message}")
    
    # Zero file size
    bad_sample2 = TrainingSampleV3(
        file_name="empty.dat",
        file_size=0,
        features_vector=[0.0] * 20,
        algorithm_used="deflate",
        compression_ratio=1.0,
    )
    bad_sample2.validate()
    assert not bad_sample2.is_valid, "Zero file size should be invalid"
    
    print(f"  [PASS] Zero-size file correctly rejected: {bad_sample2.validation_message}")
    
    return True


def test_jsonl_serialization():
    """Test 3: JSONL serialization round-trip"""
    print("\n" + "=" * 70)
    print("TEST 3: JSONL Serialization")
    print("=" * 70)
    
    from gui.core.training_store import TrainingSampleV3, FEATURE_DIM
    
    original = TrainingSampleV3(
        file_name="serialize_test.txt",
        file_size=2048,
        file_extension=".txt",
        detected_type="TEXT_PLAIN",
        resource_type="TEXT",
        features_vector=[float(i) / 20.0 for i in range(FEATURE_DIM)],
        features_dict={"shannon_entropy": 4.5, "unique_byte_ratio": 0.15},
        algorithm_used="lzmine",
        algorithm_id=3,
        compression_ratio=0.42,
        compression_time_ms=8.3,
        success=True,
    )
    
    json_line = original.to_json_line()
    restored = TrainingSampleV3.from_json_line(json_line)
    
    assert restored is not None, "Should parse successfully"
    assert restored.file_name == original.file_name
    assert restored.file_size == original.file_size
    assert len(restored.features_vector) == FEATURE_DIM
    
    max_diff = max(abs(a - b) for a, b in zip(restored.features_vector, original.features_vector))
    assert max_diff < 1e-10, f"Serialization error: max diff {max_diff}"
    
    print(f"  [PASS] JSONL round-trip successful")
    print(f"  [INFO] Line length: {len(json_line)} chars")
    
    return True


def test_from_file_record():
    """Test 4: Create sample from FileRecord"""
    print("\n" + "=" * 70)
    print("TEST 4: Sample from FileRecord")
    print("=" * 70)
    
    from gui.core.models import FileRecord
    from gui.core.training_store import TrainingSampleV3
    
    test_file = project_root / "test_train_temp.txt"
    test_content = b"Training data test content. " * 100
    test_file.write_bytes(test_content)
    
    try:
        record = FileRecord(str(test_file))
        record.load_raw_data()
        record.extract_features()
        
        # Simulate compression result
        record.compression_ratio = 0.35
        record.compression_time_ms = 15.0
        record.compressed_data = b"compressed" * 50
        from gui.core.models import CompressionStatus
        record.status = CompressionStatus.DONE
        
        sample = TrainingSampleV3.from_file_record(record)
        
        assert sample.file_name == "test_train_temp.txt"
        assert sample.file_size > 0
        assert sample.file_extension == ".txt"
        assert len(sample.features_vector) == 20, f"Expected 20 features, got {len(sample.features_vector)}"
        assert sample.compression_ratio == 0.35
        assert sample.success is True
        assert sample.decision_hint in ("COMPRESS", "SKIP", "RECOMPRESS_ONLY")
        
        is_valid = sample.validate()
        assert is_valid, f"Sample should be valid: {sample.validation_message}"
        
        print(f"  [PASS] Sample created from FileRecord")
        print(f"  [INFO] File: {sample.file_name}, Size: {sample.file_size}")
        print(f"  [INFO] Decision: {sample.decision_hint}")
        print(f"  [INFO] Features[0:5]: {[f'{v:.4f}' for v in sample.features_vector[:5]]}")
        print(f"  [INFO] Valid: {sample.validation_message}")
        
        return True
        
    finally:
        if test_file.exists():
            test_file.unlink()


def test_training_store():
    """Test 5: TrainingDataStore CRUD operations"""
    print("\n" + "=" * 70)
    print("TEST 5: TrainingDataStore CRUD")
    print("=" * 70)
    
    from gui.core.training_store import TrainingDataStore, TrainingSampleV3, FEATURE_DIM
    
    with tempfile.TemporaryDirectory() as tmpdir:
        store = TrainingDataStore(base_dir=tmpdir)
        
        # Add valid samples
        for i in range(5):
            sample = TrainingSampleV3(
                file_name=f"test_{i}.txt",
                file_size=1024 * (i + 1),
                file_extension=".txt",
                detected_type="TEXT_PLAIN",
                features_vector=[0.1 * i] * FEATURE_DIM,
                algorithm_used="deflate" if i % 2 == 0 else "lzmine",
                algorithm_id=1 if i % 2 == 0 else 3,
                compression_ratio=0.3 + 0.05 * i,
                success=True,
            )
            result = store.add_sample(sample)
            assert result, f"Sample {i} should be added"
        
        assert store.sample_count == 5, f"Expected 5 samples, got {store.sample_count}"
        
        # Add invalid sample
        bad_sample = TrainingSampleV3(
            file_name="bad.bin",
            file_size=0,
            features_vector=[0.0] * FEATURE_DIM,
            algorithm_used="deflate",
            compression_ratio=0.5,
        )
        bad_sample.validate()
        result = store.add_sample(bad_sample)
        assert not result, "Invalid sample should be rejected"
        
        print(f"  [PASS] Added 5 valid samples, rejected 1 invalid")
        
        # Save
        save_result = store.save(incremental=True)
        assert save_result, "Save should succeed"
        assert store.data_path.exists(), "Data file should exist"
        
        print(f"  [PASS] Saved to {store.data_path}")
        
        # Load
        loaded = store.load(validate=True)
        assert len(loaded) == 5, f"Expected 5 loaded samples, got {len(loaded)}"
        
        print(f"  [PASS] Loaded {len(loaded)} samples from disk")
        
        # Stats
        stats = store.get_stats()
        assert stats['valid_samples'] == 5
        assert stats['total_samples'] == 5
        
        print(f"  [PASS] Stats: {stats['valid_samples']} valid, {stats['invalid_samples']} invalid")
        
        # Feature matrix
        matrix = store.get_feature_matrix(loaded)
        assert len(matrix) == 5
        assert len(matrix[0]) == FEATURE_DIM
        
        print(f"  [PASS] Feature matrix: {len(matrix)} x {len(matrix[0])}")
        
        # Label vector
        labels = store.get_label_vector(loaded)
        assert len(labels) == 5
        
        print(f"  [PASS] Label vector: {labels}")
        
    return True


def test_incremental_save():
    """Test 6: Incremental save (append mode)"""
    print("\n" + "=" * 70)
    print("TEST 6: Incremental Save")
    print("=" * 70)
    
    from gui.core.training_store import TrainingDataStore, TrainingSampleV3, FEATURE_DIM
    
    with tempfile.TemporaryDirectory() as tmpdir:
        store = TrainingDataStore(base_dir=tmpdir)
        
        # First batch
        for i in range(3):
            sample = TrainingSampleV3(
                file_name=f"batch1_{i}.txt",
                file_size=1024,
                features_vector=[0.1] * FEATURE_DIM,
                algorithm_used="deflate",
                algorithm_id=1,
                compression_ratio=0.4,
                success=True,
            )
            store.add_sample(sample)
        
        store.save(incremental=True)
        
        # Second batch
        store2 = TrainingDataStore(base_dir=tmpdir)
        for i in range(2):
            sample = TrainingSampleV3(
                file_name=f"batch2_{i}.txt",
                file_size=2048,
                features_vector=[0.2] * FEATURE_DIM,
                algorithm_used="lzmine",
                algorithm_id=3,
                compression_ratio=0.3,
                success=True,
            )
            store2.add_sample(sample)
        
        store2.save(incremental=True)
        
        # Load all
        store3 = TrainingDataStore(base_dir=tmpdir)
        all_samples = store3.load(validate=True)
        
        assert len(all_samples) == 5, f"Expected 5 total samples, got {len(all_samples)}"
        
        batch1_count = sum(1 for s in all_samples if s.file_name.startswith("batch1"))
        batch2_count = sum(1 for s in all_samples if s.file_name.startswith("batch2"))
        
        assert batch1_count == 3, f"Expected 3 batch1, got {batch1_count}"
        assert batch2_count == 2, f"Expected 2 batch2, got {batch2_count}"
        
        print(f"  [PASS] Incremental save: batch1={batch1_count}, batch2={batch2_count}, total={len(all_samples)}")
        
    return True


def test_csv_export():
    """Test 7: CSV export"""
    print("\n" + "=" * 70)
    print("TEST 7: CSV Export")
    print("=" * 70)
    
    from gui.core.training_store import TrainingDataStore, TrainingSampleV3, FEATURE_DIM
    
    with tempfile.TemporaryDirectory() as tmpdir:
        store = TrainingDataStore(base_dir=tmpdir)
        
        for i in range(3):
            sample = TrainingSampleV3(
                file_name=f"export_{i}.txt",
                file_size=1024 * (i + 1),
                file_extension=".txt",
                detected_type="TEXT_PLAIN",
                features_vector=[0.1 * (j + i) for j in range(FEATURE_DIM)],
                algorithm_used="deflate",
                algorithm_id=1,
                compression_ratio=0.3 + 0.05 * i,
                success=True,
            )
            store.add_sample(sample)
        
        store.save(incremental=False)
        
        csv_path = Path(tmpdir) / "export_test.csv"
        count = store.export_csv(csv_path)
        
        assert count == 3, f"Expected 3 exported rows, got {count}"
        assert csv_path.exists(), "CSV file should exist"
        
        # Verify CSV content
        with open(csv_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        
        assert len(lines) == 4, f"Expected 4 lines (1 header + 3 data), got {len(lines)}"
        
        headers = lines[0].strip().split(',')
        assert 'shannon_entropy' in headers, "Should have feature columns"
        assert 'algorithm_used' in headers, "Should have algorithm column"
        
        print(f"  [PASS] Exported {count} samples to CSV")
        print(f"  [INFO] CSV: {len(lines)} lines, {len(headers)} columns")
        
    return True


def test_decision_engine_integration():
    """Test 8: DecisionEngine integration with V3 store"""
    print("\n" + "=" * 70)
    print("TEST 8: DecisionEngine + V3 Store Integration")
    print("=" * 70)
    
    from gui.core.models import FileRecord, CompressionStatus
    from gui.core.decision import DecisionEngine, DecisionResult, DecisionMode
    from gui.core.training_store import get_training_store
    
    test_file = project_root / "test_de_integration.txt"
    test_content = b"Decision engine integration test. " * 100
    test_file.write_bytes(test_content)
    
    try:
        record = FileRecord(str(test_file))
        record.load_raw_data()
        record.extract_features()
        record.compression_ratio = 0.38
        record.compression_time_ms = 10.0
        record.compressed_data = b"compressed_data"
        record.status = CompressionStatus.DONE
        
        engine = DecisionEngine()
        
        decision = DecisionResult(
            algorithm=record.algorithm,
            confidence=0.85,
            reason="Test decision",
            params={'window_size': 32768},
        )
        
        sample = engine.collect_training_sample(record, decision)
        
        assert sample is not None, "Should return a TrainingSample"
        assert sample.features.get('data_version') == '3.0', "Should use v3.0 features"
        assert sample.features.get('feature_dim') == 20, "Should have 20 features"
        
        print(f"  [PASS] DecisionEngine collected V3 sample")
        print(f"  [INFO] Feature version: {sample.features.get('data_version')}")
        print(f"  [INFO] Feature dim: {sample.features.get('feature_dim')}")
        print(f"  [INFO] Feature keys count: {len(sample.features)}")
        
        return True
        
    finally:
        if test_file.exists():
            test_file.unlink()


def main():
    """Run all tests"""
    print("\n" + "=" * 70)
    print("  ADE Training Data Collection Framework - Test Suite")
    print("  Version: 3.0 (20-dim BaseFeatures)")
    print("=" * 70)
    
    tests = [
        ("Sample Creation", test_sample_creation),
        ("Invalid Detection", test_invalid_sample),
        ("JSONL Serialization", test_jsonl_serialization),
        ("From FileRecord", test_from_file_record),
        ("Store CRUD", test_training_store),
        ("Incremental Save", test_incremental_save),
        ("CSV Export", test_csv_export),
        ("DecisionEngine Integration", test_decision_engine_integration),
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
    print("  TEST SUMMARY")
    print("=" * 70)
    
    passed = sum(1 for _, s in results if s)
    total = len(results)
    
    for name, success in results:
        status = "✓ PASS" if success else "✗ FAIL"
        print(f"  {status}: {name}")
    
    print(f"\n  Total: {passed}/{total} tests passed")
    
    if passed == total:
        print(f"\n  ALL TESTS PASSED!")
        return 0
    else:
        print(f"\n  {total - passed} test(s) failed")
        return 1


if __name__ == "__main__":
    exit_code = main()
    sys.exit(exit_code)
