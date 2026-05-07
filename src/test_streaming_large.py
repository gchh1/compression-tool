import sys
import os
import time
import tracemalloc

sys.path.insert(0, r'D:\AAA_C\compression-tool\build_pybind\src\bindings\pybind')
import core_engine


def generate_test_data(size_mb: int) -> list[int]:
    pattern = b"The quick brown fox jumps over the lazy dog. " * 10
    repeats = (size_mb * 1024 * 1024) // len(pattern)
    data = list(pattern * repeats)
    return data[:size_mb * 1024 * 1024]


def test_streaming_memory(algo_id, decomp_id, algo_name, size_mb: int):
    print(f"\n{'='*60}")
    print(f"  {algo_name} | {size_mb}MB | Streaming Test")
    print(f"{'='*60}")

    tracemalloc.start()

    print(f"  [1/4] Generating test data ({size_mb}MB)...")
    t0 = time.perf_counter()
    data = generate_test_data(size_mb)
    t1 = time.perf_counter()
    gen_mem = tracemalloc.get_traced_memory()
    print(f"        Generated in {t1-t0:.2f}s, memory: {gen_mem[0]/1024/1024:.1f}MB / peak {gen_mem[1]/1024/1024:.1f}MB")

    print(f"  [2/4] Compressing (streaming pipeline)...")
    t2 = time.perf_counter()
    result = core_engine.pipeline_compress(data, [algo_id])
    t3 = time.perf_counter()
    comp_mem = tracemalloc.get_traced_memory()
    comp_ratio = result.compressed_size / len(data) if len(data) > 0 else 0
    print(f"        {len(data)//1024//1024}MB -> {result.compressed_size//1024//1024}MB (ratio={comp_ratio:.4f})")
    print(f"        Time: {t3-t2:.2f}s, memory: {comp_mem[0]/1024/1024:.1f}MB / peak {comp_mem[1]/1024/1024:.1f}MB")

    print(f"  [3/4] Decompressing (streaming pipeline)...")
    t4 = time.perf_counter()
    decomp_result = core_engine.pipeline_decompress(result.data, [decomp_id])
    t5 = time.perf_counter()
    decomp_mem = tracemalloc.get_traced_memory()
    match = decomp_result.data == data
    print(f"        {result.compressed_size//1024//1024}MB -> {len(decomp_result.data)//1024//1024}MB")
    print(f"        Time: {t5-t4:.2f}s, roundtrip_match={match}")
    print(f"        Memory: {decomp_mem[0]/1024/1024:.1f}MB / peak {decomp_mem[1]/1024/1024:.1f}MB")

    final_mem = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    total_time = t5 - t0
    print(f"\n  SUMMARY:")
    print(f"    Total time:       {total_time:.2f}s")
    print(f"    Peak memory:      {final_mem[1]/1024/1024:.1f}MB")
    print(f"    Current memory:   {final_mem[0]/1024/1024:.1f}MB")
    print(f"    Data integrity:   {'PASS' if match else 'FAIL'}")
    print(f"    Compression:      {comp_ratio*100:.1f}%")

    del data
    del result
    del decomp_result

    return {
        'algo': algo_name,
        'size_mb': size_mb,
        'total_time': total_time,
        'peak_mb': final_mem[1] / 1024 / 1024,
        'current_mb': final_mem[0] / 1024 / 1024,
        'match': match,
        'ratio': comp_ratio,
    }


def main():
    print("=" * 60)
    print("  Streaming Chunk Module - Large File Memory Test")
    print("  Terminator-based format | chunk_size=1MB")
    print("=" * 60)

    algos = [
        (core_engine.AlgorithmID.LZSS, core_engine.AlgorithmID.LZSS_DECOMPRESS, "LZSS"),
        (core_engine.AlgorithmID.LZMINE, core_engine.AlgorithmID.LZMINE_DECOMPRESS, "LZMine"),
        (core_engine.AlgorithmID.LZCRAZY, core_engine.AlgorithmID.LZCRAZY_DECOMPRESS, "LZCrazy"),
        (core_engine.AlgorithmID.CRAZYFLATE, core_engine.AlgorithmID.CRAZYFLATE_DECOMPRESS, "CrazyFlate"),
        (core_engine.AlgorithmID.DEFLATE, core_engine.AlgorithmID.INFLATE, "Deflate"),
    ]

    sizes_mb = [5, 20, 50]

    results = []
    for size_mb in sizes_mb:
        for algo_id, decomp_id, name in algos:
            r = test_streaming_memory(algo_id, decomp_id, name, size_mb)
            results.append(r)

    print(f"\n{'='*70}")
    print(f"  ALL RESULTS SUMMARY")
    print(f"{'='*70}")
    print(f"{'Algorithm':<12} {'Size':>6} {'Time(s)':>8} {'Peak(MB)':>10} {'Current(MB)':>12} {'Ratio':>8} {'Match':>6}")
    print(f"{'-'*70}")
    for r in results:
        status = "OK" if r['match'] else "FAIL"
        print(f"{r['algo']:<12} {r['size_mb']:>5}MB {r['total_time']:>8.2f} {r['peak_mb']:>10.1f} {r['current_mb']:>12.1f} {r['ratio']:>7.2%} {status:>6}")

    all_pass = all(r['match'] for r in results)
    max_peak = max(r['peak_mb'] for r in results)
    print(f"\n  Overall: {'ALL PASSED' if all_pass else 'SOME FAILED'} | Max Peak Memory: {max_peak:.1f}MB")


if __name__ == "__main__":
    main()
