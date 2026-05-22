#include "AlgorithmFactory.hpp"

#include <fstream>
#include <memory>

#include "Brotli.hpp"
#include "Deflate.hpp"
#include "Delta.hpp"
#include "ImageCompressor.hpp"
#include "Inflate.hpp"
#include "LZDP.hpp"
#include "LZSS.hpp"
#include "DPFlate.hpp"
#include "ChunkedStreamAdapter.hpp"
#include "StreamChunkPolicy.hpp"
#include "Zstd.hpp"

namespace compressor::core {

bool (*g_cancel_callback)() = nullptr;

// ===== Global compression config =====

static CompressionConfig g_compression_config{};

CompressionConfig& compression_config() {
    return g_compression_config;
}

void reload_compression_config(const std::string& json_path) {
    g_compression_config.load_from_json_file(json_path);
}

// ===== CompressionConfig implementation =====

void CompressionConfig::load_from_json_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;  // keep defaults if file missing
    auto j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded()) return;

    auto& algo = j["algorithms"];
    if (algo.contains("lzdp"))    algo["lzdp"].get_to(lzdp);
    if (algo.contains("dpflate")) algo["dpflate"].get_to(dpflate);
    if (algo.contains("deflate")) algo["deflate"].get_to(deflate);
    if (algo.contains("jpeg"))    algo["jpeg"].get_to(jpeg);
    if (algo.contains("webp"))    algo["webp"].get_to(webp);
    // Keep struct defaults for missing keys — nlohmann get_to only overwrites present fields.
}

void CompressionConfig::save_to_json_file(const std::string& path) const {
    nlohmann::json j;
    j["algorithms"]["lzdp"]    = lzdp;
    j["algorithms"]["dpflate"] = dpflate;
    j["algorithms"]["deflate"] = deflate;
    j["algorithms"]["jpeg"]    = jpeg;
    j["algorithms"]["webp"]    = webp;
    std::ofstream f(path);
    f << j.dump(2) << '\n';
}

std::string CompressionConfig::to_json_string() const {
    nlohmann::json j;
    j["algorithms"]["lzdp"]    = lzdp;
    j["algorithms"]["dpflate"] = dpflate;
    j["algorithms"]["deflate"] = deflate;
    j["algorithms"]["jpeg"]    = jpeg;
    j["algorithms"]["webp"]    = webp;
    return j.dump();
}

// ===== createAlgorithm =====

static void apply_overrides(DeflatePipelineParams& p, const ParamOverrides& ov) {
    p.search_size       = ov.get("search_size", p.search_size);
    p.lookahead_size    = ov.get("lookahead_size", p.lookahead_size);
    p.min_match         = ov.get("min_match", p.min_match);
    p.max_chain_length  = ov.get("max_chain_length", p.max_chain_length);
}

static void apply_overrides(DpflatePipelineParams& p, const ParamOverrides& ov) {
    p.search_size              = ov.get("search_size", p.search_size);
    p.lookahead_size           = ov.get("lookahead_size", p.lookahead_size);
    p.min_match                = ov.get("min_match", p.min_match);
    p.max_chain_length         = ov.get("max_chain_length", p.max_chain_length);
    p.dp_sub_match_max         = ov.get("dp_sub_match_max", p.dp_sub_match_max);
    p.match_engine             = ov.get("match_engine", p.match_engine);
    p.use_flag_encoding        = ov.get("use_flag_encoding", p.use_flag_encoding);
    p.use_3hfmtree             = ov.get("use_3hfmtree", p.use_3hfmtree);
    p.huffman_offset_chunk_bits = ov.get("huffman_offset_chunk_bits", p.huffman_offset_chunk_bits);
    p.huffman_length_chunk_bits = ov.get("huffman_length_chunk_bits", p.huffman_length_chunk_bits);
}

static void apply_overrides(LzdpWholeFileParams& p, const ParamOverrides& ov) {
    p.search_size       = ov.get("search_size", p.search_size);
    p.lookahead_size    = ov.get("lookahead_size", p.lookahead_size);
    p.min_match         = ov.get("min_match", p.min_match);
    p.dp_top            = ov.get("dp_top", p.dp_top);
    p.use_flag_encoding = ov.get("use_flag_encoding", p.use_flag_encoding);
    p.match_engine      = ov.get("match_engine", p.match_engine);
}

static void apply_overrides(ImageCompressParams& p, const ParamOverrides& ov) {
    p.quality    = ov.get("quality", p.quality);
    p.max_width  = ov.get("max_width", p.max_width);
    p.max_height = ov.get("max_height", p.max_height);
}

