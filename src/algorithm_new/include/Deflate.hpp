#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "HuffmanTree3HM.hpp"
#include "InflateCoding.hpp"
#include "Inflate3HMCoding.hpp"
#include "LZencoding.hpp"
#include "MatchEngine.hpp"
#include "config/Config.hpp"
#include "Utils.hpp"

namespace compressor::algorithm {

struct DeflateConfig {
    config::Lz77WindowConfig window;
    EncodingConfig encoding;
    bool use_3hfmtree{false};
    HuffmanTree3HMConfig huffman_3hm;

    DeflateConfig(
        size_t sw = 32768,
        size_t lw = 258,
        size_t mcl = 256,
        bool flag_encoding = true,
        bool use_3hm = false)
        : window{sw, lw, 3, mcl},
          encoding{static_cast<uint8_t>(utils::calcBitWidth(sw)),
                   static_cast<uint8_t>(utils::calcBitWidth(lw)),
                   flag_encoding},
          use_3hfmtree(use_3hm) {
              huffman_3hm.max_offset_bits = utils::calcBitWidth(sw);
              huffman_3hm.max_length_bits = utils::calcBitWidth(lw);
          }
};

struct DeflateResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

inline DeflateResult deflate_compress(
    const std::vector<uint8_t>& input,
    const DeflateConfig& config) {
    DeflateResult result;
    if (input.empty()) return result;

    auto triples = LZMatcher::greedyWholeInput(
        input, config.window,
        config.encoding.offset_bits,
        config.encoding.length_bits);

    if (!config.encoding.use_flag_encoding && !config.use_3hfmtree) {
        triples = literalrun(triples, config.window.look_size);
    }

    result.triples = triples;

    compressor::utils::_buffer pending;
    if (config.use_3hfmtree) {
        auto enc = inflate3hm_encode(triples, config.huffman_3hm, pending);
        result.compressed = std::move(enc.data);
    } else if (!config.encoding.use_flag_encoding) {
        result.compressed = encoding_triple_lz(triples, config.encoding, pending, true);
    } else {
        auto enc = inflate_encode(triples, pending);
        result.compressed = std::move(enc.data);
    }
    return result;
}

inline std::vector<uint8_t> deflate_decompress(
    const std::vector<uint8_t>& compressed,
    const DeflateConfig& config) {
    compressor::utils::_buffer pending;
    if (config.use_3hfmtree) {
        auto dec = inflate3hm_decode(compressed, pending);
        return std::move(dec.data);
    }
    if (!config.encoding.use_flag_encoding) {
        auto triples = readtriple(compressed, config.encoding, pending);
        return decode_triple(triples, config.encoding, pending);
    }
    auto dec = inflate_decode(compressed, pending);
    return std::move(dec.data);
}

struct DeflateNonStreamingResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

DeflateNonStreamingResult compress_bytes_deflate(
    const std::vector<uint8_t>& input,
    const DeflateConfig& config);

std::vector<uint8_t> decompress_bytes_deflate(
    const std::vector<uint8_t>& compressed,
    const DeflateConfig& config);

}  // namespace compressor::algorithm

namespace compressor::algorithm::pipeline {

struct DeflateStreamingOptions {
    size_t chunk_size{300 * 1024};
    std::string workspace_dir;
    std::string temp_triples_name{"temp_deflate.tri"};
};

class DeflateStreamingPipeline {
public:
    DeflateStreamingPipeline(DeflateConfig config, DeflateStreamingOptions options);
    void compress_file(const std::string& input_path, const std::string& output_path);
private:
    DeflateConfig config_;
    DeflateStreamingOptions options_;
};

}  // namespace compressor::algorithm::pipeline