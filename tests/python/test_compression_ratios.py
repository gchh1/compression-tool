#!/usr/bin/env python3
"""Test compression ratios for all algorithms to identify issues - outputs to file"""
import sys
import os
from pathlib import Path
import time
import random

os.environ['PYTHONIOENCODING'] = 'utf-8'

project_root = Path(__file__).resolve().parent.parent.parent
src_root = project_root / 'src'
sys.path.insert(0, str(src_root))
sys.path.insert(0, str(project_root))

OUTPUT_FILE = project_root / "reports" / "benchmarks" / "compression_results.txt"


def generate_test_data():
    """Generate various types of test data"""
    datasets = {}
    
    # 1. Highly repetitive text (should compress well)
    text1 = "Hello World! " * 10000
    datasets['repetitive_text'] = text1.encode('utf-8')
    
    # 2. Random data (should not compress well)
    random.seed(42)
    random_data = bytes(random.randint(0, 255) for _ in range(100000))
    datasets['random_binary'] = random_data
    
    # 3. English text (moderate compression)
    english_text = """
    The quick brown fox jumps over the lazy dog. This is a sample text that contains
    common English words and phrases. Compression algorithms typically work well on natural
    language text because of the statistical properties of human languages. The frequency
    distribution of letters and words follows predictable patterns that can be exploited
    for compression purposes.
    """ * 500
    datasets['english_text'] = english_text.encode('utf-8')
    
    # 4. JSON-like structured data
    json_data = str([{"id": i, "name": f"item_{i}", "value": i * 1.5} for i in range(1000)]).encode()
    datasets['json_data'] = json_data
    
    # 5. Code-like data (Python source)
    code_data = '''
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n-1) + fibonacci(n-2)

class Compressor:
    def __init__(self):
        self.data = []
    
    def compress(self, input_data):
        result = []
        for byte in input_data:
            result.append(byte ^ 0xFF)
        return bytes(result)
''' * 200
    datasets['code_data'] = code_data.encode('utf-8')
    
    return datasets


