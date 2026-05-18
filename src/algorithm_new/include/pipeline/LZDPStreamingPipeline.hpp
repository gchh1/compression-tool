#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "LZDP.hpp"

namespace compressor::algorithm::pipeline {

/// §1.20 四阶段管线（TempA / TempB / 正向 emit），与 design doc 对齐。
struct LZDPStreamingOptions {
    size_t chunk_size{1 << 20};
    std::string workspace_dir;
    std::string temp_a_name{"temp_a.dp"};
    std::string temp_b_name{"temp_b.tok"};
};

class LZDPStreamingPipeline {
public:
    explicit LZDPStreamingPipeline(LZDPConfig config, LZDPStreamingOptions options);

    void compress_file(const std::string& input_path, const std::string& output_path);

private:
    LZDPConfig config_;
    LZDPStreamingOptions options_;
    LZDP lzdp_;

    std::string temp_a_path() const;
    std::string temp_b_path() const;
};

}  // namespace compressor::algorithm::pipeline
