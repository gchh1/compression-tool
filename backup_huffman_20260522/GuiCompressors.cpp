#include "GuiCompressors.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

#include "Deflate.hpp"
#include "Dpflate.hpp"
#include "LZSS.hpp"

namespace compressor::core {

namespace {

CompressorResult make_result(std::vector<uint8_t> in, std::vector<uint8_t> out, double ms) {
    CompressorResult r;
    r.data = std::move(out);
    r.original_size = in.size();
    r.compressed_size = r.data.size();
    r.compression_ratio =
        r.original_size > 0 ? static_cast<double>(r.compressed_size) / r.original_size : 0.0;
    r.time_ms = ms;
    r.success = true;
    return r;
}

CompressorResult make_cancelled_result(const std::vector<uint8_t>& in, double ms) {
    CompressorResult r;
    r.original_size = in.size();
    r.compressed_size = 0;
    r.compression_ratio = 0.0;
    r.time_ms = ms;
    r.success = false;
    r.error_message = "cancelled";
    return r;
}

template <typename Fn>
CompressorResult run_compress_job(std::vector<uint8_t> data, Fn&& fn) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    try {
        auto out = fn(data);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return make_result(std::move(data), std::move(out), ms);
    } catch (const std::runtime_error& e) {
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (std::string(e.what()) == "cancelled") {
            return make_cancelled_result(data, ms);
        }
        throw;
    }
}

}  // namespace

// ── LZDP ──

namespace {

algorithm::LZDPConfig lzdp_effective_config(const algorithm::LZDPConfig& cfg) {
    auto out = cfg;
    if (out.window.min_match_len == 0) {
        out.window.min_match_len = algorithm::utils::getMinMatch(
            out.encoding.offset_bits, out.encoding.length_bits);
    }
    return out;
}

}  // namespace

LZDPCompressor::LZDPCompressor() : dp_range_(3) {}

CompressorResult LZDPCompressor::compress(std::vector<uint8_t> data) {
    const auto cfg = lzdp_effective_config(lzdp_);
    return run_compress_job(std::move(data), [&](const std::vector<uint8_t>& in) {
        return algorithm::pipeline::compress_bytes(in, cfg).compressed;
    });
}

CompressorResult LZDPCompressor::decompress(std::vector<uint8_t> data) {
    const auto cfg = lzdp_effective_config(lzdp_);
    auto t0 = std::chrono::high_resolution_clock::now();
    auto out = algorithm::pipeline::decompress_bytes(data, cfg);
    auto t1 = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return make_result(std::move(data), std::move(out), ms);
}

std::string LZDPCompressor::get_algorithm_name() { return "LZDP (algorithm_new)"; }

algorithm::DPVisualization LZDPCompressor::get_dp_visualization(const std::vector<uint8_t>& data,
                                                                size_t range) {
    if (range == 0) {
        range = dp_range_;
    }
    return algorithm::get_dp_visualization(data, lzdp_, range);
}

void LZDPCompressor::set_search_size(size_t v) {
    lzdp_.window.search_size = v;
    lzdp_.encoding.offset_bits = static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
}
size_t LZDPCompressor::get_search_size() const { return lzdp_.window.search_size; }
void LZDPCompressor::set_lookahead_size(size_t v) {
    lzdp_.window.look_size = v;
    lzdp_.encoding.length_bits = static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
}
size_t LZDPCompressor::get_lookahead_size() const { return lzdp_.window.look_size; }
void LZDPCompressor::set_min_match(size_t v) { lzdp_.window.min_match_len = v; }
size_t LZDPCompressor::get_min_match() const {
    return algorithm::utils::effectiveMinMatchLen(
        lzdp_.window.min_match_len,
        lzdp_.encoding.offset_bits,
        lzdp_.encoding.length_bits);
}
void LZDPCompressor::set_dp_top(size_t v) {
    dp_range_ = v;
    lzdp_.dp.dp_top = static_cast<uint8_t>(v);
}
size_t LZDPCompressor::get_dp_top() const { return dp_range_; }
void LZDPCompressor::set_use_flag_encoding(bool v) { lzdp_.encoding.use_flag_encoding = v; }
bool LZDPCompressor::get_use_flag_encoding() const { return lzdp_.encoding.use_flag_encoding; }
void LZDPCompressor::set_match_engine(int v) {
    lzdp_.dp.match_engine =
        v == 0 ? algorithm::models::MatchEngine::KMP : algorithm::models::MatchEngine::HashChain;
}
int LZDPCompressor::get_match_engine() const {
    return lzdp_.dp.match_engine == algorithm::models::MatchEngine::KMP ? 0 : 1;
}

// ── LZSS ──

LZSSCompressor::LZSSCompressor() = default;

