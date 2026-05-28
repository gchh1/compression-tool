import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'build_tmp', 'src', 'bindings', 'pybind_new'))
os.environ['ADE_CRASH_LOG_PATH'] = os.path.join(os.path.dirname(__file__), 'ade_core_test.log')

import core_engine_new as ce

ade = ce.ADE()
print(f"ADE created: {type(ade).__name__}")

result = ade.analyze_file(os.path.join(os.path.dirname(__file__), 'resources', 'wow.html'))
print(f"result.algorithm = {result.algorithm} (type={type(result.algorithm).__name__}, value={int(result.algorithm)})")
print(f"result.confidence = {result.confidence}")
print(f"result.file_type = {result.file_type}")
print(f"result.reason = {result.reason}")

caid = result.algorithm
core = ce.CoreAlgorithmID
alg = ce.AlgorithmID

print(f"\nCoreAlgorithmID enum values:")
for name in ['NONE', 'DEFLATE', 'LZSS', 'LZDP', 'DPFLATE', 'BROTLI', 'ZSTD']:
    cv = getattr(core, name)
    av = getattr(alg, name)
    print(f"  CoreAlgorithmID.{name} = {int(cv)}  |  AlgorithmID.{name} = {int(av)}")

print(f"\nMapping test:")
for name in ['DEFLATE', 'LZSS', 'LZDP', 'DPFLATE', 'BROTLI', 'ZSTD', 'NONE']:
    cv = getattr(core, name)
    if caid == cv:
        print(f"  result.algorithm == CoreAlgorithmID.{name} -> algorithm matches!")
        break
else:
    print(f"  result.algorithm == {caid} -> no match found")