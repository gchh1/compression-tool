#include "DPFlateCompressor.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "DebugLog.hpp"
#include "DPFlate.hpp"
#include "ICompressor.hpp"
#include "Inflate.hpp"
#include "Inflate3HM.hpp"
#include "LZDP.hpp"
#include "StreamProcessor.hpp"

namespace compressor {
namespace core {

namespace {

template <typename T>
auto compress_to_end(T& enc, const std::vector<uint8_t>& input,
                     std::vector<uint8_t>& out) -> algorithm::AlgorithmStatus {
    enc.reset();
    size_t in_off = 0;
    size_t out_pos = 0;
    out.resize(std::max(input.size() * 2 + 65536, size_t{4096}));
    algorithm::AlgorithmStatus st{};

    for (;;) {
        if (out_pos >= out.size()) {
            out.resize(std::max(out.size() * 2, out_pos + input.size() + 65536));
        }

        const auto in_span = std::span<const uint8_t>(
            input.data() + in_off, input.size() - in_off);
        const auto out_span =
            std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);

        st = enc.process(in_span, out_span, in_off + in_span.size() >= input.size());
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
auto decompress_to_end(T& dec, const std::vector<uint8_t>& compressed,
                       std::vector<uint8_t>& out) -> algorithm::AlgorithmStatus {
    dec.reset();
    size_t in_off = 0;
    size_t out_pos = 0;
    out.resize(std::max(compressed.size() * 10 + 65536, size_t{4096}));
    algorithm::AlgorithmStatus st{};
    int iter = 0;
    for (;;) {
        ++iter;
        if (out_pos >= out.size()) {
            out.resize(std::max(out.size() * 2, out_pos + compressed.size() + 65536));
        }
        const auto in_span = std::span<const uint8_t>(
            compressed.data() + in_off, compressed.size() - in_off);
        const auto out_span =
            std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);
        st = dec.process(in_span, out_span, true);
        in_off += st.bytes_consumed;
        out_pos += st.bytes_produced;
        DEBUG_LOG("[DPFlateCompressor] decompress_to_end iter=%d in_off=%zu/%zu out_pos=%zu done=%d need_input=%d need_output=%d consumed=%zu produced=%zu",
                  iter, in_off, compressed.size(), out_pos, st.done, st.need_input, st.need_output,
                  st.bytes_consumed, st.bytes_produced);
        if (st.done) {
            DEBUG_LOG("[DPFlateCompressor] decompress_to_end: done after %d iterations, final out_pos=%zu", iter, out_pos);
            break;
        }
        if (st.need_output && st.bytes_produced == 0) {
            out.resize(std::max(out.size() * 2, out_pos + compressed.size() + 65536));
            continue;
        }
        if (st.need_input && in_off >= compressed.size()) {
            DEBUG_LOG("[DPFlateCompressor] decompress_to_end: breaking (need_input && exhausted) after %d iterations, out_pos=%zu",
                      iter, out_pos);
            break;
        }
        if (iter > 10000) {
            DEBUG_LOG("[DPFlateCompressor] decompress_to_end: SAFETY BREAK after %d iterations, out_pos=%zu",
                      iter, out_pos);
            break;
        }
    }
    out.resize(out_pos);
    return st;
}

}  // namespace

auto DPFlateCompressor::compress(std::vector<uint8_t> original_data)
    -> CompressorResult {
    CompressorResult result;
    result.original_size = original_data.size();

    DEBUG_LOG("[DPFlateCompressor] compress start: original_size=%zu use_3hfmtree=%d",
              original_data.size(), use_3hfmtree_);

    auto start_time = std::chrono::high_resolution_clock::now();

    algorithm::DPFlate dpflate(search_size_, lookahead_size_,
                               min_match_ == 0 ? 4 : min_match_,
                               max_chain_length_, dp_sub_match_max_);
    dpflate.set_match_engine(match_engine_);
    dpflate.set_use_flag_encoding(use_flag_encoding_);
    dpflate.set_use_3hfmtree(use_3hfmtree_);
    dpflate.set_huffman_offset_chunk_bits(huffman_offset_chunk_bits_);
    dpflate.set_huffman_length_chunk_bits(huffman_length_chunk_bits_);
    std::vector<uint8_t> out;
    auto status = compress_to_end(dpflate, original_data, out);
    result.data = std::move(out);

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    result.compressed_size = result.data.size();
    result.time_ms = elapsed.count();

    if (result.original_size > 0) {
        result.compression_ratio =
            static_cast<double>(result.compressed_size) / result.original_size;
    }
    result.success = status.done;
    if (!status.done) {
        result.error_message = "DPFlate compression incomplete";
    }

    return result;
}

auto DPFlateCompressor::decompress(std::vector<uint8_t> compressed_data)
    -> CompressorResult {
    CompressorResult result;
    result.compressed_size = compressed_data.size();

    DEBUG_LOG("[DPFlateCompressor] decompress start: compressed_size=%zu",
              compressed_data.size());

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<uint8_t> out;
    algorithm::AlgorithmStatus status{};

    if (compressed_data.size() >= 1) {
        const uint8_t fmt = compressed_data[0];
        DEBUG_LOG("[DPFlateCompressor] decompress: format_byte=0x%02x (0x33=3HM, 0x46=Inflate)", fmt);
        if (fmt == 0x33) {
            algorithm::Inflate3HM dec;
            std::vector<uint8_t> payload(compressed_data.begin() + 1, compressed_data.end());
            status = decompress_to_end(dec, payload, out);
        } else if (fmt == 0x46) {
            algorithm::Inflate dec;
            std::vector<uint8_t> payload(compressed_data.begin() + 1, compressed_data.end());
            status = decompress_to_end(dec, payload, out);
        } else {
            algorithm::Inflate dec;
            status = decompress_to_end(dec, compressed_data, out);
        }
    } else {
        algorithm::Inflate dec;
        status = decompress_to_end(dec, compressed_data, out);
    }

    result.data = std::move(out);

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    result.original_size = result.data.size();
    result.time_ms = elapsed.count();
    result.success = status.done;

    if (!status.done) {
        result.error_message = "Decompression incomplete";
    }

    return result;
}

}  // namespace core
}  // namespace compressor