def main():
    """Run comprehensive compression tests"""
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    lines = []
    lines.append("=" * 80)
    lines.append("  Compression Algorithm Performance Test")
    lines.append("=" * 80)
    lines.append("")
    
    from gui.models import AlgorithmType
    from gui.engine.compressor import CompressionEngine
    
    # Initialize engine
    try:
        engine = CompressionEngine()
        if not engine.available:
            raise RuntimeError("C++ core_engine not available")
        lines.append("[OK] Engine initialized successfully")
    except Exception as e:
        lines.append(f"[FAIL] Engine initialization failed: {e}")
        import traceback
        traceback.print_exc()
        return None
    
    lines.append("")
    
    # Generate test data
    datasets = generate_test_data()
    lines.append(f"[DATA] Generated {len(datasets)} test datasets:")
    for name, data in datasets.items():
        lines.append(f"  - {name}: {len(data):,} bytes ({len(data)/1024:.1f} KB)")
    lines.append("")
    
    # Algorithms to test
    algorithms = [
        (AlgorithmType.DEFLATE, "DEFLATE"),
        (AlgorithmType.LZSS, "LZSS"),
        (AlgorithmType.LZDP, "LZDP"),
        (AlgorithmType.DPFLATE, "DPFlate"),
        (AlgorithmType.BROTLI, "Brotli"),
        (AlgorithmType.ZSTD, "Zstd"),
    ]
    
    # Test each algorithm on each dataset
    results = {}
    
    for algo_type, algo_name in algorithms:
        results[algo_name] = {}
        lines.append("=" * 80)
        lines.append(f"Testing: {algo_name}")
        lines.append("=" * 80)
        
        for data_name, data in datasets.items():
            try:
                start_time = time.time()
                cr = engine.compress(data, algo_type)
                elapsed = (time.time() - start_time) * 1000
                
                if cr.success:
                    original_size = len(data)
                    compressed_size = len(cr.data) if hasattr(cr, 'data') else cr.compressed_size
                    ratio = compressed_size / original_size if original_size > 0 else 1.0
                    
                    result = {
                        'original_size': original_size,
                        'compressed_size': compressed_size,
                        'ratio': ratio,
                        'time_ms': elapsed,
                        'success': True
                    }
                    
                    savings = (1 - ratio) * 100
                    line = f"  {data_name:20s}: {original_size:>8,} -> {compressed_size:>8,} " \
                           f"(ratio: {ratio:.4f}, saved: {savings:+.1f}%, time: {elapsed:.1f}ms)"
                    lines.append(line)
                else:
                    result = {
                        'original_size': len(data),
                        'compressed_size': 0,
                        'ratio': 1.0,
                        'time_ms': 0,
                        'success': False,
                        'error': getattr(cr, 'error_message', 'Unknown error')
                    }
                    lines.append(f"  {data_name:20s}: [FAILED] - {result.get('error', 'Unknown error')}")
                
                results[algo_name][data_name] = result
                
            except Exception as e:
                results[algo_name][data_name] = {
                    'original_size': len(data),
                    'compressed_size': 0,
                    'ratio': 1.0,
                    'time_ms': 0,
                    'success': False,
                    'error': str(e)
                }
                lines.append(f"  {data_name:20s}: [EXCEPTION] - {e}")
        
        lines.append("")
    
    # Summary table
    lines.append("=" * 80)
    lines.append("  COMPRESSION RATIO SUMMARY (lower is better)")
    lines.append("=" * 80)
    
    header = f"{'Algorithm':<12}" + "".join([f"{name:>15}" for name in datasets.keys()])
    lines.append(header)
    lines.append("-" * len(header))
    
    for algo_name in results.keys():
        row = f"{algo_name:<12}"
        for data_name in datasets.keys():
            r = results[algo_name][data_name]
            if r['success']:
                row += f"{r['ratio']:>14.4f} "
            else:
                row += f"{'FAILED':>15}"
        lines.append(row)
    
    lines.append("")
    
    # Identify problems
    lines.append("=" * 80)
    lines.append("  PROBLEM ANALYSIS")
    lines.append("=" * 80)
    lines.append("")
    
    # Compare Brotli/Zstd with others
    problem_algos = ['Brotli', 'Zstd']
    reference_algos = ['DEFLATE', 'LZDP']
    
    for pa in problem_algos:
        if pa not in results:
            continue
            
        lines.append(f"[ANALYSIS] {pa}:")
        
        for data_name in datasets.keys():
            if not results[pa][data_name]['success']:
                lines.append(f"  * {data_name}: Implementation error - {results[pa][data_name].get('error', 'Unknown')}")
                continue
                
            pa_ratio = results[pa][data_name]['ratio']
            
            # Find best reference
            best_ref_ratio = float('inf')
            best_ref = None
            for ra in reference_algos:
                if ra in results and results[ra][data_name]['success']:
                    if results[ra][data_name]['ratio'] < best_ref_ratio:
                        best_ref_ratio = results[ra][data_name]['ratio']
                        best_ref = ra
            
            if best_ref:
                gap = pa_ratio - best_ref_ratio
                if gap > 0.1:  # More than 10% worse
                    lines.append(f"  [WARNING] {data_name}: {pa} ({pa_ratio:.4f}) is {gap:.2%} worse than {best_ref} ({best_ref_ratio:.4f})")
                elif gap > 0.05:  # 5-10% worse
                    lines.append(f"  [CAUTION] {data_name}: {pa} ({pa_ratio:.4f}) is slightly worse than {best_ref} ({best_ref_ratio:.4f})")
                else:
                    lines.append(f"  [OK] {data_name}: {pa} performance acceptable")
        
        lines.append("")
    
    # Write to file
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    
    print(f"[DONE] Results written to {OUTPUT_FILE}")
    
    # Also print summary to console
    print("\n" + "=" * 80)
    print("COMPRESSION RATIO SUMMARY (lower is better)")
    print("=" * 80)
    header = f"{'Algorithm':<12}" + "".join([f"{name:>15}" for name in datasets.keys()])
    print(header)
    print("-" * len(header))
    
    for algo_name in results.keys():
        row = f"{algo_name:<12}"
        for data_name in datasets.keys():
            r = results[algo_name][data_name]
            if r['success']:
                row += f"{r['ratio']:>14.4f} "
            else:
                row += f"{'FAILED':>15}"
        print(row)
    
    return results


if __name__ == "__main__":
    results = main()
