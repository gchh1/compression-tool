#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "IAlgorithm.hpp"

namespace compressor::core {

/// Helper: read a JSON field as bool, accepting both boolean true/false and integer 0/1.
inline bool json_bool(const nlohmann::json& j, const char* key, bool fallback) {
    auto it = j.find(key);
    if (it == j.end()) return fallback;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number_integer()) return it->get<int>() != 0;
    return fallback;
}

/// Snapshot for ``LZDP_OutOfCore`` when using whole-file framed file compression.
struct LzdpWholeFileParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t dp_top{3};
    bool use_flag_encoding{false};
    int match_engine{0};
};

struct DeflatePipelineParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t max_chain_length{256};
};

struct DpflatePipelineParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t max_chain_length{256};
    std::size_t dp_sub_match_max{6};
    int match_engine{1};
    bool use_flag_encoding{false};
    bool use_3hfmtree{false};
    std::size_t huffman_offset_chunk_bits{8};
    std::size_t huffman_length_chunk_bits{8};
};

struct ImageCompressParams {
    int quality{85};
    int max_width{0};
    int max_height{0};
};

// ---- nlohmann JSON serialization ----
// Custom from_json/to_json instead of NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
// because the JSON config stores booleans as 0/1 integers.

inline void from_json(const nlohmann::json& j, LzdpWholeFileParams& p) {
    p.search_size       = j.value("search_size", 4096ULL);
    p.lookahead_size    = j.value("lookahead_size", 256ULL);
    p.min_match         = j.value("min_match", 0ULL);
    p.dp_top            = j.value("dp_top", 3ULL);
    p.use_flag_encoding = json_bool(j, "use_flag_encoding", false);
    p.match_engine      = j.value("match_engine", 0);
}

inline void to_json(nlohmann::json& j, const LzdpWholeFileParams& p) {
    j = nlohmann::json{
        {"search_size", p.search_size},
        {"lookahead_size", p.lookahead_size},
        {"min_match", p.min_match},
        {"dp_top", p.dp_top},
        {"use_flag_encoding", p.use_flag_encoding},
        {"match_engine", p.match_engine}};
}

inline void from_json(const nlohmann::json& j, DeflatePipelineParams& p) {
    p.search_size      = j.value("search_size", 4096ULL);
    p.lookahead_size   = j.value("lookahead_size", 256ULL);
    p.min_match        = j.value("min_match", 0ULL);
    p.max_chain_length = j.value("max_chain_length", 256ULL);
}

inline void to_json(nlohmann::json& j, const DeflatePipelineParams& p) {
    j = nlohmann::json{
        {"search_size", p.search_size},
        {"lookahead_size", p.lookahead_size},
        {"min_match", p.min_match},
        {"max_chain_length", p.max_chain_length}};
}

inline void from_json(const nlohmann::json& j, DpflatePipelineParams& p) {
    p.search_size              = j.value("search_size", 4096ULL);
    p.lookahead_size           = j.value("lookahead_size", 256ULL);
    p.min_match                = j.value("min_match", 0ULL);
    p.max_chain_length         = j.value("max_chain_length", 256ULL);
    p.dp_sub_match_max         = j.value("dp_sub_match_max", 6ULL);
    p.match_engine             = j.value("match_engine", 1);
    p.use_flag_encoding        = json_bool(j, "use_flag_encoding", false);
    p.use_3hfmtree             = json_bool(j, "use_3hfmtree", false);
    p.huffman_offset_chunk_bits = j.value("huffman_offset_chunk_bits", 8ULL);
    p.huffman_length_chunk_bits = j.value("huffman_length_chunk_bits", 8ULL);
}

inline void to_json(nlohmann::json& j, const DpflatePipelineParams& p) {
    j = nlohmann::json{
        {"search_size", p.search_size},
        {"lookahead_size", p.lookahead_size},
        {"min_match", p.min_match},
        {"max_chain_length", p.max_chain_length},
        {"dp_sub_match_max", p.dp_sub_match_max},
        {"match_engine", p.match_engine},
        {"use_flag_encoding", p.use_flag_encoding},
        {"use_3hfmtree", p.use_3hfmtree},
        {"huffman_offset_chunk_bits", p.huffman_offset_chunk_bits},
        {"huffman_length_chunk_bits", p.huffman_length_chunk_bits}};
}

