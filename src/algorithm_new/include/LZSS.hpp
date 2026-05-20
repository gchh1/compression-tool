#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "MatchEngine.hpp"
#include "LZencoding.hpp"
#include "config/Config.hpp"
#include "Utils.hpp"

namespace compressor::algorithm {

struct LZSSConfig {
    config::Lz77WindowConfig window;
    EncodingConfig encoding;

    LZSSConfig(
        size_t sw = 4095,
        size_t lw = 255,
        size_t mml = 3,
        bool use_flag = true)
        : window{sw, lw, mml},
          encoding{static_cast<uint8_t>(utils::calcBitWidth(sw)),
                   static_cast<uint8_t>(utils::calcBitWidth(lw)),
                   use_flag} {
        if (window.min_match_len == 0) {
            window.min_match_len =
                utils::getMinMatch(encoding.offset_bits, encoding.length_bits);
        }
    }
};

class LZSS {
public:
    explicit LZSS(const LZSSConfig& cfg = LZSSConfig{}) : config_(cfg) {}

    const LZSSConfig& getConfig() const { return config_; }

    /// §1.14 贪心：``matchAtPosition`` + ``dp_top=1``（与 ``kmpSearch`` 返回容器约定一致）。
    std::vector<Triple> greedyMatch(const std::vector<uint8_t>& input) const {
        return LZMatcher::greedyWholeInput(
            input,
            config_.window,
            config_.encoding.offset_bits,
            config_.encoding.length_bits);
    }

private:
    LZSSConfig config_;
};

}  // namespace compressor::algorithm

// ──── Pipeline ────

#include <string>

namespace compressor::algorithm::pipeline {

struct LZSSNonStreamingResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

LZSSNonStreamingResult compress_bytes_lzss(
    const std::vector<uint8_t>& input,
    const LZSSConfig& config);

std::vector<uint8_t> decompress_bytes_lzss(
    const std::vector<uint8_t>& compressed,
    const LZSSConfig& config);

struct LZSSStreamingOptions {
    size_t chunk_size{1 << 20};
    std::string workspace_dir;
    std::string temp_a_name{"temp_lzss.dp"};
};

class LZSSStreamingPipeline {
public:
    explicit LZSSStreamingPipeline(LZSSConfig config, LZSSStreamingOptions options);

    void compress_file(const std::string& input_path, const std::string& output_path);

private:
    LZSSConfig config_;
    LZSSStreamingOptions options_;
    LZSS lzss_;

    std::string temp_a_path() const;
};

}  // namespace compressor::algorithm::pipeline