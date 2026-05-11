import sys, os
from pathlib import Path

project_root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(project_root / 'build' / 'src' / 'bindings' / 'pybind'))
sys.path.insert(0, str(project_root / 'src'))
os.chdir(str(project_root))

import core_engine
print(f"core_engine loaded: {core_engine.__file__}")
print(f"has ADE: {hasattr(core_engine, 'ADE')}")
print(f"has ParameterOptimizer: {hasattr(core_engine, 'ParameterOptimizer')}")

from gui.ade.engine import (
    DecisionEngine,
    RandomForestStrategy,
    NeuralNetworkStrategy,
    StrategyDispatcher,
    DecisionMode,
)
from gui.models import FileRecord

print("\n=== Strategy.py Integration Test ===\n")

engine = DecisionEngine.get()
print(f"DecisionEngine ready: {engine.is_ready}")
print(f"Mode: {engine.get_mode().name}")
info = engine.get_info()
for k, v in info.items():
    print(f"  {k}: {v}")

test_files = [
    str(project_root / "src" / "gui" / "main.py"),
    str(project_root / "CMakeLists.txt"),
    str(project_root / "run.bat"),
]

print("\n--- DecisionEngine.decide() ---")
for fp in test_files:
    if not os.path.exists(fp):
        continue
    rec = FileRecord(fp)
    result = engine.decide(rec)
    print(f"  [{os.path.basename(fp)}]")
    print(f"    algo={result.algorithm.value}, conf={result.confidence:.2f}")
    print(f"    reason={result.reason[:80]}")
    print(f"    mode={result.mode_used}, extract={result.extraction_time_ms:.2f}ms")
    if result.params:
        print(f"    params={result.params}")

print("\n--- RandomForestStrategy ---")
rf = RandomForestStrategy()
rec = FileRecord(test_files[0])
r = rf.decide(rec)
print(f"  algo={r.algorithm.value}, mode={r.mode_used}, conf={r.confidence:.2f}")

print("\n--- NeuralNetworkStrategy ---")
nn = NeuralNetworkStrategy()
rec2 = FileRecord(test_files[0])
r2 = nn.decide(rec2)
print(f"  has_torch={nn._has_torch}")
print(f"  algo={r2.algorithm.value}, mode={r2.mode_used}, reason={r2.reason}")

print("\n--- StrategyDispatcher ---")
disp = StrategyDispatcher()
rec3 = FileRecord(test_files[0])
rec3.load_raw_data()
rec3_out, dec = disp.dispatch(rec3)
print(f"  dispatched: algo={dec.algorithm.value}, record.algo={rec3_out.algorithm.value}")

print("\n--- Training Data Collection ---")
sample = engine.collect_training_sample(rec3, dec)
print(f"  sample_id={sample.record_id}")
print(f"  features={sample.features}")
print(f"  algorithm={sample.algorithm_used}")
print(f"  total_samples={engine.sample_count}")

print("\n--- Mode Switching ---")
for mode in [DecisionMode.RULE_BASED, DecisionMode.ML_HYBRID, DecisionMode.ML_ONLY]:
    engine.set_mode(mode)
    r = engine.decide(FileRecord(test_files[0]))
    print(f"  {mode.name}: algo={r.algorithm.value}, conf={r.confidence:.2f}")

print("\n=== Strategy Test Complete ===")
