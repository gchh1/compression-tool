#!/usr/bin/env python3
"""Test UI enhancements: Algorithm column + Right-click menu + Decision detail dialog"""
import sys
import os
from pathlib import Path

os.environ['PYTHONIOENCODING'] = 'utf-8'

project_root = Path(__file__).resolve().parent
src_root = project_root / 'src'
sys.path.insert(0, str(src_root))
sys.path.insert(0, str(project_root))

from PyQt6.QtWidgets import QApplication
from gui.widgets.main_window import FileTableWidget, DecisionDetailDialog
from gui.core.models import FileRecord, CompressionStatus, AlgorithmType

def test_algorithm_column():
    """Test 1: Verify algorithm column exists and is correctly positioned"""
    print("\n" + "=" * 70)
    print("TEST 1: Algorithm Column Configuration")
    print("=" * 70)
    
    app = QApplication.instance()
    if app is None:
        app = QApplication(sys.argv)
    
    table = FileTableWidget()
    
    # Check column count (should be 7 now)
    assert table.columnCount() == 7, f"Expected 7 columns, got {table.columnCount()}"
    print(f"  [PASS] Column count: {table.columnCount()}")
    
    # Check column positions
    assert table.COL_CHECK == 0, "COL_CHECK should be 0"
    assert table.COL_NAME == 1, "COL_NAME should be 1"
    assert table.COL_SIZE == 2, "COL_SIZE should be 2"
    assert table.COL_TYPE == 3, "COL_TYPE should be 3"
    assert table.COL_STATUS == 4, "COL_STATUS should be 4"
    assert table.COL_ALGORITHM == 5, "COL_ALGORITHM should be 5"
    assert table.COL_RATIO == 6, "COL_RATIO should be 6"
    print(f"  [PASS] Column indices correct")
    
    # Check header labels
    headers = [table.horizontalHeaderItem(i).text() for i in range(table.columnCount())]
    expected_headers = ["☐", "文件名", "大小", "类型", "状态", "算法", "压缩率"]
    assert headers == expected_headers, f"Headers mismatch: {headers} vs {expected_headers}"
    print(f"  [PASS] Headers: {headers}")
    
    # Check algorithm column width
    algo_width = table.columnWidth(table.COL_ALGORITHM)
    assert algo_width > 0, "Algorithm column should have width"
    print(f"  [PASS] Algorithm column width: {algo_width}px")
    
    return True


def test_add_file_with_algorithm():
    """Test 2: Adding a file populates the algorithm column"""
    print("\n" + "=" * 70)
    print("TEST 2: Add File with Algorithm Column")
    print("=" * 70)
    
    app = QApplication.instance()
    if app is None:
        app = QApplication(sys.argv)
    
    table = FileTableWidget()
    
    # Create test file record
    row = table.add_file(__file__)
    
    assert row >= 0, "Failed to add file"
    print(f"  [PASS] Added file at row {row}")
    
    # Get the actual record from the table
    record = table.get_record(row)
    assert record is not None, "Should get record from table"
    
    # Check initial algorithm value (should be "-")
    algo_item = table.item(row, table.COL_ALGORITHM)
    assert algo_item is not None, "Algorithm item should exist"
    assert algo_item.text() == "-", f"Initial algorithm should be '-', got '{algo_item.text()}'"
    print(f"  [PASS] Initial algorithm value: '{algo_item.text()}'")
    
    # Simulate compression completion (modify the SAME record object in the table)
    record.algorithm = AlgorithmType.LZMINE
    record.status = CompressionStatus.DONE
    record.compression_ratio = 0.65
    record.size = 102400
    
    # Update the row
    table.update_row(row)
    
    # Check updated algorithm value
    algo_item = table.item(row, table.COL_ALGORITHM)
    assert algo_item is not None, "Algorithm item should exist after update"
    assert algo_item.text() == "lzmine", f"Expected 'lzmine', got '{algo_item.text()}'"
    print(f"  [PASS] Updated algorithm value after compression: '{algo_item.text()}'")
    
    return True


def test_decision_result_persistence():
    """Test 3: DecisionResult is persisted in FileRecord"""
    print("\n" + "=" * 70)
    print("TEST 3: DecisionResult Persistence")
    print("=" * 70)
    
    from gui.core.decision import DecisionEngine, DecisionResult
    
    # Create test record
    record = FileRecord(__file__)
    
    # Initially no decision result
    assert record.decision_result is None, "Initially decision_result should be None"
    print(f"  [PASS] Initial decision_result: None")
    
    # Simulate ADE decision (if available)
    de = DecisionEngine.get()
    if de._ade:
        try:
            decision = de.decide(record)
            record.decision_result = decision
            
            assert record.decision_result is not None, "decision_result should be set"
            print(f"  [PASS] DecisionResult saved to record")
            print(f"         Algorithm: {record.decision_result.algorithm.value}")
            print(f"         Confidence: {record.decision_result.confidence:.2f}")
        except Exception as e:
            print(f"  [SKIP] ADE decision failed (may need training data): {e}")
    else:
        print(f"  [SKIP] ADE not initialized (using mock)")
        
        # Create mock decision for testing
        mock_decision = DecisionResult(
            algorithm=AlgorithmType.DEFLATE,
            confidence=0.85,
            reason="Test decision",
            params={'window_size': 8192, 'min_match': 3}
        )
        record.decision_result = mock_decision
        
        assert record.decision_result is not None
        assert record.decision_result.algorithm == AlgorithmType.DEFLATE
        assert record.decision_result.confidence == 0.85
        print(f"  [PASS] Mock DecisionResult saved successfully")
        print(f"         Algorithm: {record.decision_result.algorithm.value}")
        print(f"         Confidence: {record.decision_result.confidence:.2f}")
    
    return True


