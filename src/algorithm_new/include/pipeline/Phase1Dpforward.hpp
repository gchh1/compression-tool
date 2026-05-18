#pragma once

#include <cstddef>
#include <string>

#include "LZDP.hpp"
#include "Models.hpp"

namespace compressor::algorithm::pipeline {

/// 阶段①结果：TempA 已写入；末节点供 `total_tokens` 使用。
struct Phase1Result {
    size_t total_input_bytes{0};
    models::DPNode terminal_node{};
};

/// §1.20 阶段①：ChunkReader → VB&lt;u8&gt; 三分块 → dpforward → DPNode → TempA
Phase1Result run_phase1_dpforward(
    LZDP& lzdp,
    const LZDPConfig& config,
    const std::string& input_path,
    const std::string& temp_a_path,
    size_t chunk_size);

}  // namespace compressor::algorithm::pipeline
