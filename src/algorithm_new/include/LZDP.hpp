#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <vector>

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
        size_t mml = 0
    ) : window{sw, lw, mml},
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
            return literal_count * (config_.encoding.offset_bits + 1) + match_count * (config_.encoding.length_bits + 1);
        }
        size_t tmp = literal_count;
        size_t maxlen = (size_t{1} << config_.encoding.length_bits) - 1;
        size_t cost = 0;
        while (tmp > maxlen) {
            cost += config_.encoding.offset_bits + config_.encoding.length_bits + maxlen * 8;
            tmp -= maxlen;
        }
        cost += config_.encoding.offset_bits + config_.encoding.length_bits + tmp * 8;
        return cost;
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
        size_t end = 0
    );

    template <typename DPNodes>
    std::vector<Triple> dpbacktrack(// 从end到begin
        DPNodes& dp_nodes,
        int& cur_pos,
        size_t begin = 0
    );
};

}  // namespace compressor::algorithm