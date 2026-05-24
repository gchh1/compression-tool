#include "api.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "Brotli.hpp"
#include "Deflate.hpp"
#include "Dpflate.hpp"
#include "ImageCompressor.hpp"
#include "LZDP.hpp"
#include "LZSS.hpp"
#include "Zstd.hpp"
#include "StreamingCancel.hpp"
#include "WCXProtocol.hpp"
#include "io/FileIO.hpp"

#ifndef __EMSCRIPTEN__
namespace fs = std::filesystem;
#endif

namespace compressor::api_new {

namespace {

using namespace compressor::algorithm;
using namespace compressor::algorithm::pipeline;

size_t effective_chunk(size_t requested) {
    constexpr size_t kMin = 64 * 1024;
    constexpr size_t kMax = 64 * 1024 * 1024;
    constexpr size_t kDefault = 1024 * 1024;
    if (requested == 0) {
        return kDefault;
    }
    return std::clamp(requested, kMin, kMax);
}

std::string spill_workspace_dir(const fs::path& staged_output) {
    const fs::path parent = staged_output.parent_path();
    if (!parent.empty()) {
        return parent.string();
    }
    return fs::temp_directory_path().string();
}

LZDPConfig lzdp_from_params(const compressor::core::LzdpWholeFileParams* p) {
    LZDPConfig cfg;
    if (!p) {
        return cfg;
    }
    cfg.window.search_size = p->search_size;
    cfg.window.look_size = p->lookahead_size;
    cfg.window.min_match_len = p->min_match;
    cfg.dp.dp_top = static_cast<uint8_t>(p->dp_top);
    cfg.encoding.use_flag_encoding = p->use_flag_encoding;
    cfg.dp.match_engine =
        p->match_engine == 0 ? models::MatchEngine::KMP : models::MatchEngine::HashChain;
    cfg.encoding.offset_bits = static_cast<uint8_t>(
        algorithm::utils::calcBitWidth(cfg.window.search_size));
    cfg.encoding.length_bits = static_cast<uint8_t>(
        algorithm::utils::calcBitWidth(cfg.window.look_size));
    if (cfg.window.min_match_len == 0) {
        cfg.window.min_match_len = algorithm::utils::getMinMatch(
            cfg.encoding.offset_bits, cfg.encoding.length_bits);
    }
    return cfg;
}

LZSSConfig lzss_from_defaults() { return LZSSConfig{}; }

LZSSConfig lzss_from_params(const compressor::core::LzssPipelineParams* p, AlgorithmID id) {
    LZSSConfig cfg;
    if (p) {
        cfg.window.search_size = p->search_size;
        cfg.window.look_size = p->lookahead_size;
        cfg.window.min_match_len = p->min_match;
        cfg.encoding.offset_bits =
            static_cast<uint8_t>(algorithm::utils::calcBitWidth(p->search_size));
        cfg.encoding.length_bits =
            static_cast<uint8_t>(algorithm::utils::calcBitWidth(p->lookahead_size));
        cfg.encoding.use_flag_encoding = p->use_flag_encoding;
    }
    if (id == AlgorithmID::LZSS_NoFlag || id == AlgorithmID::LZSSDecompress_NoFlag) {
        cfg.encoding.use_flag_encoding = false;
    }
    if (cfg.window.min_match_len == 0) {
        cfg.window.min_match_len = algorithm::utils::getMinMatch(
            cfg.encoding.offset_bits, cfg.encoding.length_bits);
    }
    return cfg;
}

DeflateConfig deflate_from_params(const compressor::core::DeflatePipelineParams* p) {
    DeflateConfig cfg;
    if (!p) {
        return cfg;
    }
    cfg.window.search_size = p->search_size;
    cfg.window.look_size = p->lookahead_size;
    cfg.encoding.offset_bits =
        static_cast<uint8_t>(algorithm::utils::calcBitWidth(p->search_size));
    cfg.encoding.length_bits =
        static_cast<uint8_t>(algorithm::utils::calcBitWidth(p->lookahead_size));
    cfg.window.min_match_len = p->min_match;
    cfg.window.max_chain_length = p->max_chain_length;
    if (cfg.window.min_match_len == 0) {
        cfg.window.min_match_len = algorithm::utils::getMinMatch(
            cfg.encoding.offset_bits, cfg.encoding.length_bits);
    }
    cfg.encoding.use_flag_encoding = p->use_flag_encoding;
    cfg.use_3hfmtree = p->use_3hfmtree;
    cfg.huffman_3hm.chunk_bits = static_cast<uint32_t>(p->huffman_offset_chunk_bits);
    return cfg;
}

DPFlateConfig dpflate_from_params(const compressor::core::DpflatePipelineParams* p) {
    DPFlateConfig cfg;
    if (!p) {
        return cfg;
    }
    cfg.lzdp.window.search_size = p->search_size;
    cfg.lzdp.window.look_size = p->lookahead_size;
    cfg.lzdp.window.min_match_len = p->min_match;
    cfg.lzdp.window.max_chain_length = p->max_chain_length;
    cfg.lzdp.dp.dp_top = 3;
    cfg.lzdp.dp.match_engine =
        p->match_engine == 0 ? models::MatchEngine::KMP : models::MatchEngine::HashChain;
    cfg.encoding = EncodingConfig{
        static_cast<uint8_t>(algorithm::utils::calcBitWidth(p->search_size)),
        static_cast<uint8_t>(algorithm::utils::calcBitWidth(p->lookahead_size)),
        p->use_flag_encoding
    };
    cfg.use_3hfmtree = p->use_3hfmtree;
    cfg.huffman_3hm.chunk_bits = static_cast<uint32_t>(p->huffman_offset_chunk_bits);
    if (cfg.lzdp.window.min_match_len == 0) {
        cfg.lzdp.window.min_match_len = algorithm::utils::getMinMatch(
            cfg.encoding.offset_bits, cfg.encoding.length_bits);
    }
    return cfg;
}

bool is_lzdp(AlgorithmID id) {
    return id == AlgorithmID::LZDP || id == AlgorithmID::LZDP_New;
}

bool is_lzss(AlgorithmID id) {
    return id == AlgorithmID::LZSS || id == AlgorithmID::LZSS_New ||
           id == AlgorithmID::LZSS_NoFlag;
}

bool is_deflate(AlgorithmID id) {
    return id == AlgorithmID::Deflate || id == AlgorithmID::Deflate_New;
}

bool is_dpflate(AlgorithmID id) {
    return id == AlgorithmID::DPFlate || id == AlgorithmID::DPFlate_New;
}

bool is_lzdp_decompress(AlgorithmID id) {
    return id == AlgorithmID::LZDPDecompress || id == AlgorithmID::LZDPDecompress_New;
}

bool is_lzss_decompress(AlgorithmID id) {
    return id == AlgorithmID::LZSSDecompress || id == AlgorithmID::LZSSDecompress_New ||
           id == AlgorithmID::LZSSDecompress_NoFlag;
}

bool is_brotli(AlgorithmID id) {
    return id == AlgorithmID::Brotli;
}

bool is_brotli_decompress(AlgorithmID id) {
    return id == AlgorithmID::BrotliDecompress;
}

bool is_zstd(AlgorithmID id) {
    return id == AlgorithmID::Zstd;
}

bool is_zstd_decompress(AlgorithmID id) {
    return id == AlgorithmID::ZstdDecompress;
}

bool is_image_jpeg(AlgorithmID id) {
    return id == AlgorithmID::ImageJpeg;
}

bool is_image_jpeg_decompress(AlgorithmID id) {
    return id == AlgorithmID::ImageJpegDecompress;
}

bool is_image_png(AlgorithmID id) {
    return id == AlgorithmID::ImagePng;
}

bool is_image_png_decompress(AlgorithmID id) {
    return id == AlgorithmID::ImagePngDecompress;
}

CompressResult fail(const std::string& msg) {
    CompressResult r;
    r.error_message = msg;
    r.success = false;
    return r;
}

#ifndef __EMSCRIPTEN__
fs::path staged_part(const std::string& final_path) {
    return fs::path(final_path + ".part");
}

void remove_best_effort(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

bool commit_staged(const fs::path& part, const fs::path& final_path, std::string& err) {
    std::error_code ec;
    fs::rename(part, final_path, ec);
    if (!ec) {
        return true;
    }
    fs::remove(final_path, ec);
    ec.clear();
    std::ifstream src(part.string(), std::ios::binary);
    std::ofstream dst(final_path.string(), std::ios::binary | std::ios::trunc);
    if (!src || !dst) {
        err = "cannot copy staged output";
        return false;
    }
    dst << src.rdbuf();
    fs::remove(part, ec);
    return dst.good();
}
#endif

}  // namespace

auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain,
              const compressor::core::LzdpWholeFileParams* lzdp_whole_file,
              const compressor::core::DpflatePipelineParams* dpflate_pipeline,
              const compressor::core::DeflatePipelineParams* deflate_pipeline,
              const compressor::core::LzssPipelineParams* lzss_pipeline,
              std::size_t) -> CompressResult {
    CompressResult result;
    result.original_size = data.size();
    if (chain.empty()) {
        return fail("Empty algorithm chain");
    }
    if (chain.size() != 1) {
        return fail("multi-stage chain not supported yet");
    }

    if (core_new::is_streaming_cancel_requested()) {
        fprintf(stderr, "[CANCEL_TRACE] compress: cancelled at entry\n");
        fflush(stderr);
        result.cancelled = true;
        return fail("cancelled");
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    const auto id = chain.front();

    try {
        if (is_lzdp(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto r = compress_bytes(data, lzdp_from_params(lzdp_whole_file));
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(r.compressed);
        } else if (is_lzss(id)) {
            fprintf(stderr, "[CANCEL_TRACE] compress LZSS memory path\n");
            fflush(stderr);
            if (core_new::is_streaming_cancel_requested()) {
                fprintf(stderr, "[CANCEL_TRACE] compress LZSS: cancelled before compress\n");
                fflush(stderr);
                throw std::runtime_error("cancelled");
            }
            auto r = compress_bytes_lzss(data, lzss_from_params(lzss_pipeline, id));
            if (core_new::is_streaming_cancel_requested()) {
                fprintf(stderr, "[CANCEL_TRACE] compress LZSS: cancelled after compress\n");
                fflush(stderr);
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(r.compressed);
        } else if (is_deflate(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto r = deflate_compress(data, deflate_from_params(deflate_pipeline));
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(r.compressed);
        } else if (is_dpflate(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto r = dpflate_compress(data, dpflate_from_params(dpflate_pipeline));
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(r.compressed);
        } else if (is_brotli(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto encoded = brotli_encode(data, BrotliParams{});
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(encoded);
        } else if (is_zstd(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto compressed = zstd_compress(data);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(compressed);
        } else if (is_image_jpeg(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto compressed = image_compress(data, ImageFormat::JPEG);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(compressed);
        } else if (is_image_png(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto compressed = image_compress(data, ImageFormat::PNG);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = std::move(compressed);
        } else {
            return fail("unsupported algorithm in chain");
        }
    } catch (const std::exception& e) {
        CompressResult r = fail(e.what());
        if (std::string(e.what()) == "cancelled") {
            r.cancelled = true;
        }
        return r;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        result.original_size > 0
            ? static_cast<double>(result.compressed_size) / result.original_size
            : 0.0;
    result.time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.success = true;
    return result;
}

auto decompress(const std::vector<uint8_t>& data,
                std::span<const AlgorithmID> chain,
                const compressor::core::LzdpWholeFileParams* lzdp_whole_file,
                const compressor::core::DpflatePipelineParams* dpflate_pipeline,
                const compressor::core::DeflatePipelineParams* deflate_pipeline,
                const compressor::core::LzssPipelineParams* lzss_pipeline,
                std::size_t) -> CompressResult {
    CompressResult result;
    result.original_size = data.size();
    if (chain.empty()) {
        return fail("Empty algorithm chain");
    }
    if (chain.size() != 1) {
        return fail("multi-stage chain not supported yet");
    }

    if (core_new::is_streaming_cancel_requested()) {
        result.cancelled = true;
        return fail("cancelled");
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    const auto id = chain.front();

    try {
        if (is_lzdp_decompress(id) || is_lzdp(id)) {
            result.data = decompress_bytes(data, lzdp_from_params(lzdp_whole_file));
        } else if (is_lzss_decompress(id) || is_lzss(id)) {
            result.data = decompress_bytes_lzss(data, lzss_from_params(lzss_pipeline, id));
        } else if (id == AlgorithmID::Inflate || is_deflate(id)) {
            result.data = deflate_decompress(data, deflate_from_params(deflate_pipeline));
        } else if (is_dpflate(id)) {
            result.data = dpflate_decompress(data, dpflate_from_params(dpflate_pipeline));
        } else if (is_brotli_decompress(id) || is_brotli(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = brotli_decode(data);
        } else if (is_zstd_decompress(id) || is_zstd(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = zstd_decompress(data);
        } else if (is_image_jpeg_decompress(id) || is_image_jpeg(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = image_decompress(data);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (is_image_png_decompress(id) || is_image_png(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            result.data = image_decompress(data);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else {
            return fail("unsupported decompress algorithm");
        }
    } catch (const std::exception& e) {
        CompressResult r = fail(e.what());
        if (std::string(e.what()) == "cancelled") {
            r.cancelled = true;
        }
        return r;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.compressed_size = result.data.size();
    result.time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.success = true;
    return result;
}

auto pack_wcx(const std::vector<uint8_t>& compressed_data,
              AlgorithmID algorithm,
              size_t original_size,
              const std::string& original_filename,
              bool is_folder,
              bool web_dict_preprocess) -> std::vector<uint8_t> {
    const uint8_t code = wcx::toAlgoCode(algorithm);
    const auto orig_u32 = static_cast<uint32_t>(std::min(original_size, size_t{UINT32_MAX}));
    const auto comp_u32 =
        static_cast<uint32_t>(std::min(compressed_data.size(), size_t{UINT32_MAX}));
    auto out = wcx::buildHeaderBytes(code, orig_u32, comp_u32, original_filename);
    if (out.size() >= 15) {
        uint8_t flags = 0;
        if (is_folder) {
            flags |= 0x01;
        }
        if (web_dict_preprocess) {
            flags |= 0x02;
        }
        out[14] = flags;
    }
    out.insert(out.end(), compressed_data.begin(), compressed_data.end());
    return out;
}

auto unpack_wcx(const std::vector<uint8_t>& data) -> WCXUnpackResult {
    WCXUnpackResult result;
    wcx::HeaderView header{};
    if (!wcx::tryParseHeader(std::span<const uint8_t>(data.data(), data.size()), header) ||
        !header.valid) {
        result.error_message = "Invalid WCX header";
        return result;
    }
    result.algo_code = header.algo_code;
    result.original_size = header.original_size;
    result.compressed_size = header.compressed_size;
    result.original_filename = header.original_filename;
    result.is_folder = (header.flags & 0x01) != 0;
    result.web_dict_preprocess = (header.flags & 0x02) != 0;

    const uint64_t need = static_cast<uint64_t>(header.total_size) +
                          static_cast<uint64_t>(header.compressed_size);
    if (need > data.size()) {
        result.error_message = "WCX data shorter than declared header + compressed_size";
        return result;
    }
    result.payload.assign(
        data.begin() + static_cast<std::ptrdiff_t>(header.total_size),
        data.begin() + static_cast<std::ptrdiff_t>(need));
    result.success = true;
    return result;
}

#ifndef __EMSCRIPTEN__

void set_streaming_compress_cancel_requested(bool requested) {
    fprintf(stderr, "[CANCEL_TRACE] api_new::set_streaming_compress_cancel_requested(%d)\n", requested);
    fflush(stderr);
    core_new::set_streaming_cancel_requested(requested);
}

auto compressFile(const std::string& input_path,
                  const std::string& output_path,
                  std::span<const AlgorithmID> chain,
                  size_t stream_chunk_bytes,
                  uint32_t file_compress_opts,
                  const compressor::core::LzdpWholeFileParams* lzdp_whole_file,
                  const compressor::core::DpflatePipelineParams* dpflate_pipeline,
                  const compressor::core::DeflatePipelineParams* deflate_pipeline,
                  const compressor::core::LzssPipelineParams* lzss_pipeline) -> CompressResult {
    CompressResult result;

    fprintf(stderr, "[CANCEL_TRACE] compressFile: entry, input=%s, id=%d\n",
            input_path.c_str(), chain.empty() ? -1 : static_cast<int>(chain.front()));
    fflush(stderr);

    if (chain.empty()) {
        return fail("Empty algorithm chain");
    }
    if (chain.size() != 1) {
        return fail("multi-stage chain not supported yet");
    }
    if (core_new::is_streaming_cancel_requested()) {
        result.cancelled = true;
        return fail("cancelled");
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    const auto id = chain.front();
    const size_t chunk = effective_chunk(stream_chunk_bytes);

    std::error_code ec;
    const auto file_size = fs::file_size(input_path, ec);
    if (ec) {
        return fail("Cannot stat input: " + ec.message());
    }
    result.original_size = static_cast<size_t>(file_size);

    const fs::path part = staged_part(output_path);
    const fs::path final_path(output_path);
    remove_best_effort(part);

    const std::string payload_tmp = (part.string() + ".payload");
    remove_best_effort(payload_tmp);

    std::string active_workspace;
    try {
        const bool use_streaming = file_size > 256 * 1024;
        // GUI sets kFileCompressLzdpWholeFileFramed for LZDP file jobs: same ``compress_bytes`` as
        // ``LZDPCompressor`` (algorithm_new), not chunked ``LZDPStreamingPipeline``.
        const bool lzdp_use_chunked_stream =
            use_streaming &&
            ((file_compress_opts & compressor::core::kFileCompressLzdpWholeFileFramed) == 0);

        if (is_lzdp(id)) {
            const auto cfg = lzdp_from_params(lzdp_whole_file);
            if (lzdp_use_chunked_stream) {
                LZDPStreamingOptions opts;
                opts.chunk_size = chunk;
                opts.workspace_dir = spill_workspace_dir(part);
                active_workspace = opts.workspace_dir;
                LZDPStreamingPipeline pipe(cfg, opts);
                pipe.compress_file(input_path, payload_tmp);
            } else {
                const auto input = core_new::io::read_file_bytes(input_path);
                if (core_new::is_streaming_cancel_requested()) {
                    throw std::runtime_error("cancelled");
                }
                auto r = compress_bytes(input, cfg);
                if (core_new::is_streaming_cancel_requested()) {
                    throw std::runtime_error("cancelled");
                }
                core_new::io::write_file_bytes(payload_tmp, r.compressed);
            }
        } else if (is_lzss(id)) {
            fprintf(stderr, "[CANCEL_TRACE] compressFile LZSS path, use_streaming=%d\n", use_streaming);
            fflush(stderr);
            const auto cfg = lzss_from_params(lzss_pipeline, id);
            if (use_streaming) {
                fprintf(stderr, "[CANCEL_TRACE] compressFile LZSS → LZSSStreamingPipeline::compress_file\n");
                fflush(stderr);
                LZSSStreamingOptions opts;
                opts.chunk_size = chunk;
                opts.workspace_dir = spill_workspace_dir(part);
                active_workspace = opts.workspace_dir;
                LZSSStreamingPipeline pipe(cfg, opts);
                pipe.compress_file(input_path, payload_tmp);
            } else {
                fprintf(stderr, "[CANCEL_TRACE] compressFile LZSS → compress_bytes_lzss (memory path)\n");
                fflush(stderr);
                const auto input = core_new::io::read_file_bytes(input_path);
                if (core_new::is_streaming_cancel_requested()) {
                    fprintf(stderr, "[CANCEL_TRACE] compressFile LZSS memory: cancelled after read\n");
                    fflush(stderr);
                    throw std::runtime_error("cancelled");
                }
                auto r = compress_bytes_lzss(input, cfg);
                if (core_new::is_streaming_cancel_requested()) {
                    fprintf(stderr, "[CANCEL_TRACE] compressFile LZSS memory: cancelled after compress\n");
                    fflush(stderr);
                    throw std::runtime_error("cancelled");
                }
                core_new::io::write_file_bytes(payload_tmp, r.compressed);
            }
        } else if (is_deflate(id)) {
            const auto cfg = deflate_from_params(deflate_pipeline);
            const auto input = core_new::io::read_file_bytes(input_path);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto r = deflate_compress(input, cfg);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            core_new::io::write_file_bytes(payload_tmp, r.compressed);
        } else if (is_dpflate(id)) {
            const auto cfg = dpflate_from_params(dpflate_pipeline);
            const auto input = core_new::io::read_file_bytes(input_path);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto r = dpflate_compress(input, cfg);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            core_new::io::write_file_bytes(payload_tmp, r.compressed);
        } else if (is_brotli(id)) {
            const auto input = core_new::io::read_file_bytes(input_path);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto encoded = brotli_encode(input, BrotliParams{});
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            core_new::io::write_file_bytes(payload_tmp, encoded);
        } else if (is_zstd(id)) {
            const auto input = core_new::io::read_file_bytes(input_path);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto compressed = zstd_compress(input);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            core_new::io::write_file_bytes(payload_tmp, compressed);
        } else if (is_image_jpeg(id)) {
            const auto input = core_new::io::read_file_bytes(input_path);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto compressed = image_compress(input, ImageFormat::JPEG);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            core_new::io::write_file_bytes(payload_tmp, compressed);
        } else if (is_image_png(id)) {
            const auto input = core_new::io::read_file_bytes(input_path);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto compressed = image_compress(input, ImageFormat::PNG);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            core_new::io::write_file_bytes(payload_tmp, compressed);
        } else {
            return fail("unsupported algorithm for compressFile");
        }

        if (core_new::is_streaming_cancel_requested()) {
            remove_best_effort(part);
            remove_best_effort(payload_tmp);
            result.cancelled = true;
            return fail("cancelled");
        }

        const auto payload = core_new::io::read_file_bytes(payload_tmp);
        remove_best_effort(payload_tmp);

        std::ofstream out(part.string(), std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail("Cannot open staged output");
        }

        const std::string original_filename = fs::path(input_path).filename().string();
        const uint8_t algo_code = wcx::toAlgoCode(id);
        const auto header_orig =
            static_cast<uint32_t>(std::min<uint64_t>(file_size, UINT32_MAX));
        if (!wcx::writeHeader(out, algo_code, header_orig, 0, original_filename)) {
            return fail("Cannot write WCX header");
        }

        out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        if (!out) {
            return fail("Cannot write WCX payload");
        }

        wcx::patchCompressedSize(out, static_cast<uint32_t>(
                                         std::min(payload.size(), size_t{UINT32_MAX})));
        out.close();

        std::string commit_err;
        if (!commit_staged(part, final_path, commit_err)) {
            return fail(commit_err);
        }

        result.compressed_size = payload.size() + wcx::FIXED_HEADER_SIZE;
        result.bytes_processed = result.original_size;
        result.success = true;
    } catch (const std::exception& e) {
        remove_best_effort(part);
        remove_best_effort(payload_tmp);
        if (!active_workspace.empty()) {
            try {
                for (const auto& entry : fs::directory_iterator(active_workspace)) {
                    if (entry.is_regular_file()) {
                        remove_best_effort(entry.path().string());
                    }
                }
            } catch (...) {}
        }
        CompressResult r = fail(e.what());
        if (std::string(e.what()) == "cancelled") {
            r.cancelled = true;
        }
        return r;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.compression_ratio =
        result.original_size > 0
            ? static_cast<double>(result.compressed_size) / result.original_size
            : 0.0;
    return result;
}

auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t,
                    const compressor::core::LzdpWholeFileParams* lzdp_whole_file,
                    const compressor::core::LzssPipelineParams* lzss_pipeline,
                    const compressor::core::DpflatePipelineParams* dpflate_pipeline,
                    const compressor::core::DeflatePipelineParams* deflate_pipeline) -> CompressResult {
    CompressResult result;
    if (chain.empty()) {
        return fail("Empty algorithm chain");
    }

    if (core_new::is_streaming_cancel_requested()) {
        result.cancelled = true;
        return fail("cancelled");
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    try {
        const auto wcx_bytes = core_new::io::read_file_bytes(input_path);
        auto unpacked = unpack_wcx(wcx_bytes);
        if (!unpacked.success) {
            return fail(unpacked.error_message);
        }

        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }

        AlgorithmID id = chain.front();
        if (id == AlgorithmID::None) {
            switch (unpacked.algo_code) {
                case 3:
                    id = AlgorithmID::LZDPDecompress;
                    break;
                case 2:
                    id = AlgorithmID::LZSSDecompress;
                    break;
                case 1:
                    id = AlgorithmID::Inflate;
                    break;
                case 5:
                    id = AlgorithmID::DPFlate;
                    break;
                case 7:
                    id = AlgorithmID::BrotliDecompress;
                    break;
                case 8:
                    id = AlgorithmID::ZstdDecompress;
                    break;
                case 10:
                    id = AlgorithmID::ImageJpegDecompress;
                    break;
                case 11:
                    id = AlgorithmID::ImagePngDecompress;
                    break;
                default:
                    return fail("Unknown WCX algo code");
            }
        } else if (id == AlgorithmID::Inflate && unpacked.algo_code == 5) {
            id = AlgorithmID::DPFlate;
        }

        std::vector<uint8_t> plain;
        if (is_lzdp_decompress(id) || is_lzdp(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = decompress_bytes(unpacked.payload, lzdp_from_params(lzdp_whole_file));
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (is_lzss_decompress(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = decompress_bytes_lzss(unpacked.payload, lzss_from_params(lzss_pipeline, id));
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (id == AlgorithmID::Inflate || is_deflate(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = deflate_decompress(unpacked.payload,
                                               deflate_from_params(deflate_pipeline));
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (is_dpflate(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = dpflate_decompress(unpacked.payload,
                                             dpflate_from_params(dpflate_pipeline));
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (is_brotli_decompress(id) || is_brotli(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = brotli_decode(unpacked.payload);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (is_zstd_decompress(id) || is_zstd(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = zstd_decompress(unpacked.payload);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (is_image_jpeg_decompress(id) || is_image_jpeg(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = image_decompress(unpacked.payload);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else if (is_image_png_decompress(id) || is_image_png(id)) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            plain = image_decompress(unpacked.payload);
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
        } else {
            auto dec = decompress(unpacked.payload, std::span<const AlgorithmID>(&id, 1),
                                  nullptr, dpflate_pipeline, deflate_pipeline, lzss_pipeline);
            if (!dec.success) {
                return dec;
            }
            plain = std::move(dec.data);
        }

        core_new::io::write_file_bytes(output_path, plain);
        result.original_size = wcx_bytes.size();
        result.compressed_size = plain.size();
        result.success = true;
    } catch (const std::exception& e) {
        CompressResult r = fail(e.what());
        if (std::string(e.what()) == "cancelled") {
            r.cancelled = true;
        }
        return r;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

auto compressDirectory(const std::string&,
                       const std::string&,
                       std::span<const AlgorithmID>,
                       size_t,
                       uint32_t,
                       const compressor::core::LzdpWholeFileParams*,
                       const compressor::core::DpflatePipelineParams*,
                       const compressor::core::DeflatePipelineParams*,
                       const compressor::core::LzssPipelineParams*) -> CompressResult {
    return fail("compressDirectory not implemented for algorithm_new api stack");
}

#endif

}  // namespace compressor::api_new
