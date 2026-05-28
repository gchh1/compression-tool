import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'build_tmp', 'src', 'bindings', 'pybind_new'))
os.environ['ADE_CRASH_LOG_PATH'] = os.path.join(os.path.dirname(__file__), 'ade_lzdp_test.log')

import core_engine_new as ce

test_data = b"Hello World! " * 1000
print(f"Test data: {len(test_data)} bytes")

eng = ce

params = eng.LzdpWholeFileParams()
params.search_size = 4096
params.lookahead_size = 256
params.min_match = 0
params.dp_top = 3
params.use_flag_encoding = False
params.match_engine = 0

print(f"Params: search={params.search_size} look={params.lookahead_size} min={params.min_match} dp_top={params.dp_top} flag={params.use_flag_encoding} engine={params.match_engine}")
print("Calling pipeline_compress with LZDP...")
sys.stdout.flush()

result = eng.pipeline_compress(test_data, [eng.AlgorithmID.LZDP], params, None, None, None, 0)

print(f"Result: success={result.success} orig_size={result.original_size} comp_size={result.compressed_size}")
print("OK - LZDP pipeline_compress works!")