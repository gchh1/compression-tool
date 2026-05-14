#include "DeflateCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "Deflate.hpp"
#include "DPFlate.hpp"
#include "Inflate.hpp"
#include "ICompressor.hpp"

namespace compressor {
namespace core {

auto DeflateCompressor::compress(std::vector<uint8_t> original_data)
    -> CompressorResult {
    CompressorResult result;
    result.original_size = original_data.size();

    auto start_time = std::chrono::high_resolution_clock::now();

    if (use_3hfmtree_) {
        algorithm::DPFlate dpflate(
            slide_size_, lookahead_size_, min_match_ == 0 ? 4 : min_match_,
            max_chain_length_, dp_sub_match_max_);
        dpflate.set_match_engine(match_engine_);
        dpflate.set_use_flag_encoding(use_flag_encoding_);
        dpflate.set_use_3hfmtree(true);
        dpflate.set_huffman_offset_chunk_bits(huffman_offset_chunk_bits_);
        dpflate.set_huffman_length_chunk_bits(huffman_length_chunk_bits_);
        dpflate.reset();

        size_t out_capacity = original_data.size() * 2 + 65536;
        if (out_capacity < 4096) out_capacity = 4096;

        std::vector<uint8_t> out(out_capacity);
        auto status = dpflate.process(original_data, out, true);

        result.data.resize(status.bytes_produced);
        if (status.bytes_produced > 0) {
            std::copy_n(out.begin(), status.bytes_produced, result.data.begin());
        }
    } else {
        const size_t look =
            lookahead_size_ == 0 ? size_t{258} : lookahead_size_;
        algorithm::Deflate deflate(slide_size_, min_match_ == 0 ? 3 : min_match_,
                                   max_chain_length_, look);
        deflate.reset();

        size_t out_capacity = original_data.size() * 2 + 65536;
        if (out_capacity < 4096) out_capacity = 4096;

        std::vector<uint8_t> out(out_capacity);
        auto status = deflate.process(original_data, out, true);

        result.data.resize(status.bytes_produced);
        if (status.bytes_produced > 0) {
            std::copy_n(out.begin(), status.bytes_produced, result.data.begin());
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    result.compressed_size = result.data.size();
    result.time_ms = elapsed.count();

    if (result.original_size > 0) {
        result.compression_ratio =
            static_cast<double>(result.compressed_size) / result.original_size;
    }
    result.success = true;

    return result;
}

auto DeflateCompressor::decompress(std::vector<uint8_t> compressed_data)
    -> CompressorResult {
    CompressorResult result;
    result.compressed_size = compressed_data.size();

    auto start_time = std::chrono::high_resolution_clock::now();

    if (use_3hfmtree_) {
        algorithm::DPFlateDecompress inflate;
        inflate.reset();

        size_t out_capacity = compressed_data.size() * 10 + 65536;
        if (out_capacity < 4096) out_capacity = 4096;

        std::vector<uint8_t> out(out_capacity);
        auto status = inflate.process(compressed_data, out, true);

        result.data.resize(status.bytes_produced);
        if (status.bytes_produced > 0) {
            std::copy_n(out.begin(), status.bytes_produced, result.data.begin());
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

        result.original_size = result.data.size();
        result.time_ms = elapsed.count();
        result.success = status.done;
        if (!status.done) {
            result.error_message = "DPFlate/3HfM decompression incomplete";
        }
    } else {
        algorithm::Inflate inflate;
        inflate.reset();

        size_t out_capacity = compressed_data.size() * 10 + 65536;
        if (out_capacity < 4096) out_capacity = 4096;

        std::vector<uint8_t> out(out_capacity);
        auto status = inflate.process(compressed_data, out, true);

        result.data.resize(status.bytes_produced);
        if (status.bytes_produced > 0) {
            std::copy_n(out.begin(), status.bytes_produced, result.data.begin());
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

        result.original_size = result.data.size();
        result.time_ms = elapsed.count();
        result.success = status.done;

        if (!status.done) {
            result.error_message = "Inflate decompression incomplete";
        }
    }

    return result;
}

}  // namespace core
}  // namespace compressor
