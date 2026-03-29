import os
import sys
import time
import struct

# Helper to get the base directory robustly
if getattr(sys, 'frozen', False):
    # Go up from Package/bin to Package
    app_dir = os.path.dirname(os.path.dirname(sys.executable))
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

def unpack_directory(packed_data, output_dir):
    """
    Unpacks a binary stream back into a directory structure.
    Format for each file:
    - 4 bytes: path length (L)
    - L bytes: path string (utf-8)
    - 4 bytes: file data length (S)
    - S bytes: file data
    """
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    offset = 0
    file_count = 0
    total_length = len(packed_data)
    
    while offset < total_length:
        # Read path length (4 bytes)
        if offset + 4 > total_length:
            break
        path_len = struct.unpack('<I', packed_data[offset:offset+4])[0]
        offset += 4
        
        # Read path
        rel_path = packed_data[offset:offset+path_len].decode('utf-8')
        offset += path_len
        
        # Read data length (4 bytes)
        data_len = struct.unpack('<I', packed_data[offset:offset+4])[0]
        offset += 4
        
        # Read data
        file_data = packed_data[offset:offset+data_len]
        offset += data_len
        
        # Write to file
        out_path = os.path.join(output_dir, rel_path)
        out_file_dir = os.path.dirname(out_path)
        if not os.path.exists(out_file_dir):
            os.makedirs(out_file_dir)
            
        with open(out_path, 'wb') as f:
            f.write(file_data)
            
        file_count += 1
        
    print(f"Unpacked {file_count} files to '{output_dir}'.")
    return file_count

def unpack_directory(packed_data, output_dir):
    """
    Unpacks a binary stream back into a directory structure.
    Format for each file:
    - 4 bytes: path length (L)
    - L bytes: path string (utf-8)
    - 4 bytes: file data length (S)
    - S bytes: file data
    """
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    offset = 0
    file_count = 0
    total_length = len(packed_data)
    
    while offset < total_length:
        # Read path length (4 bytes)
        if offset + 4 > total_length:
            break
        path_len = struct.unpack('<I', packed_data[offset:offset+4])[0]
        offset += 4
        
        # Read path
        rel_path = packed_data[offset:offset+path_len].decode('utf-8')
        offset += path_len
        
        # Read data length (4 bytes)
        data_len = struct.unpack('<I', packed_data[offset:offset+4])[0]
        offset += 4
        
        # Read data
        file_data = packed_data[offset:offset+data_len]
        offset += data_len
        
        # Write to file
        out_path = os.path.join(output_dir, rel_path)
        out_file_dir = os.path.dirname(out_path)
        if not os.path.exists(out_file_dir):
            os.makedirs(out_file_dir)
            
        with open(out_path, 'wb') as f:
            f.write(file_data)
            
        file_count += 1
        
    print(f"Unpacked {file_count} files to '{output_dir}'.")
    return file_count

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
        
    # Write to a compressed file
    if getattr(sys, 'frozen', False):
        comp_dir = os.path.join(app_dir, "compressed")
    else:
        comp_dir = os.path.join(app_dir, "Package", "compressed")
    os.makedirs(comp_dir, exist_ok=True)
    out_file = os.path.join(comp_dir, os.path.basename(directory_path) + ".lz77")
    with open(out_file, "wb") as f:
        f.write(compressed_data)
    print(f"Compressed archive saved to: {out_file}")
    
    # Write the decompressed output to a file
    if getattr(sys, 'frozen', False):
        decomp_dir = os.path.join(app_dir, "decompressed")
    else:
        decomp_dir = os.path.join(app_dir, "Package", "decompressed")
    os.makedirs(decomp_dir, exist_ok=True)
    decomp_file = os.path.join(decomp_dir, os.path.basename(directory_path) + "_restored.bin")
    with open(decomp_file, "wb") as f:
        f.write(decompressed_data)
        
    # Actually unpack the directory so the user can view the restored site
    restored_site_dir = os.path.join(decomp_dir, "demo_site_restored")
    unpack_directory(decompressed_data, restored_site_dir)
    print(f"Decompressed archive saved to: {decomp_file}")
    print(f"Site perfectly restored to: {restored_site_dir} - You can now open index.html inside!")

if __name__ == "__main__":
    demo_site_path = os.path.join(app_dir, "resources", "demo_site")
    test_site_compression(demo_site_path)
