#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Deflate.hpp"

namespace compressor::core_new {

struct DeflateCompressorConfig {
    algorithm::DeflateConfig deflate;
    bool use_streaming{false};
    size_t streaming_chunk_size{1 << 20};
    std::string workspace_dir{"."};
};

class DeflateCompressor {
public:
    explicit DeflateCompressor(DeflateCompressorConfig config = {});

    std::vector<uint8_t> compress_file(const std::string& input_path);
    std::vector<uint8_t> decompress_file(const std::string& compressed_path);

    void compress_file_to_path(const std::string& input_path,
                               const std::string& output_path);

    void decompress_file_to_path(const std::string& compressed_path,
                                  const std::string& output_path,
                                  size_t write_chunk_size = 1 << 20);

private:
    DeflateCompressorConfig config_;
};

}  // namespace compressor::core_new