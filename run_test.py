#!/usr/bin/env python3
"""Simple test runner for Stage2 parameter regression"""
import sys
import os
from pathlib import Path

os.environ['PYTHONIOENCODING'] = 'utf-8'

project_root = Path(__file__).resolve().parent
src_root = project_root / 'src'
sys.path.insert(0, str(src_root))
sys.path.insert(0, str(project_root))

from tests.test_stage2_param_regression import main

if __name__ == "__main__":
    sys.exit(main())
