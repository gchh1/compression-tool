import os
import sys
import time
import struct

# Add MinGW bin to DLL path on Windows
mingw_bin = "D:\\AAA_C\\AAA_MinGW\\mingw64\\bin"
if os.path.exists(mingw_bin) and hasattr(os, 'add_dll_directory'):
    os.add_dll_directory(mingw_bin)

# Add our custom bin directory to sys.path so Python can find lz77.pyd
bin_dir = os.path.join(os.path.dirname(__file__), "..", "bin")
sys.path.insert(0, os.path.abspath(bin_dir))

import lz77

def pack_directory(directory_path):
    """
    Packs a directory into a single binary stream.
    Format for each file:
    - 4 bytes: path length (L)
    - L bytes: path string (utf-8)
    - 4 bytes: file data length (S)
    - S bytes: file data
    """
    packed_data = bytearray()
    file_count = 0
    
    for root, _, files in os.walk(directory_path):
        for file in files:
            file_path = os.path.join(root, file)
            # Get relative path so it's consistent
            rel_path = os.path.relpath(file_path, directory_path)
            rel_path_bytes = rel_path.encode('utf-8')
            
            with open(file_path, 'rb') as f:
                file_data = f.read()
                
            # Pack path length and path
            packed_data.extend(struct.pack('<I', len(rel_path_bytes)))
            packed_data.extend(rel_path_bytes)
            
            # Pack data length and data
            packed_data.extend(struct.pack('<I', len(file_data)))
            packed_data.extend(file_data)
            
            file_count += 1
            
    print(f"Packed {file_count} files from '{directory_path}'.")
    return bytes(packed_data)

def test_site_compression(directory_path):
    print(f"--- Testing compression on directory: {directory_path} ---")
    
    if not os.path.exists(directory_path):
        print(f"Directory not found: {directory_path}")
        return
        
    original_archive = pack_directory(directory_path)
    
    print(f"Original archive size: {len(original_archive)} bytes")
    
    print("Calling compress...")
    start_time = time.time()
    # We use search_size=16, lookahead_size=8 as in previous tests
    # which means up to 65535 bytes backward search, up to 255 bytes match
    # Note: search_window=16 implies swd=uint16_t in the C++ code, which maxes at 65535
    compressed_data = lz77.compress(original_archive, 16, 8)
    compress_time = time.time() - start_time
    
    print(f"Compressed size: {len(compressed_data)} bytes")
    print(f"Compression ratio: {len(compressed_data) / len(original_archive):.2f}")
    print(f"Compression time: {compress_time:.4f} seconds")
    
    print("Calling decompress...")
    start_time = time.time()
    decompressed_data = lz77.decompress(compressed_data)
    decompress_time = time.time() - start_time
    print("Decompress finished.")
    
    print(f"Decompressed size: {len(decompressed_data)} bytes")
    print(f"Decompression time: {decompress_time:.4f} seconds")
    
    if original_archive == decompressed_data:
        print("[SUCCESS]: Decompressed archive matches the original perfectly!")
    else:
        print("[FAILED]: Decompressed archive DOES NOT match!")
        sys.exit(1)

if __name__ == "__main__":
    demo_site_path = os.path.join(os.path.dirname(__file__), "..", "lty_demo-site")
    test_site_compression(demo_site_path)
