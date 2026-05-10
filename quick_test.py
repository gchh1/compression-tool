#!/usr/bin/env python3
"""Quick compression test"""
import sys, os
from pathlib import Path

os.environ['PYTHONIOENCODING'] = 'utf-8'
project_root = Path(__file__).resolve().parent
sys.path.insert(0, str(project_root / 'src'))
sys.path.insert(0, str(project_root))

from gui.core.models import AlgorithmType
from gui.core.engine import CompressionEngine

engine = CompressionEngine()
print(f"Engine available: {engine.available}")

if engine.available:
    test_data = b"Hello World! " * 10000
    
    for algo in [AlgorithmType.DEFLATE, AlgorithmType.LZMINE, AlgorithmType.BROTLI, AlgorithmType.ZSTD]:
        try:
            cr = engine.compress(test_data, algo)
            if cr.success:
                ratio = len(cr.data) / len(test_data)
                print(f"{algo.value:10s}: {len(test_data):>8,} -> {len(cr.data):>8,} (ratio: {ratio:.4f})")
            else:
                print(f"{algo.value:10s}: FAILED - {cr.error_message}")
        except Exception as e:
            print(f"{algo.value:10s}: ERROR - {e}")
