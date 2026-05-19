#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "LZSS.hpp"

namespace compressor::core_new {

struct LZSSCompressorConfig {
    algorithm::LZSSConfig lzss;
    bool use_streaming{false};
    size_t streaming_chunk_size{1 << 20};
    std::string workspace_dir{"."};
};

class LZSSCompressor {
public:
    explicit LZSSCompressor(LZSSCompressorConfig config = {});

    std::vector<uint8_t> compress_file(const std::string& input_path);
    std::vector<uint8_t> decompress_file(const std::string& compressed_path);

    void compress_file_to_path(const std::string& input_path,
                               const std::string& output_path);

    void decompress_file_to_path(const std::string& compressed_path,
                                  const std::string& output_path,
                                  size_t write_chunk_size = 1 << 20);

private:
    LZSSCompressorConfig config_;
};

}  // namespace compressor::core_new