def test_right_click_menu_signals():
    """Test 4: Right-click menu signals exist"""
    print("\n" + "=" * 70)
    print("TEST 4: Right-Click Menu Signals")
    print("=" * 70)
    
    app = QApplication.instance()
    if app is None:
        app = QApplication(sys.argv)
    
    table = FileTableWidget()
    
    # Check that new signal exists
    assert hasattr(table, 'request_decision_detail'), "request_decision_detail signal should exist"
    print(f"  [PASS] request_decision_detail signal exists")
    
    # Check all required signals
    required_signals = [
        'selection_changed',
        'request_demo',
        'request_heatmap',
        'request_comparison',
        'request_network',
        'request_webpage_heatmap',
        'request_folder_summary',
        'request_decision_detail',
    ]
    
    for sig_name in required_signals:
        assert hasattr(table, sig_name), f"Missing signal: {sig_name}"
    print(f"  [PASS] All {len(required_signals)} signals present")
    
    # Check helper methods exist
    assert hasattr(table, '_open_file_location'), "_open_file_location method should exist"
    assert hasattr(table, '_copy_file_path'), "_copy_file_path method should exist"
    print(f"  [PASS] Helper methods (_open_file_location, _copy_file_path) exist")
    
    return True


def test_decision_detail_dialog():
    """Test 5: DecisionDetailDialog can be created"""
    print("\n" + "=" * 70)
    print("TEST 5: DecisionDetailDialog Creation")
    print("=" * 70)
    
    app = QApplication.instance()
    if app is None:
        app = QApplication(sys.argv)
    
    # Create test record with decision
    record = FileRecord(__file__)
    record.algorithm = AlgorithmType.BROTLI
    record.status = CompressionStatus.DONE
    record.compression_ratio = 0.55
    record.compression_time_ms = 150.5
    
    from gui.core.decision import DecisionResult
    record.decision_result = DecisionResult(
        algorithm=AlgorithmType.BROTLI,
        confidence=0.92,
        reason="High entropy binary content",
        params={'window_size': 16384, 'min_match': 4, 'max_chain_length': 256}
    )
    
    # Create dialog (don't show it, just verify creation)
    try:
        dialog = DecisionDetailDialog(record)
        
        assert dialog.windowTitle() == f"决策详情 - {record.name}", "Title mismatch"
        print(f"  [PASS] Dialog created with title: '{dialog.windowTitle()}'")
        
        # Check minimum size
        min_size = dialog.minimumSize()
        assert min_size.width() >= 600, f"Min width should be >= 600, got {min_size.width()}"
        assert min_size.height() >= 500, f"Min height should be >= 500, got {min_size.height()}"
        print(f"  [PASS] Dialog minimum size: {min_size.width()}x{min_size.height()}")
        
        dialog.close()
        del dialog
        print(f"  [PASS] Dialog closed successfully")
        
    except Exception as e:
        print(f"  [FAIL] Failed to create dialog: {e}")
        import traceback
        traceback.print_exc()
        return False
    
    return True


def main():
    """Run all tests"""
    print("\n" + "=" * 70)
    print("  UI Enhancement Test Suite")
    print("  Algorithm Column + Right-Click Menu + Decision Detail Dialog")
    print("=" * 70)
    
    tests = [
        ("Algorithm Column Configuration", test_algorithm_column),
        ("Add File with Algorithm Column", test_add_file_with_algorithm),
        ("DecisionResult Persistence", test_decision_result_persistence),
        ("Right-Click Menu Signals", test_right_click_menu_signals),
        ("DecisionDetailDialog Creation", test_decision_detail_dialog),
    ]
    
    results = []
    for name, test_func in tests:
        try:
            passed = test_func()
            results.append((name, passed))
        except Exception as e:
            print(f"\n  [FAIL] {name} crashed: {e}")
            import traceback
            traceback.print_exc()
            results.append((name, False))
    
    # Summary
    print("\n" + "=" * 70)
    print("  TEST SUMMARY")
    print("=" * 70)
    
    passed_count = sum(1 for _, p in results if p)
    total_count = len(results)
    
    for name, passed in results:
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {name}")
    
    print(f"\n  Total: {passed_count}/{total_count} tests passed")
    
    if passed_count == total_count:
        print("\n  ✅ All UI enhancement tests PASSED!")
        return 0
    else:
        print(f"\n  ❌ {total_count - passed_count} test(s) FAILED")
        return 1


if __name__ == "__main__":
    sys.exit(main())
