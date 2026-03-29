import os
import sys

# Helper to get the base directory robustly
if getattr(sys, 'frozen', False):
    app_dir = os.path.dirname(os.path.dirname(sys.executable)) # Go up from Package/bin to Package
else:
    app_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

# Add MinGW bin to DLL path on Windows
mingw_bin = "D:\\AAA_C\\AAA_MinGW\\mingw64\\bin"
if os.path.exists(mingw_bin) and hasattr(os, 'add_dll_directory'):
    os.add_dll_directory(mingw_bin)

# Add our custom bin directory to sys.path so Python can find lz77.pyd
if getattr(sys, 'frozen', False):
    bin_dir = os.path.join(app_dir, "bin")
else:
    bin_dir = os.path.join(app_dir, "bin")
    sys.path.insert(0, os.path.abspath(bin_dir))

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
        
    # Write the compressed output to a file so we can see it
    # Save compressed data to file
    if getattr(sys, 'frozen', False):
        comp_dir = os.path.join(app_dir, "compressed")
    else:
        comp_dir = os.path.join(app_dir, "Package", "compressed")
    os.makedirs(comp_dir, exist_ok=True)
    out_file = os.path.join(comp_dir, os.path.basename(file_path) + ".lz77")
    with open(out_file, "wb") as f:
        f.write(compressed_data)
    print(f"Compressed data saved to: {out_file}")
    
    # Write the decompressed output to a file to verify output
    if getattr(sys, 'frozen', False):
        decomp_dir = os.path.join(app_dir, "decompressed")
    else:
        decomp_dir = os.path.join(app_dir, "Package", "decompressed")
    os.makedirs(decomp_dir, exist_ok=True)
    decomp_file = os.path.join(decomp_dir, os.path.basename(file_path))
    with open(decomp_file, "wb") as f:
        f.write(decompressed_data)
    print(f"Decompressed data saved to: {decomp_file}")

if __name__ == "__main__":
    test1_path = os.path.join(app_dir, "resources", "test1.txt")
    if not os.path.exists(test1_path):
        os.makedirs(os.path.dirname(test1_path), exist_ok=True)
        print(f"Creating {test1_path}...")
        with open(test1_path, "w", encoding="utf-8") as f:
            f.write("A" * 100 + "B" * 50 + "ABC" * 20)
            
    test_compression(test1_path)
