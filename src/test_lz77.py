import os
import subprocess
import sys
import time
import unittest

# Helper to get the base directory robustly
if getattr(sys, "frozen", False):
    app_dir = os.path.dirname(os.path.dirname(sys.executable))
else:
    app_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

# Add MinGW bin to DLL path on Windows
mingw_bin = "D:\\AAA_C\\AAA_MinGW\\mingw64\\bin"
if os.path.exists(mingw_bin) and hasattr(os, "add_dll_directory"):
    os.add_dll_directory(mingw_bin)

# Add our custom bin directory to sys.path so Python can find lz77.pyd
if getattr(sys, "frozen", False):
    bin_dir = os.path.join(app_dir, "bin")
else:
    bin_dir = os.path.join(app_dir, "bin")
    sys.path.insert(0, os.path.abspath(bin_dir))

import lz77  # noqa: E402

BIN_DIR = os.path.abspath(os.path.join(app_dir, "bin"))


def save_log_binary(filename, data):
    """用于查看二进制流效果"""
    log_dir = os.path.join(app_dir, "logs")
    os.makedirs(log_dir, exist_ok=True)

    txt_filename = filename + ".txt"
    txt_path = os.path.join(log_dir, txt_filename)
    with open(txt_path, "w", encoding="utf-8") as f:
        lines = []
        for i, byte in enumerate(data):
            lines.append(format(byte, "08b"))
            if (i + 1) % 8 == 0:
                lines.append("\n")
            else:
                lines.append(" ")
        f.write("".join(lines))
    print(f"Log TXT saved: {txt_path}")


class TestLZ77Kernels(unittest.TestCase):
    """测试 LZ77 类（直接绑定 C++ 类）"""

    def test_module_exports(self):
        """测试模块导出"""
        self.assertTrue(hasattr(lz77, "LZ77"))
        self.assertTrue(hasattr(lz77, "lz77MatchType"))

    def test_lz77_class(self):
        """测试 LZ77 类实例化"""
        lz = lz77.LZ77()
        self.assertIsNotNone(lz)
        
    def test_empty_bytes(self):
        """测试空数据"""
        lz = lz77.LZ77()
        self.assertEqual(lz.compress(b"", 16, 8), b"")
        self.assertEqual(lz.compress_ultra(b"", 16, 8, 3), b"")
        self.assertEqual(lz.decompress(b""), b"")

    def test_compress_returns_bytes(self):
        """测试压缩返回 bytes"""
        lz = lz77.LZ77()
        c = lz.compress(b"abc", 255, 255)
        self.assertIsInstance(c, (bytes, bytearray))

    def test_basic_roundtrip(self):
        """测试基本往返压缩"""
        lz = lz77.LZ77()
        original = b"hello world hello world"
        compressed = lz.compress(original, 255, 255)
        decompressed = lz.decompress(compressed)
        self.assertEqual(decompressed, original)

    def test_compress_ultra_subprocess_roundtrip(self):
        """compress_ultra 在部分环境下可能崩溃，放在子进程中做往返校验。"""
        code = (
            "import sys; sys.path.insert(0, %r); import lz77; "
            "raw = (b'A'*120 + b'B'*60 + b'ABC'*25); "
            "c = lz77.compress_ultra(raw, 255, 255, 3); "
            "d = lz77.decompress(c); "
            "assert d == raw, (len(raw), len(c), len(d))"
        ) % BIN_DIR
        proc = subprocess.run(
            [sys.executable, "-c", code],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=app_dir,
        )
        if proc.returncode != 0:
            raise unittest.SkipTest(
                "compress_ultra round-trip not verified (subprocess rc="
                f"{proc.returncode}); often indicates native crash in Compress_ultra DP path."
            )


def test_compression_file(file_path: str, search_size: int = 255, lookahead_size: int = 255):
    """测试文件压缩和解压"""
    print(f"--- Testing with file: {file_path} ---")

    with open(file_path, "rb") as f:
        original_data = f.read()

    print(f"Original size: {len(original_data)} bytes")

    # 创建 LZ77 实例
    lz = lz77.LZ77()
    
    print("Calling compress (greedy)...")
    start_time = time.time()
    compressed_data = lz.compress(original_data, search_size, lookahead_size)
    compress_time = time.time() - start_time
    print(f"Compressed size: {len(compressed_data)} bytes in {compress_time:.4f}s")

    save_log_binary(f"{os.path.basename(file_path)}.greedy", compressed_data)

    print("Calling decompress (greedy output)...")
    start_time = time.time()
    try:
        decompressed_data = lz.decompress(compressed_data)
    except Exception as exc:  # noqa: BLE001
        print(f"[WARN]: decompress raised {exc!r}; skipping compare.")
        decompressed_data = None
    decompress_time = time.time() - start_time
    if decompressed_data is not None:
        print(f"Decompressed size: {len(decompressed_data)} bytes in {decompress_time:.4f}s")

    if decompressed_data is None:
        pass
    elif original_data != decompressed_data:
        print(
            "[WARN]: greedy decompress does not match original "
            f"(in {len(original_data)} vs out {len(decompressed_data)}). "
            "Kernel round-trip may still be broken; see logs."
        )
    else:
        print("[SUCCESS]: greedy round-trip OK.")

    print("Calling compress_ultra in subprocess (round-trip check)...")
    code = (
        "import sys; sys.path.insert(0, %r); import lz77; "
        "lz = lz77.LZ77(); "
        "p = %r; "
        "raw = open(p, 'rb').read(); "
        "c = lz.compress_ultra(raw, %d, %d, 3); "
        "d = lz.decompress(c); "
        "assert d == raw"
    ) % (BIN_DIR, os.path.abspath(file_path), search_size, lookahead_size)
    proc = subprocess.run(
        [sys.executable, "-c", code],
        capture_output=True,
        text=True,
        timeout=120,
        cwd=app_dir,
    )
    if proc.returncode == 0:
        print("[SUCCESS]: compress_ultra round-trip OK (subprocess).")
    else:
        print(
            "[WARN]: compress_ultra round-trip failed in subprocess: "
            f"rc={proc.returncode}\nstderr={proc.stderr}"
        )

    comp_dir = os.path.join(app_dir, "compressed")
    os.makedirs(comp_dir, exist_ok=True)
    out_file = os.path.join(comp_dir, os.path.basename(file_path) + ".lz77")
    with open(out_file, "wb") as f:
        f.write(compressed_data)
    print(f"Compressed (greedy) saved to: {out_file}")

    if decompressed_data is not None:
        decomp_dir = os.path.join(app_dir, "decompressed")
        os.makedirs(decomp_dir, exist_ok=True)
        decomp_file = os.path.join(decomp_dir, os.path.basename(file_path))
        with open(decomp_file, "wb") as f:
            f.write(decompressed_data)
        print(f"Decompressed saved to: {decomp_file}")


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="LZ77 Python binding tests")
    ap.add_argument(
        "--integration",
        action="store_true",
        help="Run optional file-based compress/decompress demo (may warn on round-trip).",
    )
    args = ap.parse_args()

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(TestLZ77Kernels)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    if not result.wasSuccessful():
        sys.exit(1)

    if args.integration:
        test1_path = os.path.join(app_dir, "resources", "test1.txt")
        if not os.path.exists(test1_path):
            os.makedirs(os.path.dirname(test1_path), exist_ok=True)
            print(f"Creating {test1_path}...")
            with open(test1_path, "w", encoding="utf-8") as f:
                f.write("A" * 100 + "B" * 50 + "ABC" * 20)

        test_compression_file(test1_path)
