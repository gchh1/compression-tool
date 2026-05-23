#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "config/Config.hpp"
#include "Huffman_3HfMTree.hpp"
#include "Huffman_Inflate.hpp"
#include "LZencoding.hpp"
#include "Utils.hpp"

namespace compressor::algorithm {

struct DeflateConfig {
    config::Lz77WindowConfig window;
    config::HuffmanBackendConfig huffman;
    EncodingConfig encoding;

    DeflateConfig(
        size_t sw = 16384,
        size_t lw = 258,
        size_t mml = 3,
        bool use3hfm = false,
        uint8_t off_chunk_bits = 8,
        uint8_t len_chunk_bits = 8,
        bool use_flag = true,
        size_t mcl = 256)
        : window{sw, lw, mml, mcl},
          huffman{use3hfm, off_chunk_bits, len_chunk_bits},
          encoding{static_cast<uint8_t>(utils::calcBitWidth(sw)),
                   static_cast<uint8_t>(utils::calcBitWidth(lw)),
                   use_flag} {
        if (window.min_match_len == 0) {
            window.min_match_len =
                utils::getMinMatch(encoding.offset_bits, encoding.length_bits);
        }
    }
};

class Deflate {
public:
    explicit Deflate(const DeflateConfig& cfg = DeflateConfig{}) : config_(cfg) {}

    const DeflateConfig& getConfig() const { return config_; }

    /// §1.6 熵编码层 only；LZ 匹配见 ``LZMatcher::greedyWholeInput`` / ``matchAtPosition``（§1.14）。
    std::vector<uint8_t> huffmanEncode(const std::vector<Triple>& triples) const {
        if (config_.huffman.use_3hfmtree) {
            Huffman_3HfMTConfig hmcfg{
                config_.huffman.huffman_offset_bitwidth,
                config_.huffman.huffman_length_bitwidth};
            Huffman_3HfMT encoder(hmcfg, config_.encoding);
            encoder.countFreq(triples);
            encoder.buildTree();
            return encoder.encode(triples);
        } else {
            Huffman_InflateConfig icfg{
                config_.encoding.offset_bits,
                config_.encoding.length_bits,
                config_.huffman.huffman_offset_bitwidth,
                config_.huffman.huffman_length_bitwidth};
            Huffman_Inflate encoder(icfg, config_.encoding);
            encoder.countFreq(triples);
            encoder.buildTree();
            return encoder.encode(triples);
        }
    }

    std::vector<Triple> huffmanDecode(const std::vector<uint8_t>& data) const {
        if (config_.huffman.use_3hfmtree) {
            Huffman_3HfMTConfig hmcfg{
                config_.huffman.huffman_offset_bitwidth,
                config_.huffman.huffman_length_bitwidth};
            Huffman_3HfMT decoder(hmcfg, config_.encoding);
            return decoder.decode(data);
        } else {
            Huffman_InflateConfig icfg{
                config_.encoding.offset_bits,
                config_.encoding.length_bits,
                config_.huffman.huffman_offset_bitwidth,
                config_.huffman.huffman_length_bitwidth};
            Huffman_Inflate decoder(icfg, config_.encoding);
            return decoder.decode(data);
        }
    }

private:
    DeflateConfig config_;
};

}  // namespace compressor::algorithm

// ──── Pipeline ────

#include <string>

namespace compressor::algorithm::pipeline {

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

struct DeflateStreamingOptions {
    size_t chunk_size{1 << 20};
    std::string workspace_dir;
    std::string temp_a_name{"temp_deflate.dp"};
};

class DeflateStreamingPipeline {
public:
    explicit DeflateStreamingPipeline(DeflateConfig config, DeflateStreamingOptions options);

    void compress_file(const std::string& input_path, const std::string& output_path);

private:
    DeflateConfig config_;
    DeflateStreamingOptions options_;

    std::string temp_a_path() const;
};

}  // namespace compressor::algorithm::pipeline