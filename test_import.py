#!/usr/bin/env python3
"""Test if main_window.py structure is fixed"""
import sys
from pathlib import Path

project_root = Path(__file__).resolve().parent
src_root = project_root / 'src'
sys.path.insert(0, str(src_root))

try:
    from gui.widgets.main_window import MainWindow, DecisionDetailDialog
    print('✅ Import successful!')
    print(f'MainWindow has _on_webpage_heatmap: {hasattr(MainWindow, "_on_webpage_heatmap")}')
    print(f'MainWindow has _on_view_decision_detail: {hasattr(MainWindow, "_on_view_decision_detail")}')
    print(f'MainWindow has _on_folder_summary: {hasattr(MainWindow, "_on_folder_summary")}')
    print(f'DecisionDetailDialog is defined: {DecisionDetailDialog is not None}')
    
    # Check that methods are actually in MainWindow class (not module-level)
    import inspect
    members = [name for name, _ in inspect.getmembers(MainWindow) if name.startswith('_on_')]
    print(f'\nMainWindow methods starting with _on_:')
    for m in sorted(members):
        print(f'  - {m}')
    
except Exception as e:
    print(f'❌ Import failed: {e}')
    import traceback
    traceback.print_exc()
    sys.exit(1)
