#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "LZDP.hpp"
#include "LZencoding.hpp"

namespace compressor::algorithm {

struct DPCandidate {
    Triple triple;
    bool is_chosen;
};

struct DPState {
    size_t position{0};
    bool reachable{false};
    size_t cost{0};
    size_t token_count{0};
    size_t literal_count{0};
    size_t match_count{0};
    size_t predecessor{0};
    Triple choice;
};

struct DPStep {
    size_t position{0};
    std::vector<DPCandidate> candidates;
    size_t best_cost{0};
    /// GUI legacy field (token count at ``position``).
    size_t best_token_count{0};
};

struct DPVisualization {
    std::vector<DPStep> steps;
    std::vector<DPState> dp_array;
    std::vector<Triple> optimal_path;
    size_t input_length{0};
    size_t search_size{0};
    size_t lookahead_size{0};
};

/// DP strip for GUI demo (``cal_cost`` semantics, one step per input byte).
DPVisualization get_dp_visualization(
    const std::vector<uint8_t>& input,
    const LZDPConfig& config,
    size_t range = 0);

} // namespace compressor::algorithm
