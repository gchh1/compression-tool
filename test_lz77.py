import os
import sys

# Add MinGW bin to DLL path on Windows
mingw_bin = "D:\\AAA_C\\AAA_MinGW\\mingw64\\bin"
if os.path.exists(mingw_bin) and hasattr(os, 'add_dll_directory'):
    os.add_dll_directory(mingw_bin)

import lz77
import time

def test_compression(file_path):
    print(f"--- Testing with file: {file_path} ---")
    
    with open(file_path, "rb") as f:
        original_data = f.read()
        
    print(f"Original size: {len(original_data)} bytes")
    
    start_time = time.time()
    print("Calling compress...")
    compressed_data = lz77.compress(original_data, 16, 8)
    compress_time = time.time() - start_time
    
    print(f"Compressed size: {len(compressed_data)} bytes")
    print("Calling decompress...")
    
    start_time = time.time()
    decompressed_data = lz77.decompress(compressed_data)
    decompress_time = time.time() - start_time
    print("Decompress finished.")
    
    print(f"Decompressed size: {len(decompressed_data)} bytes")
    print(f"Decompression time: {decompress_time:.4f} seconds")
    
    if original_data == decompressed_data:
        print("[SUCCESS]: Decompressed data matches original!")
    else:
        print("[FAILED]: Decompressed data DOES NOT match original!")
        sys.exit(1)

if __name__ == "__main__":
    if not os.path.exists("test1.txt"):
        print("Creating test1.txt...")
        with open("test1.txt", "w", encoding="utf-8") as f:
            f.write("A" * 100 + "B" * 50 + "ABC" * 20)
            
    test_compression("test1.txt")