inline void from_json(const nlohmann::json& j, ImageCompressParams& p) {
    p.quality    = j.value("quality", 85);
    p.max_width  = j.value("max_width", 0);
    p.max_height = j.value("max_height", 0);
}

inline void to_json(nlohmann::json& j, const ImageCompressParams& p) {
    j = nlohmann::json{
        {"quality", p.quality},
        {"max_width", p.max_width},
        {"max_height", p.max_height}};
}

enum class AlgorithmID {
    None,
    Deflate,
    Inflate,
    DeltaEncode,
    DeltaDecode,
    LZSS,
    LZSSDecompress,
    LZSS_NoFlag,
    LZSSDecompress_NoFlag,
    LZDP,
    LZDPDecompress,
    DPFlate,
    Brotli,
    BrotliDecompress,
    Zstd,
    ZstdDecompress,
    JPEG_Compress,
    JPEG_Decompress,
    WebP_Compress,
    WebP_Decompress,
};

inline constexpr uint32_t kFileCompressOptsNone = 0;
inline constexpr uint32_t kFileCompressLzdpWholeFileFramed = 1u << 0;

/// Per-call overrides for ADE (Algorithm Decision Engine) to customize
/// individual file compression without modifying the global config.
struct ParamOverrides {
    std::unordered_map<std::string, int> values;

    bool empty() const { return values.empty(); }

    int get(const std::string& key, int fallback) const {
        auto it = values.find(key);
        return it != values.end() ? it->second : fallback;
    }
};

/// All algorithm parameters in one struct — loaded once at startup from
/// ``webcompress_settings.json``, read lock-free by parallel compression threads.
struct CompressionConfig {
    LzdpWholeFileParams lzdp;
    DpflatePipelineParams dpflate;
    DeflatePipelineParams deflate;
    ImageCompressParams jpeg;
    ImageCompressParams webp;

    /// Load from JSON file. Called at startup and after GUI config changes.
    void load_from_json_file(const std::string& path);
    /// Write current config back to JSON file.
    void save_to_json_file(const std::string& path) const;
    /// Return the JSON representation as string (for Python inspection).
    std::string to_json_string() const;
};

/// Global config singleton — initialized at startup, read-only during compression.
CompressionConfig& compression_config();

/// Reload global config from JSON file. Called from Python after set_config()
/// saves new settings. Safe to call while no compression is running.
void reload_compression_config(const std::string& json_path);

/// Create an algorithm instance from global config, with optional per-call
/// overrides (ADE delta). Thread-safe: reads g_compression_config (read-only
/// during compression) and copies params to the stack before merging overrides.
auto createAlgorithm(AlgorithmID id,
                     const ParamOverrides* overrides = nullptr)
    -> std::unique_ptr<algorithm::IAlgorithm>;

extern bool (*g_cancel_callback)();

inline AlgorithmID getDecompressorID(AlgorithmID comp) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::Deflate, AlgorithmID::Inflate},
        {AlgorithmID::LZSS, AlgorithmID::LZSSDecompress},
        {AlgorithmID::LZSS_NoFlag, AlgorithmID::LZSSDecompress_NoFlag},
        {AlgorithmID::LZDP, AlgorithmID::LZDPDecompress},
        {AlgorithmID::DPFlate, AlgorithmID::Inflate},
        {AlgorithmID::Brotli, AlgorithmID::BrotliDecompress},
        {AlgorithmID::Zstd, AlgorithmID::ZstdDecompress},
    };
    auto it = map.find(comp);
    return it != map.end() ? it->second : AlgorithmID::None;
}

inline AlgorithmID getPostpressorID(AlgorithmID pre) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::None, AlgorithmID::None},
        {AlgorithmID::DeltaEncode, AlgorithmID::DeltaDecode},
    };
    auto it = map.find(pre);
    return it != map.end() ? it->second : AlgorithmID::None;
}

}
