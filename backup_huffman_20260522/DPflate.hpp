#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "LZDP.hpp"
#include "LZencoding.hpp"
#include "config/Config.hpp"

namespace compressor::algorithm {

struct DPflateConfig {
    config::Lz77WindowConfig window;
    config::DpMatcherConfig dp;
    config::HuffmanBackendConfig huffman;
    EncodingConfig encoding;
    size_t min_match_len;

    DPflateConfig(
        size_t sw = 4095,
        size_t lw = 255,
        size_t dp_top = 3,
        models::MatchEngine me = models::MatchEngine::HashChain,
        bool flag = true,
        bool use3hfm = false,
        uint8_t off_chunk_bits = 8,
        uint8_t len_chunk_bits = 8,
        size_t mml = 0,
        size_t mcl = 256)
        : window{sw, lw, mml, mcl},
          dp{dp_top, me},
          huffman{use3hfm, off_chunk_bits, len_chunk_bits},
          encoding{static_cast<uint8_t>(utils::calcBitWidth(sw)),
                   static_cast<uint8_t>(utils::calcBitWidth(lw)),
                   flag},
          min_match_len(mml) {
        if (min_match_len == 0) {
            min_match_len = utils::getMinMatch(encoding.offset_bits,
                                                encoding.length_bits);
        }
    }

    LZDPConfig to_lzdp_config() const {
        return LZDPConfig{window.search_size, window.look_size, dp.dp_top,
                          dp.match_engine, encoding.use_flag_encoding,
                          min_match_len, window.max_chain_length};
    }
};

class DPflate {
    DPflateConfig config_;
    LZDP lzdp_;

public:
    explicit DPflate(const DPflateConfig& cfg = DPflateConfig{})
        : config_(cfg), lzdp_(cfg.to_lzdp_config()) {}

    const DPflateConfig& getConfig() const { return config_; }
    LZDP& lzdp() { return lzdp_; }
    const LZDP& lzdp() const { return lzdp_; }
};

}  // namespace compressor::algorithm

// ──── Pipeline ────

#include <string>

namespace compressor::algorithm::pipeline {

struct DPFlateNonStreamingResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

DPFlateNonStreamingResult compress_bytes_dpflate(
    const std::vector<uint8_t>& input,
    const DPflateConfig& config);

std::vector<uint8_t> decompress_bytes_dpflate(
    const std::vector<uint8_t>& compressed,
    const DPflateConfig& config);

struct DPFlateStreamingOptions {
    size_t chunk_size{1 << 20};
    std::string workspace_dir;
    std::string temp_a_name{"temp_a_dpf.dp"};
    std::string temp_b_name{"temp_b_dpf.tok"};
};

class DPFlateStreamingPipeline {
public:
    DPFlateStreamingPipeline(DPflateConfig config, DPFlateStreamingOptions opts);

    void compress_file(const std::string& input_path,
                       const std::string& output_path);

private:
    DPflateConfig config_;
    DPFlateStreamingOptions opts_;
    DPflate dpflate_;

    std::string temp_a_path() const;
    std::string temp_b_path() const;
};

}  // namespace compressor::algorithm::pipeline