auto createAlgorithm(AlgorithmID id,
                     const ParamOverrides* overrides)
    -> std::unique_ptr<algorithm::IAlgorithm> {
    using SDA = processor::StreamingDecompressAdapter;
    const auto& cfg = compression_config();  // read-only, thread-safe
    const ParamOverrides noov;
    const ParamOverrides& ov = overrides ? *overrides : noov;

    switch (id) {
        case AlgorithmID::None:
            return nullptr;

        case AlgorithmID::Deflate: {
            auto p = cfg.deflate;  // stack copy
            apply_overrides(p, ov);
            auto min_m = p.min_match == 0 ? std::size_t{3} : p.min_match;
            auto look  = p.lookahead_size == 0 ? std::size_t{258} : p.lookahead_size;
            return std::make_unique<algorithm::Deflate>(
                p.search_size, min_m, p.max_chain_length, look);
        }
        case AlgorithmID::Inflate:
            return std::make_unique<algorithm::Inflate>();

        case AlgorithmID::DeltaEncode:
            return std::make_unique<algorithm::DeltaEncode>();
        case AlgorithmID::DeltaDecode:
            return std::make_unique<algorithm::DeltaDecode>();

        case AlgorithmID::DPFlate: {
            auto p = cfg.dpflate;
            apply_overrides(p, ov);
            auto min_m = p.min_match == 0 ? std::size_t{4} : p.min_match;
            auto inst = std::make_unique<algorithm::DPFlate>(
                p.search_size, p.lookahead_size, min_m,
                p.max_chain_length, p.dp_sub_match_max);
            inst->set_match_engine(p.match_engine);
            inst->set_use_flag_encoding(p.use_flag_encoding);
            inst->set_use_3hfmtree(p.use_3hfmtree);
            auto hob = p.huffman_offset_chunk_bits > 0 ? p.huffman_offset_chunk_bits : 8;
            auto hlb = p.huffman_length_chunk_bits > 0 ? p.huffman_length_chunk_bits : 8;
            inst->set_huffman_offset_chunk_bits(hob);
            inst->set_huffman_length_chunk_bits(hlb);
            return inst;
        }
        case AlgorithmID::LZSS:
            return std::make_unique<algorithm::LZSS_OutOfCore>();
        case AlgorithmID::LZSSDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::decompress(data);
                });
        case AlgorithmID::LZSS_NoFlag:
            return std::make_unique<algorithm::LZSS_OutOfCore>(4096, 3, false);
        case AlgorithmID::LZSSDecompress_NoFlag:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::decompress(data, 3, false);
                });

        case AlgorithmID::LZDP: {
            auto p = cfg.lzdp;
            apply_overrides(p, ov);
            return std::make_unique<algorithm::LZDP_OutOfCore>(
                p.search_size, p.lookahead_size, p.min_match, p.dp_top,
                p.use_flag_encoding, p.match_engine);
        }
        case AlgorithmID::LZDPDecompress:
            return std::make_unique<algorithm::LZDPDecompress_OutOfCore>(false);

        case AlgorithmID::Brotli:
            return std::make_unique<algorithm::BrotliCompress>(65536, 3, 256);
        case AlgorithmID::BrotliDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::BrotliDecompress decompress;
                    std::vector<uint8_t> out(std::max(data.size() * 4 + 65536, size_t(2097152)));
                    auto status = decompress.process(data, out, true);
                    out.resize(status.bytes_produced);
                    return out;
                });
        case AlgorithmID::Zstd:
            return std::make_unique<algorithm::ZstdCompress>(3);
        case AlgorithmID::ZstdDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::ZstdDecompress decomp;
                    return decomp.decompress(data);
                });

        case AlgorithmID::JPEG_Compress: {
            auto p = cfg.jpeg;
            apply_overrides(p, ov);
            return std::make_unique<algorithm::ImageCompressor>(
                algorithm::ImageFormat::JPEG, p.quality, p.max_width, p.max_height);
        }
        case AlgorithmID::JPEG_Decompress:
            return nullptr;

        case AlgorithmID::WebP_Compress: {
            auto p = cfg.webp;
            apply_overrides(p, ov);
            return std::make_unique<algorithm::ImageCompressor>(
                algorithm::ImageFormat::JPEG, p.quality, p.max_width, p.max_height);
        }
        case AlgorithmID::WebP_Decompress:
            return nullptr;
    }
    return nullptr;
}

}  // namespace compressor::core
