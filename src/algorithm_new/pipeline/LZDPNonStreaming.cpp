#include "pipeline/LZDPNonStreaming.hpp"

#include <utility>

#include "ByteView.hpp"

namespace compressor::algorithm::pipeline {

namespace {

void init_dp_frontier(std::vector<models::DPNode>& dp, const std::vector<uint8_t>& input) {
    if (input.empty()) {
        return;
    }
    dp.assign(input.size(), models::DPNode{});
    dp[0] = models::DPNode(0, 0, -1, Triple(0, 1, input[0]));
}

}  // namespace

LZDPNonStreamingResult compress_bytes(
    const std::vector<uint8_t>& input,
    const LZDPConfig& config) {
    LZDPNonStreamingResult result;
    if (input.empty()) {
        return result;
    }

    LZDP lzdp(config);
    std::vector<models::DPNode> dp;
    init_dp_frontier(dp, input);

    VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp, 0, input.size());

    int cur_pos = 0;
    std::vector<Triple> triples = lzdp.dpbacktrack(dp, cur_pos, 0);

    if (!config.encoding.use_flag_encoding) {
        triples = literalrun(triples, config.window.look_size);
    }

    compressor::utils::_buffer pending;
    result.triples = std::move(triples);
    result.compressed =
        encoding_triple_lz(result.triples, config.encoding, pending, true);
    return result;
}

std::vector<uint8_t> decompress_bytes(
    const std::vector<uint8_t>& compressed,
    const LZDPConfig& config) {
    compressor::utils::_buffer pending;
    std::vector<Triple> triples = readtriple(compressed, config.encoding, pending);
    if (!config.encoding.use_flag_encoding) {
        triples = literalrun(triples, config.window.look_size);
    }
    return decode_triple(triples, config.encoding, pending);
}

}  // namespace compressor::algorithm::pipeline
