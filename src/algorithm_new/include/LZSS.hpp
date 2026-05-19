#pragma once

#include <cstddef>
#include <utility>
#include <vector>

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
            window.min_match_len = 3;
        }
    }
};

class LZSS {
public:
    explicit LZSS(const LZSSConfig& cfg = LZSSConfig{}) : config_(cfg) {}

    const LZSSConfig& getConfig() const { return config_; }

    std::vector<Triple> greedyMatch(const std::vector<uint8_t>& input) const {
        std::vector<Triple> result;
        const size_t n = input.size();
        if (n == 0) return result;

        const size_t search_size = config_.window.search_size;
        const size_t min_match = config_.window.min_match_len;
        const size_t max_match = config_.window.look_size;

        size_t cursor = 0;
        while (cursor < n) {
            size_t best_off = 0;
            size_t best_len = 0;

            size_t search_start = (cursor > search_size) ? cursor - search_size : 0;
            for (size_t i = search_start; i < cursor; ++i) {
                size_t cur_len = 0;
                while (cur_len < max_match && cursor + cur_len < n &&
                       input[i + cur_len] == input[cursor + cur_len]) {
                    ++cur_len;
                }
                if (cur_len > best_len) {
                    best_len = cur_len;
                    best_off = cursor - i;
                }
            }

            if (best_len >= min_match) {
                result.emplace_back(static_cast<uint32_t>(best_off),
                                    static_cast<uint32_t>(best_len), 0);
                cursor += best_len;
            } else {
                result.emplace_back(0, 1, input[cursor]);
                ++cursor;
            }
        }
        return result;
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