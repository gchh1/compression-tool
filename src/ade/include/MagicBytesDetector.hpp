/**
 * @file MagicBytesDetector.hpp
 * @author ADE Module - Magic Bytes (File Signature) Detector
 * @brief Reliable file type detection using file header byte patterns
 * @version 3.0
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details Implements Google Magika-inspired file type detection using
 * Magic Bytes (file signatures) from file headers. Supports 30+ formats
 * with confidence scoring and fallback heuristics.
 *
 * Key advantages over extension-based detection:
 * - Resistant to file renaming attacks
 * - Detects actual content format, not just claimed type
 * - Provides confidence scores for ML integration
 */

#pragma once

#include "FeatureVectorV3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

// ========================================================================
// MAGIC BYTES SIGNATURE DATABASE
// ========================================================================

/**
 * @brief Single magic bytes signature pattern
 */
struct MagicSignature {
    std::vector<uint8_t> pattern;  /// Byte pattern to match
    size_t offset;                 /// Offset in file where pattern starts
    FileType detected_type;        /// File type if pattern matches
    float confidence;              /// Base confidence score [0.0, 1.0]
    std::string description;       /// Human-readable description
};

/**
 * @brief Comprehensive database of file signatures (30+ formats)
 *
 * Signatures are ordered by specificity (most specific first)
 * to avoid false positives from overlapping patterns.
 */
class MagicBytesDatabase {
   public:
    static auto get_signatures() -> const std::vector<MagicSignature>&;

   private:
    static auto initialize() -> std::vector<MagicSignature>;
};

// ========================================================================
// MAGIC BYTES DETECTOR CLASS
// ========================================================================

/**
 * @brief Main detector class for file type identification via Magic Bytes
 *
 * Usage:
 * ```cpp
 * auto detector = MagicBytesDetector();
 * auto result = detector.detect(file_data);
 * if (result.confidence > 0.8) {
 *     std::cout << "Detected: " << file_type_to_string(result.type) << std::endl;
 * }
 * ```
 */
struct DetectionResult {
    FileType type{FileType::UNKNOWN};
    float confidence{0.0f};
    ExtensionType ext_type{ExtensionType::NONE};
};

class MagicBytesDetector {
   public:
    MagicBytesDetector();

    auto detect(const uint8_t* data, size_t size) const -> DetectionResult;

    auto detect(const std::vector<uint8_t>& data) const -> DetectionResult;

    static auto get_type_description(FileType type) -> std::string;

   private:
    const std::vector<MagicSignature>& signatures_;

    auto matches_signature(const uint8_t* data, size_t size,
                           const MagicSignature& sig) const -> bool;

    auto apply_special_rules(const uint8_t* data, size_t size,
                             DetectionResult& result) const -> void;

    auto detect_by_heuristics(const uint8_t* data, size_t size) const
        -> DetectionResult;
};

}  // namespace ade
}  // namespace compressor
