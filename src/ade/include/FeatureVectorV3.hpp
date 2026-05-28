/**
 * @file FeatureVectorV3.hpp
 * @author ADE Module - Feature Vector Data Structures (v3.0)
 * @brief Core data structures for the segmented feature vector architecture
 * @version 3.0
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details Implements the segmented feature vector architecture:
 *   BaseSegment (20 dims = 80 bytes) + ExtensionData (union, up to 8 dims = 32 bytes)
 *   + FeatureVectorV3 (composite with metadata).
 *
 * Total size per padded vector: 33 × float = 132 bytes
 * Total with metadata: ~144 bytes per analyzed file.
 */

#pragma once

#include "MagicBytesDetector.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

namespace constants {
    constexpr size_t MAX_PADDED_DIMENSIONS = 33;

    // CMS params used by BaseFeatureExtractor
    constexpr size_t CMS_WIDTH = 4096;
    constexpr size_t CMS_DEPTH = 4;

    // Block processing params used by BaseFeatureExtractor
    constexpr size_t LOCAL_ENTROPY_WINDOW = 1024;
    constexpr size_t BLOCK_SIZE = 4096;
    constexpr size_t MIN_ZERO_RUN_FOR_BOUNDARY = 8;
}

// ========================================================================
// Extension Type Enum
// ========================================================================
enum class ExtensionType : uint8_t {
    NONE = 0,
    TEXT,
    IMAGE,
    AUDIO,
    VIDEO,
    ARCHIVE,
    BINARY
};

inline auto extension_type_to_string(ExtensionType t) -> std::string {
    switch (t) {
        case ExtensionType::TEXT:    return "TEXT";
        case ExtensionType::IMAGE:   return "IMAGE";
        case ExtensionType::AUDIO:   return "AUDIO";
        case ExtensionType::VIDEO:   return "VIDEO";
        case ExtensionType::ARCHIVE: return "ARCHIVE";
        case ExtensionType::BINARY:  return "BINARY";
        default:                     return "NONE";
    }
}

// ========================================================================
// Base Segment (20 × float = 80 bytes)
// ========================================================================
#pragma pack(push, 1)
struct BaseSegment {
    float file_size_log2{0.0f};         // [0]
    float magic_confidence{0.0f};       // [1]
    float printable_ratio{0.0f};        // [2]
    float shannon_entropy{0.0f};        // [3]  normalized [0, 1]
    float min_entropy{0.0f};            // [4]
    float unique_byte_ratio{0.0f};      // [5]
    float mean_byte_norm{0.0f};         // [6]
    float std_byte_norm{0.0f};          // [7]
    float longest_run_log2{0.0f};       // [8]
    float zero_byte_ratio{0.0f};        // [9]
    float high_bit_ratio{0.0f};         // [10]
    float header_entropy{0.0f};         // [11]
    float local_entropy_var{0.0f};      // [12]
    float block_boundary_density{0.0f}; // [13]
    float skewness{0.0f};              // [14]
    float kurtosis{0.0f};              // [15]
    float unique_bigram_ratio{0.0f};    // [16]
    float bigram_topk_conc{0.0f};       // [17]
    float rle_potential{0.0f};          // [18]
    float dict_potential{0.0f};         // [19]

    static constexpr size_t SIZE = 20 * sizeof(float);

    auto reset() -> void {
        std::memset(this, 0, SIZE);
    }

    auto to_vector() const -> std::vector<float> {
        std::vector<float> v(20);
        std::memcpy(v.data(), this, SIZE);
        return v;
    }

    static auto from_vector(const std::vector<float>& v) -> BaseSegment {
        BaseSegment s{};
        size_t n = std::min(v.size(), size_t(20));
        std::memcpy(&s, v.data(), n * sizeof(float));
        return s;
    }
};
#pragma pack(pop)

// ========================================================================
// Extension Data Union (max 8 × float = 32 bytes)
// ========================================================================
#pragma pack(push, 4)
union ExtensionData {
    struct {  // TextCode (5 dims)
        float language_score;
        float syntax_density;
        float line_ending;
        float indent_style;
        float comment_ratio;
    } text;

