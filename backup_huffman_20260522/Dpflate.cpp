#include "Dpflate.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include "EncodingTriple.hpp"
#include "Huffman_3HfMTree.hpp"
#include "Huffman_Inflate.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"
#include "LZDP.hpp"
#include "StreamingCancel.hpp"

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

DPFlateNonStreamingResult compress_bytes_dpflate(
    const std::vector<uint8_t>& input,
    const DPflateConfig& config) {
    DPFlateNonStreamingResult result;
    if (input.empty()) return result;

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    auto lzdp_cfg = config.to_lzdp_config();
    LZDP lzdp(lzdp_cfg);

    std::vector<models::DPNode> dp;
    dp.assign(input.size() + 1, models::DPNode{});
    if (!input.empty()) {
        dp[0] = models::DPNode(0, 0, -1, Triple(0, 0, 0));
    }

    VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp, 0, input.size());

    int cur_pos = 0;
    std::vector<Triple> triples = lzdp.dpbacktrack(dp, cur_pos, 0);

    if (config.encoding.use_flag_encoding) {
        if (config.huffman.use_3hfmtree) {
            Huffman_3HfMTConfig hmcfg{
                config.huffman.huffman_offset_bitwidth,
                config.huffman.huffman_length_bitwidth};
            Huffman_3HfMT encoder(hmcfg, config.encoding);
            encoder.countFreq(triples);
            encoder.buildTree();
            result.compressed = encoder.encode(triples);
        } else {
            Huffman_InflateConfig icfg{
                config.encoding.offset_bits,
                config.encoding.length_bits,
                config.huffman.huffman_offset_bitwidth,
                config.huffman.huffman_length_bitwidth};
            Huffman_Inflate encoder(icfg, config.encoding);
            encoder.countFreq(triples);
            encoder.buildTree();
            result.compressed = encoder.encode(triples);
        }
    } else {
        auto run_triples = literalrun(triples, config.window.look_size);
        compressor::utils::_buffer pending;
        result.compressed = encoding_triple_lz(run_triples, config.encoding, pending, true);
    }

    result.triples = std::move(triples);
    return result;
}

std::vector<uint8_t> decompress_bytes_dpflate(
    const std::vector<uint8_t>& compressed,
    const DPflateConfig& config) {
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<Triple> triples;
    compressor::utils::_buffer pending;

    if (config.encoding.use_flag_encoding) {
        if (config.huffman.use_3hfmtree) {
            Huffman_3HfMTConfig hmcfg{
                config.huffman.huffman_offset_bitwidth,
                config.huffman.huffman_length_bitwidth};
            Huffman_3HfMT decoder(hmcfg, config.encoding);
            triples = decoder.decode(compressed);
        } else {
            Huffman_InflateConfig icfg{
                config.encoding.offset_bits,
                config.encoding.length_bits,
                config.huffman.huffman_offset_bitwidth,
                config.huffman.huffman_length_bitwidth};
            Huffman_Inflate decoder(icfg, config.encoding);
            triples = decoder.decode(compressed);
        }
    } else {
        triples = readtriple(compressed, config.encoding, pending);
    }

    return decode_triple(triples, config.encoding, pending);
}

DPFlateStreamingPipeline::DPFlateStreamingPipeline(DPflateConfig config,
                                                     DPFlateStreamingOptions opts)
    : config_(std::move(config)), opts_(std::move(opts)), dpflate_(config_) {}

std::string DPFlateStreamingPipeline::temp_a_path() const {
    return (fs::path(opts_.workspace_dir) / opts_.temp_a_name).string();
}

std::string DPFlateStreamingPipeline::temp_b_path() const {
    return (fs::path(opts_.workspace_dir) / opts_.temp_b_name).string();
}

void DPFlateStreamingPipeline::compress_file(const std::string& input_path,
                                              const std::string& output_path) {
    fs::create_directories(opts_.workspace_dir);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    auto lzdp_cfg = config_.to_lzdp_config();

    const Phase1Result phase1 = run_phase1_dpforward(
        dpflate_.lzdp(), lzdp_cfg, input_path, temp_a_path(), opts_.chunk_size);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    const Phase2Result phase2 = run_phase2_dpbacktrack(
        dpflate_.lzdp(), phase1, temp_a_path(), temp_b_path(), opts_.chunk_size);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<uint8_t> encoded;

    if (config_.encoding.use_flag_encoding) {
        if (config_.huffman.use_3hfmtree) {
            Huffman_3HfMTConfig hmcfg{
                config_.huffman.huffman_offset_bitwidth,
                config_.huffman.huffman_length_bitwidth};
            Huffman_3HfMT encoder(hmcfg, config_.encoding);

            encoder.countFreq(phase2.triples);
            encoder.buildTree();
            encoded = encoder.encode(phase2.triples);
        } else {
            Huffman_InflateConfig icfg{
                config_.encoding.offset_bits,
                config_.encoding.length_bits,
                config_.huffman.huffman_offset_bitwidth,
                config_.huffman.huffman_length_bitwidth};
            Huffman_Inflate encoder(icfg, config_.encoding);

            encoder.countFreq(phase2.triples);
            encoder.buildTree();
            encoded = encoder.encode(phase2.triples);
        }
    } else {
        auto run_triples = literalrun(phase2.triples, config_.window.look_size);
        compressor::utils::_buffer ebuf;
        encoded = encoding_triple_lz(run_triples, config_.encoding, ebuf, true);
    }

    {
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("DPFlateStreamingPipeline: cannot open " + output_path);
        }
        if (!encoded.empty()) {
            out.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));
        }
    }
}

}  // namespace compressor::algorithm::pipeline