CompressorResult LZSSCompressor::compress(std::vector<uint8_t> data) {
    return run_compress_job(std::move(data), [&](const std::vector<uint8_t>& in) {
        return algorithm::pipeline::compress_bytes_lzss(in, lzss_).compressed;
    });
}

CompressorResult LZSSCompressor::decompress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto out = algorithm::pipeline::decompress_bytes_lzss(data, lzss_);
    auto t1 = std::chrono::high_resolution_clock::now();
    return make_result(std::move(data), std::move(out),
                       std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string LZSSCompressor::get_algorithm_name() { return "LZSS (algorithm_new)"; }

void LZSSCompressor::set_search_size(size_t v) {
    lzss_.window.search_size = v;
    lzss_.encoding.offset_bits = static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
}
size_t LZSSCompressor::get_search_size() const { return lzss_.window.search_size; }
void LZSSCompressor::set_lookahead_size(size_t v) {
    lzss_.window.look_size = v;
    lzss_.encoding.length_bits = static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
}
size_t LZSSCompressor::get_lookahead_size() const { return lzss_.window.look_size; }
void LZSSCompressor::set_min_match(size_t v) { lzss_.window.min_match_len = v; }
size_t LZSSCompressor::get_min_match() const {
    return algorithm::utils::effectiveMinMatchLen(
        lzss_.window.min_match_len,
        lzss_.encoding.offset_bits,
        lzss_.encoding.length_bits);
}
void LZSSCompressor::set_use_flag_encoding(bool v) { lzss_.encoding.use_flag_encoding = v; }
bool LZSSCompressor::get_use_flag_encoding() const { return lzss_.encoding.use_flag_encoding; }

// ── Deflate ──

DeflateCompressor::DeflateCompressor() = default;

CompressorResult DeflateCompressor::compress(std::vector<uint8_t> data) {
    return run_compress_job(std::move(data), [&](const std::vector<uint8_t>& in) {
        return algorithm::pipeline::compress_bytes_deflate(in, deflate_).compressed;
    });
}

CompressorResult DeflateCompressor::decompress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto out = algorithm::pipeline::decompress_bytes_deflate(data, deflate_);
    auto t1 = std::chrono::high_resolution_clock::now();
    return make_result(std::move(data), std::move(out),
                       std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string DeflateCompressor::get_algorithm_name() { return "Deflate (algorithm_new)"; }

void DeflateCompressor::set_search_size(size_t v) {
    deflate_.window.search_size = v;
    deflate_.encoding.offset_bits = static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
}
size_t DeflateCompressor::get_search_size() const { return deflate_.window.search_size; }
void DeflateCompressor::set_lookahead_size(size_t v) {
    deflate_.window.look_size = v;
    deflate_.encoding.length_bits = static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
}
size_t DeflateCompressor::get_lookahead_size() const { return deflate_.window.look_size; }
void DeflateCompressor::set_min_match(size_t v) { deflate_.window.min_match_len = v; }
size_t DeflateCompressor::get_min_match() const {
    return algorithm::utils::effectiveMinMatchLen(
        deflate_.window.min_match_len,
        deflate_.encoding.offset_bits,
        deflate_.encoding.length_bits);
}
void DeflateCompressor::set_max_chain_length(size_t v) { deflate_.window.max_chain_length = v; }
size_t DeflateCompressor::get_max_chain_length() const { return deflate_.window.max_chain_length; }
void DeflateCompressor::set_use_3hfmtree(bool v) { deflate_.huffman.use_3hfmtree = v; }
bool DeflateCompressor::get_use_3hfmtree() const { return deflate_.huffman.use_3hfmtree; }
void DeflateCompressor::set_huffman_chunk_bits(size_t k) {
    deflate_.huffman.huffman_offset_bitwidth = static_cast<uint8_t>(k);
    deflate_.huffman.huffman_length_bitwidth = static_cast<uint8_t>(k);
}
size_t DeflateCompressor::get_huffman_chunk_bits() const {
    return deflate_.huffman.huffman_offset_bitwidth;
}
void DeflateCompressor::set_huffman_offset_chunk_bits(size_t k) {
    deflate_.huffman.huffman_offset_bitwidth = static_cast<uint8_t>(k);
}
void DeflateCompressor::set_huffman_length_chunk_bits(size_t k) {
    deflate_.huffman.huffman_length_bitwidth = static_cast<uint8_t>(k);
}
size_t DeflateCompressor::get_huffman_offset_chunk_bits() const {
    return deflate_.huffman.huffman_offset_bitwidth;
}
size_t DeflateCompressor::get_huffman_length_chunk_bits() const {
    return deflate_.huffman.huffman_length_bitwidth;
}
void DeflateCompressor::set_dp_sub_match_max(size_t) {}
size_t DeflateCompressor::get_dp_sub_match_max() const { return 6; }
void DeflateCompressor::set_match_engine(int) {}
int DeflateCompressor::get_match_engine() const { return 1; }
void DeflateCompressor::set_use_flag_encoding(bool v) { deflate_.encoding.use_flag_encoding = v; }
bool DeflateCompressor::get_use_flag_encoding() const { return deflate_.encoding.use_flag_encoding; }

// ── DPFlate ──

DPFlateCompressor::DPFlateCompressor() = default;

CompressorResult DPFlateCompressor::compress(std::vector<uint8_t> data) {
    return run_compress_job(std::move(data), [&](const std::vector<uint8_t>& in) {
        return algorithm::pipeline::compress_bytes_dpflate(in, dpflate_).compressed;
    });
}

CompressorResult DPFlateCompressor::decompress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto out = algorithm::pipeline::decompress_bytes_dpflate(data, dpflate_);
    auto t1 = std::chrono::high_resolution_clock::now();
    return make_result(std::move(data), std::move(out),
                       std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string DPFlateCompressor::get_algorithm_name() { return "DPFlate (algorithm_new)"; }

void DPFlateCompressor::set_search_size(size_t v) {
    dpflate_.window.search_size = v;
    dpflate_.encoding.offset_bits =
        static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
    if (dpflate_.min_match_len == 0) {
        dpflate_.min_match_len = algorithm::utils::getMinMatch(
            dpflate_.encoding.offset_bits, dpflate_.encoding.length_bits);
    }
}
size_t DPFlateCompressor::get_search_size() const { return dpflate_.window.search_size; }
void DPFlateCompressor::set_lookahead_size(size_t v) {
    dpflate_.window.look_size = v;
    dpflate_.encoding.length_bits =
        static_cast<uint8_t>(algorithm::utils::calcBitWidth(v));
    if (dpflate_.min_match_len == 0) {
        dpflate_.min_match_len = algorithm::utils::getMinMatch(
            dpflate_.encoding.offset_bits, dpflate_.encoding.length_bits);
    }
}
size_t DPFlateCompressor::get_lookahead_size() const { return dpflate_.window.look_size; }
void DPFlateCompressor::set_min_match(size_t v) { dpflate_.min_match_len = v; }
size_t DPFlateCompressor::get_min_match() const { return dpflate_.min_match_len; }
void DPFlateCompressor::set_max_chain_length(size_t v) { dpflate_.window.max_chain_length = v; }
size_t DPFlateCompressor::get_max_chain_length() const { return dpflate_.window.max_chain_length; }
void DPFlateCompressor::set_dp_sub_match_max(size_t v) {
    dpflate_.dp.dp_top = static_cast<uint8_t>(v);
}
size_t DPFlateCompressor::get_dp_sub_match_max() const { return dpflate_.dp.dp_top; }
void DPFlateCompressor::set_match_engine(int v) {
    dpflate_.dp.match_engine =
        v == 0 ? algorithm::models::MatchEngine::KMP : algorithm::models::MatchEngine::HashChain;
}
int DPFlateCompressor::get_match_engine() const {
    return dpflate_.dp.match_engine == algorithm::models::MatchEngine::KMP ? 0 : 1;
}
void DPFlateCompressor::set_use_flag_encoding(bool v) { dpflate_.encoding.use_flag_encoding = v; }
bool DPFlateCompressor::get_use_flag_encoding() const { return dpflate_.encoding.use_flag_encoding; }
void DPFlateCompressor::set_use_3hfmtree(bool v) { dpflate_.huffman.use_3hfmtree = v; }
bool DPFlateCompressor::get_use_3hfmtree() const { return dpflate_.huffman.use_3hfmtree; }
void DPFlateCompressor::set_huffman_chunk_bits(size_t k) {
    dpflate_.huffman.huffman_offset_bitwidth = static_cast<uint8_t>(k);
    dpflate_.huffman.huffman_length_bitwidth = static_cast<uint8_t>(k);
}
size_t DPFlateCompressor::get_huffman_chunk_bits() const {
    return dpflate_.huffman.huffman_offset_bitwidth;
}
void DPFlateCompressor::set_huffman_offset_chunk_bits(size_t k) {
    dpflate_.huffman.huffman_offset_bitwidth = static_cast<uint8_t>(k);
}
void DPFlateCompressor::set_huffman_length_chunk_bits(size_t k) {
    dpflate_.huffman.huffman_length_bitwidth = static_cast<uint8_t>(k);
}
size_t DPFlateCompressor::get_huffman_offset_chunk_bits() const {
    return dpflate_.huffman.huffman_offset_bitwidth;
}
size_t DPFlateCompressor::get_huffman_length_chunk_bits() const {
    return dpflate_.huffman.huffman_length_bitwidth;
}

}  // namespace compressor::core
