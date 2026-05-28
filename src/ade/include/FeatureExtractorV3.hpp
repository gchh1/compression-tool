#pragma once

#include "ade_debug_log.h"
#include "BaseFeatureExtractor.hpp"
#include "ExtensionExtractors.hpp"
#include "FeatureVectorV3.hpp"
#include "MagicBytesDetector.hpp"

#include <cstdint>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

// ========================================================================
// Extension dimension constants (for NUM_DIMENSIONS lookups)
// ========================================================================
struct TextCodeExtension  { static constexpr uint8_t NUM_DIMENSIONS = 5; };
struct ImageExtension     { static constexpr uint8_t NUM_DIMENSIONS = 8; };
struct AudioExtension     { static constexpr uint8_t NUM_DIMENSIONS = 6; };
struct VideoExtension     { static constexpr uint8_t NUM_DIMENSIONS = 7; };
struct ArchiveExtension   { static constexpr uint8_t NUM_DIMENSIONS = 4; };
struct BinaryExtension    { static constexpr uint8_t NUM_DIMENSIONS = 5; };

inline auto extension_type_to_num_dims(ExtensionType type) -> uint8_t {
    switch (type) {
        case ExtensionType::TEXT:    return TextCodeExtension::NUM_DIMENSIONS;
        case ExtensionType::IMAGE:   return ImageExtension::NUM_DIMENSIONS;
        case ExtensionType::AUDIO:   return AudioExtension::NUM_DIMENSIONS;
        case ExtensionType::VIDEO:   return VideoExtension::NUM_DIMENSIONS;
        case ExtensionType::ARCHIVE: return ArchiveExtension::NUM_DIMENSIONS;
        case ExtensionType::BINARY:  return BinaryExtension::NUM_DIMENSIONS;
        default:                     return 0;
    }
}

inline auto file_type_to_extension_type(FileType ft) -> ExtensionType {
    switch (ft) {
        case FileType::TEXT:
        case FileType::HTML:
        case FileType::CSS:
        case FileType::JAVASCRIPT:
        case FileType::JSON:
        case FileType::XML:
            return ExtensionType::TEXT;
        case FileType::IMAGE:
            return ExtensionType::IMAGE;
        case FileType::AUDIO:
            return ExtensionType::AUDIO;
        case FileType::VIDEO:
            return ExtensionType::VIDEO;
        case FileType::COMPRESSED:
        case FileType::PDF:
        case FileType::OFFICE:
            return ExtensionType::ARCHIVE;
        case FileType::BINARY:
        case FileType::EXECUTABLE:
            return ExtensionType::BINARY;
        default:
            return ExtensionType::NONE;
    }
}

// ========================================================================
// ExtractionResult
// ========================================================================
struct ExtractionResult {
    FeatureVectorV3 vector;
    DetectionResult detection;
    double extraction_time_ms{0.0};
    size_t input_size{0};

    auto to_json() const -> std::string {
        return vector.to_json();
    }

    auto to_ml_input() const -> std::vector<float> {
        return vector.to_padded_array(constants::MAX_PADDED_DIMENSIONS);
    }
};

// ========================================================================
// FeatureExtractorV3
// ========================================================================
class FeatureExtractorV3 {
public:
    FeatureExtractorV3() = default;

    auto extract(const uint8_t* data, size_t size) -> ExtractionResult {
        ExtractionResult result;
        result.input_size = (data == nullptr) ? 0 : size;

        if (data == nullptr || size == 0) {
            result.detection.type = FileType::UNKNOWN;
            result.detection.confidence = 0.0f;
            result.detection.type_name = "UNKNOWN";
            result.vector.ext_type = ExtensionType::NONE;
            result.vector.ext_dim = 0;
            return result;
        }

        auto start_time = std::clock();

        // Stage 1: Magic Bytes Detection
        ade_debug_write("extract: stage1 calling detector_.detect_with_fallback");
        result.detection = detector_.detect_with_fallback(data, size);
        ade_debug_writef("extract: stage1 detect done type=%d", static_cast<int>(result.detection.type));

        // Stage 2: Base Segment Extraction (20 dims)
        ade_debug_write("extract: stage2 calling base_extractor_.extract");
        auto base_result = base_extractor_.extract(data, size, result.detection);
        ade_debug_writef("extract: stage2 base done, entropy=%.3f", base_result.base.shannon_entropy);
        result.vector.base = base_result.base;

        // Stage 3: Extension Type Mapping
        auto ext_type = file_type_to_extension_type(result.detection.type);
        result.vector.ext_type = ext_type;
        result.vector.ext_dim = extension_type_to_num_dims(ext_type);

        // Stage 4: Extension Segment Extraction
        if (ext_type != ExtensionType::NONE) {
            ade_debug_writef("extract: stage4 calling extractExtensionFeatures type=%d", static_cast<int>(ext_type));
            extractExtensionFeatures(data, size, ext_type, result.vector.ext);
            ade_debug_write("extract: stage4 extension done");
        } else {
            ade_debug_write("extract: stage4 skipped (NONE)");
        }

        // Fill metadata
        result.vector.meta.detection = result.detection;
        result.vector.meta.file_size = size;

        auto end_time = std::clock();
        result.extraction_time_ms = 1000.0 * static_cast<double>(end_time - start_time) / CLOCKS_PER_SEC;
        result.vector.meta.extraction_time_ms = result.extraction_time_ms;

        return result;
    }

    auto extract(const std::vector<uint8_t>& data) -> ExtractionResult {
        return extract(data.data(), data.size());
    }

    static auto get_memory_statistics() -> std::string {
        std::ostringstream oss;
        oss << "FeatureExtractorV3 Memory Statistics:\n";
        oss << "  CountMinSketch: "
            << (constants::CMS_WIDTH * constants::CMS_DEPTH * sizeof(uint32_t))
            << " bytes\n";
        oss << "  FeatureVectorV3: " << sizeof(FeatureVectorV3) << " bytes\n";
        oss << "  MagicBytesDetector signatures: " << MagicBytesDetector::get_signature_count() << "\n";
        return oss.str();
    }

private:
    BaseFeatureExtractor base_extractor_;
    MagicBytesDetector detector_;
};

} // namespace ade
} // namespace compressor