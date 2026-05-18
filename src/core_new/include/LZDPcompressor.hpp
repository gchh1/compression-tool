#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "LZDP.hpp"
#include "pipeline/LZDPNonStreaming.hpp"
#include "pipeline/LZDPStreamingPipeline.hpp"

namespace compressor::core_new {

struct LZDPCompressorConfig {
    algorithm::LZDPConfig lzdp;
    bool use_streaming{false};
    size_t streaming_chunk_size{1 << 20};
    std::string workspace_dir{"."};
};

class LZDPCompressor {
public:
    explicit LZDPCompressor(LZDPCompressorConfig config = {});

    std::vector<uint8_t> compress_file(const std::string& input_path);
    std::vector<uint8_t> decompress_file(const std::string& compressed_path);

    void compress_file_to_path(const std::string& input_path,
                               const std::string& output_path);

private:
    LZDPCompressorConfig config_;
};

}  // namespace compressor::core_new