    struct {  // Image (8 dims)
        float width_norm;
        float height_norm;
        float bit_depth;
        float has_alpha;
        float color_mode;
        float is_lossless_orig;
        float jpeg_quality;
        float compression_savings;
    } image;

    struct {  // Audio (6 dims)
        float sample_rate_norm;
        float bit_depth;
        float channels;
        float duration_norm;
        float is_lossless;
        float bitrate_norm;
    } audio;

    struct {  // Video (7 dims)
        float width_norm;
        float height_norm;
        float fps_norm;
        float duration_norm;
        float codec_type;
        float is_lossless;
        float compression_savings;
    } video;

    struct {  // Archive (4 dims)
        float inner_format;
        float current_ratio;
        float file_count_norm;
        float recompress_potential;
    } archive;

    struct {  // Binary (5 dims)
        float structure_density;
        float padding_ratio;
        float alignment;
        float endianness;
        float exec_score;
    } binary;

    ExtensionData() { std::memset(this, 0, sizeof(*this)); }
};
#pragma pack(pop)

// ========================================================================
// Extraction Metadata
// ========================================================================
struct ExtractionMetadata {
    double extraction_time_ms{0.0};
    DetectionResult detection;
    size_t file_size{0};
};

// ========================================================================
// Composite Feature Vector
// ========================================================================
struct FeatureVectorV3 {
    BaseSegment base;
    ExtractionMetadata meta;
    ExtensionData ext;

    // Convenience fields (direct access, kept in sync with extractor code)
    ExtensionType ext_type{ExtensionType::NONE};
    uint8_t ext_dim{0};

    auto to_padded_array(size_t target_dim = 33) const -> std::vector<float> {
        std::vector<float> result(target_dim, 0.0f);
        std::memcpy(result.data(), &base, BaseSegment::SIZE);
        if (ext_type != ExtensionType::NONE && ext_dim > 0) {
            size_t copy_bytes = std::min(static_cast<size_t>(ext_dim) * sizeof(float),
                                         sizeof(ExtensionData));
            std::memcpy(result.data() + 20, &ext, copy_bytes);
        }
        return result;
    }

    auto validate() const -> bool {
        // Check base segment fields are in valid ranges
        if (base.file_size_log2 < 0.0f || base.file_size_log2 > 1.0f) return false;
        if (base.magic_confidence < 0.0f || base.magic_confidence > 1.0f) return false;
        if (base.printable_ratio < 0.0f || base.printable_ratio > 1.0f) return false;
        if (base.shannon_entropy < 0.0f || base.shannon_entropy > 8.0f) return false;
        if (base.unique_byte_ratio < 0.0f || base.unique_byte_ratio > 1.0f) return false;
        if (ext_type != ExtensionType::NONE && ext_dim == 0) return false;
        return true;
    }

    auto to_json() const -> std::string {
        std::string json = "{\n";
        json += "  \"version\": \"3.0-Final\",\n";
        json += "  \"base_segment\": {\n";
        json += "    \"file_size_log2\": " + std::to_string(base.file_size_log2) + ",\n";
        json += "    \"shannon_entropy\": " + std::to_string(base.shannon_entropy) + ",\n";
        json += "    \"unique_byte_ratio\": " + std::to_string(base.unique_byte_ratio) + "\n";
        json += "  },\n";
        json += "  \"extension_segment\": {\n";
        json += "    \"ext_type\": \"" + extension_type_to_string(ext_type) + "\",\n";
        json += "    \"ext_dim\": " + std::to_string(ext_dim) + "\n";
        json += "  },\n";
        json += "  \"detection\": {\n";
        json += "    \"type\": \"" + meta.detection.type_name + "\",\n";
        json += "    \"confidence\": " + std::to_string(meta.detection.confidence) + "\n";
        json += "  },\n";
        json += "  \"extraction_time_ms\": " + std::to_string(meta.extraction_time_ms) + "\n";
        json += "}";
        return json;
    }
};

} // namespace ade
} // namespace compressor