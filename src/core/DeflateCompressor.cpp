#include "DeflateCompressor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "Deflate.hpp"
#include "DPFlate.hpp"
#include "Inflate.hpp"
#include "Inflate3HM.hpp"
#include "ICompressor.hpp"
#include "LZDP.hpp"

// === DEBUG_BLOCK_BEGIN (可删除) ===
namespace {
FILE* g_deflate_comp_dbg = nullptr;
void deflate_comp_dbg_open() {
    if (!g_deflate_comp_dbg) {
        g_deflate_comp_dbg = fopen("deflate_compressor_debug.log", "a");
    }
}
void deflate_comp_log(const char* fmt, ...) {
    deflate_comp_dbg_open();
    if (g_deflate_comp_dbg) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_deflate_comp_dbg, fmt, args);
        va_end(args);
        fprintf(g_deflate_comp_dbg, "\n");
        fflush(g_deflate_comp_dbg);
    }
}
}
// === DEBUG_BLOCK_END ===

namespace compressor {
namespace core {

namespace {

template <typename Algo>
auto compress_to_end(Algo& algo, const std::vector<uint8_t>& input,
                     std::vector<uint8_t>& out) -> algorithm::AlgorithmStatus {
    algo.reset();
    size_t in_off = 0;
    size_t out_pos = 0;
    out.resize(std::max(input.size() * 2 + 65536, size_t{4096}));
    algorithm::AlgorithmStatus st{};
    int iter = 0;
    for (;;) {
        iter++;
        if (out_pos >= out.size()) {
            out.resize(std::max(out.size() * 2, out_pos + input.size() + 65536));
        }
        const auto in_span = std::span<const uint8_t>(
            input.data() + in_off, input.size() - in_off);
        const auto out_span =
            std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);
        st = algo.process(in_span, out_span, in_off + in_span.size() >= input.size());
        in_off += st.bytes_consumed;
        out_pos += st.bytes_produced;
        if (st.done) {
            break;
        }
        if (st.need_output && st.bytes_produced == 0) {
            out.resize(std::max(out.size() * 2, out_pos + input.size() + 65536));
            continue;
        }
        if (st.need_input && in_off >= input.size()) {
            break;
        }
    }
    out.resize(out_pos);
    return st;
}

template <typename T>
auto inflate_to_end(T& infl,
                    const std::vector<uint8_t>& compressed,
                    std::vector<uint8_t>& out) -> algorithm::AlgorithmStatus {
    infl.reset();
    size_t in_off = 0;
    size_t out_pos = 0;
    out.resize(std::max(compressed.size() * 10 + 65536, size_t{4096}));
    algorithm::AlgorithmStatus st{};
    for (;;) {
        if (out_pos >= out.size()) {
            out.resize(std::max(out.size() * 2, out_pos + compressed.size() + 65536));
        }
        const auto in_span = std::span<const uint8_t>(
            compressed.data() + in_off, compressed.size() - in_off);
        const auto out_span =
            std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);
        st = infl.process(in_span, out_span, true);
        in_off += st.bytes_consumed;
        out_pos += st.bytes_produced;
        if (st.done) {
            break;
        }
        if (st.need_output && st.bytes_produced == 0) {
            out.resize(std::max(out.size() * 2, out_pos + compressed.size() + 65536));
            continue;
        }
        if (st.need_input && in_off >= compressed.size()) {
            break;
        }
    }
    out.resize(out_pos);
    return st;
}

}  // namespace

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

        std::vector<uint8_t> out;
        auto status = compress_to_end(dpflate, original_data, out);
        result.data = std::move(out);
        result.success = status.done;
        if (!status.done) {
            result.error_message = "DPFlate/3HfM compression incomplete";
        }
    } else {
        const size_t look =
            lookahead_size_ == 0 ? size_t{258} : lookahead_size_;
        algorithm::Deflate deflate(slide_size_, min_match_ == 0 ? 3 : min_match_,
                                   max_chain_length_, look, use_flag_encoding_);

        std::vector<uint8_t> out;
        auto status = compress_to_end(deflate, original_data, out);
        result.data = std::move(out);
        result.success = status.done;
        if (!status.done) {
            result.error_message = "Deflate compression incomplete";
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

    return result;
}

auto DeflateCompressor::decompress(std::vector<uint8_t> compressed_data)
    -> CompressorResult {
    CompressorResult result;
    result.compressed_size = compressed_data.size();

    deflate_comp_log("[DeflateCompressor] decompress start: compressed_size=%zu use_3hfmtree=%d",
                     compressed_data.size(), use_3hfmtree_);

    auto start_time = std::chrono::high_resolution_clock::now();

    if (use_3hfmtree_) {
        deflate_comp_log("[DeflateCompressor] decompress 3HfMT path: checking format byte");
        std::vector<uint8_t> payload;
        if (compressed_data.size() >= 1) {
            uint8_t fmt = compressed_data[0];
            deflate_comp_log("[DeflateCompressor] decompress 3HfMT: format_byte=0x%02x", fmt);
            payload.assign(compressed_data.begin() + 1, compressed_data.end());
        } else {
            payload = compressed_data;
        }

        algorithm::Inflate3HM inflate3hm;
        std::vector<uint8_t> out;
        deflate_comp_log("[DeflateCompressor] decompress 3HfMT: calling inflate_to_end with payload_size=%zu",
                         payload.size());
        auto status = inflate_to_end(inflate3hm, payload, out);
        result.data = std::move(out);

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

        result.original_size = result.data.size();
        result.time_ms = elapsed.count();
        result.success = status.done;
        deflate_comp_log("[DeflateCompressor] decompress 3HfMT done: dec_size=%zu success=%d done=%d",
                         result.original_size, result.success, status.done);
        if (!status.done) {
            result.error_message = "DPFlate/3HfM decompression incomplete";
        }
    } else {
        deflate_comp_log("[DeflateCompressor] decompress standard Deflate path");
        if (!use_flag_encoding_ && compressed_data.size() >= 1 && compressed_data[0] == 0x4E) {
            deflate_comp_log("[DeflateCompressor] decompress non-flag Deflate path");
            std::vector<uint8_t> payload(compressed_data.begin() + 1, compressed_data.end());
            algorithm::LZDPDecompress_Streaming lzdp_dec;
            std::vector<uint8_t> out;
            auto status = inflate_to_end(lzdp_dec, payload, out);
            result.data = std::move(out);

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

            result.original_size = result.data.size();
            result.time_ms = elapsed.count();
            result.success = status.done;

            deflate_comp_log("[DeflateCompressor] decompress non-flag done: dec_size=%zu success=%d done=%d",
                             result.original_size, result.success, status.done);

            if (!status.done) {
                result.error_message = "Deflate non-flag decompression incomplete";
            }
        } else {
            algorithm::Inflate inflate;
            std::vector<uint8_t> out;
            auto status = inflate_to_end(inflate, compressed_data, out);
            result.data = std::move(out);

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

            result.original_size = result.data.size();
            result.time_ms = elapsed.count();
            result.success = status.done;

            deflate_comp_log("[DeflateCompressor] decompress standard done: dec_size=%zu success=%d done=%d bytes_consumed=%zu bytes_produced=%zu",
                             result.original_size, result.success, status.done,
                             status.bytes_consumed, status.bytes_produced);

            if (!status.done) {
                result.error_message = "Inflate decompression incomplete";
            }
        }
    }

    return result;
}

}  // namespace core
}  // namespace compressor
