#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "LZDP.hpp"
#include "LZencoding.hpp"
#include "Models.hpp"
#include "pipeline/Phase1Dpforward.hpp"

namespace compressor::algorithm::pipeline {

struct Phase2Result {
    size_t total_tokens{0};
    std::vector<Triple> triples;
};

/// §1.20 阶段②：Reverse ChunkReader(TempA) → VB&lt;DPNode&gt; → dpbacktrack → 预分配 TempB + Reverse 写入
Phase2Result run_phase2_dpbacktrack(
    LZDP& lzdp,
    const Phase1Result& phase1,
    const std::string& temp_a_path,
    const std::string& temp_b_path,
    size_t chunk_size);

}  // namespace compressor::algorithm::pipeline
