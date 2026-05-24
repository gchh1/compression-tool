#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "HuffmanTree3HM.hpp"
#include "InflateCoding.hpp"
#include "Inflate3HMCoding.hpp"
#include "LZDP.hpp"
#include "LZencoding.hpp"
#include "config/Config.hpp"
#include "Utils.hpp"

namespace compressor::algorithm {

struct DPFlateConfig {
    LZDPConfig lzdp;
    EncodingConfig encoding;
    bool use_3hfmtree{false};
    HuffmanTree3HMConfig huffman_3hm;

    DPFlateConfig(
        size_t sw = 4095,
        size_t lw = 255,
        size_t dp_top = 3,
        bool flag_encoding = false,
        bool use_3hm = false,
        size_t mcl = 256,
        size_t mml = 0)
        : lzdp{sw, lw, dp_top, models::MatchEngine::HashChain, flag_encoding, mml, mcl},
          encoding{static_cast<uint8_t>(utils::calcBitWidth(sw)),
                   static_cast<uint8_t>(utils::calcBitWidth(lw)),
                   flag_encoding},
          use_3hfmtree(use_3hm) {
            huffman_3hm.max_offset_bits = utils::calcBitWidth(sw);
            huffman_3hm.max_length_bits = utils::calcBitWidth(lw);
            huffman_3hm.chunk_bits = 8;
            huffman_3hm.offset_range = 256;
            huffman_3hm.length_range = 256;
        }
};

struct DPFlateResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

std::vector<uint8_t> encode_flate_huffman(const std::vector<Triple>& triples);
std::vector<uint8_t> decode_flate_huffman(const std::vector<uint8_t>& compressed);
std::vector<uint8_t> encode_3hm_huffman(const std::vector<Triple>& triples,
                                          const HuffmanTree3HMConfig& cfg);
std::vector<uint8_t> decode_3hm_huffman(const std::vector<uint8_t>& compressed);

std::vector<uint8_t> encode_demo_huffman(const std::vector<Triple>& triples,
                                          uint32_t offset_bits, uint32_t length_bits,
                                          uint32_t offset_chunk_bits, uint32_t length_chunk_bits);

inline DPFlateResult dpflate_compress(
    const std::vector<uint8_t>& input,
    const DPFlateConfig& config) {
    DPFlateResult result;
    if (input.empty()) return result;

    std::vector<compressor::algorithm::models::DPNode> dp_nodes(input.size() + 1,
        compressor::algorithm::models::DPNode(0, 0, -2));
    dp_nodes[0] = compressor::algorithm::models::DPNode(0, 0, -1,
        compressor::algorithm::Triple(0, 0, 0));

    LZDP lzdp(config.lzdp);
    VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp_nodes, 0, input.size(), input.size());

    int cur_pos = 0;
    std::vector<Triple> triples = lzdp.dpbacktrack(dp_nodes, cur_pos, 0);

    result.triples = triples;

    if (config.use_3hfmtree) {
        result.compressed = encode_3hm_huffman(triples, config.huffman_3hm);
    } else {
        result.compressed = encode_flate_huffman(triples);
    }
    return result;
}

inline std::vector<uint8_t> dpflate_decompress(
    const std::vector<uint8_t>& compressed,
    const DPFlateConfig& config) {
    if (config.use_3hfmtree) {
        return decode_3hm_huffman(compressed);
    }
    return decode_flate_huffman(compressed);
}

struct DPFlateNonStreamingResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

DPFlateNonStreamingResult compress_bytes_dpflate(
    const std::vector<uint8_t>& input,
    const DPFlateConfig& config);

std::vector<uint8_t> decompress_bytes_dpflate(
    const std::vector<uint8_t>& compressed,
    const DPFlateConfig& config);

}  // namespace compressor::algorithm

namespace compressor::algorithm::pipeline {

struct DPFlateStreamingOptions {
    size_t chunk_size{300 * 1024};
    std::string workspace_dir;
    std::string temp_a_name{"temp_a_dpf.dp"};
    std::string temp_b_name{"temp_b_dpf.tok"};
};

class DPFlateStreamingPipeline {
public:
    DPFlateStreamingPipeline(DPFlateConfig config, DPFlateStreamingOptions opts);
    void compress_file(const std::string& input_path, const std::string& output_path);
private:
    std::string temp_a_path() const;
    std::string temp_b_path() const;
    DPFlateConfig config_;
    DPFlateStreamingOptions opts_;
};

}  // namespace compressor::algorithm::pipeline