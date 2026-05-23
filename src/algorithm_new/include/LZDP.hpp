#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ByteView.hpp"
#include "LZencoding.hpp"
#include "Models.hpp"
#include "Utils.hpp"

#include "HashChain.hpp"
#include "KMP.hpp"
#include "Streaming.hpp"
#include "config/Config.hpp"

namespace compressor::algorithm {

struct LZDPConfig {
    config::Lz77WindowConfig window;
    config::DpMatcherConfig dp;
    EncodingConfig encoding;

    LZDPConfig(
        size_t sw = 4095,
        size_t lw = 255,
        size_t dp_top = 3,
        models::MatchEngine me = models::MatchEngine::HashChain,
        bool flag = true,
        size_t mml = 0,
        size_t mcl = 256
    ) : window{sw, lw, mml, mcl},
        dp{dp_top, me},
        encoding{static_cast<uint8_t>(utils::calcBitWidth(sw)),
                 static_cast<uint8_t>(utils::calcBitWidth(lw)),
                 flag} {
        if (window.min_match_len == 0) {
            window.min_match_len = utils::getMinMatch(encoding.offset_bits, encoding.length_bits);
        }
    }
};

class LZDP {
    LZDPConfig config_;

    size_t cal_cost(size_t literal_count, size_t match_count) const {
        if (config_.encoding.use_flag_encoding) {
            const size_t lit_cost = 9;
            const size_t match_cost = 1 + config_.encoding.offset_bits + config_.encoding.length_bits;
            return literal_count * lit_cost + match_count * match_cost;
        }
        const size_t lit_cost = 8;
        const size_t match_cost = config_.encoding.offset_bits + config_.encoding.length_bits;
        return literal_count * lit_cost + match_count * match_cost;
    }

public:
    explicit LZDP(const LZDPConfig& config = LZDPConfig())
        : config_(config) {}

    const LZDPConfig& getConfig() const { return config_; }

    template <typename Input, typename DPNodes>
    void dpforward(
        const Input& input,
        DPNodes& dp,
        size_t begin = 0,
        size_t end = 0,
        size_t match_end = 0
    );

    template <typename DPNodes>
    std::vector<Triple> dpbacktrack(// 从end到begin
        DPNodes& dp_nodes,
        int& cur_pos,
        size_t begin = 0
    );
};

}  // namespace compressor::algorithm

// ──── Pipeline ────

#include "LZencoding.hpp"
#include "Models.hpp"

namespace compressor::algorithm::pipeline {

struct LZDPNonStreamingResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

LZDPNonStreamingResult compress_bytes(
    const std::vector<uint8_t>& input,
    const LZDPConfig& config);

std::vector<uint8_t> decompress_bytes(
    const std::vector<uint8_t>& compressed,
    const LZDPConfig& config);

struct Phase1Result {
    size_t total_input_bytes{0};
    models::DPNode terminal_node{};
};

Phase1Result run_phase1_dpforward(
    LZDP& lzdp,
    const LZDPConfig& config,
    const std::string& input_path,
    const std::string& temp_a_path,
    size_t chunk_size);

struct Phase2Result {
    size_t total_tokens{0};
    std::vector<Triple> triples;
};

Phase2Result run_phase2_dpbacktrack(
    LZDP& lzdp,
    const Phase1Result& phase1,
    const std::string& temp_a_path,
    const std::string& temp_b_path,
    size_t chunk_size);

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