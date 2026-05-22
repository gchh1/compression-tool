/**
 * @file FeatureExtractorV3.hpp
 * @author ADE Module - Main Feature Extraction Pipeline (v3.0)
 * @brief Orchestrates complete feature extraction: Magic Bytes → Base → Extension
 * @version 3.0 Final
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details This is the primary entry point for the v3.0 feature extraction system.
 * It coordinates:
 * 1. File type detection via Magic Bytes
 * 2. Base Segment extraction (20 universal features)
 * 3. Extension Segment extraction (type-specific features)
 * 4. JSON serialization for storage/transmission
 *
 * Usage Example:
 * ```cpp
 * FeatureExtractorV3 extractor;
 *
 * // Load file data
 * std::vector<uint8_t> file_data = load_file("example.png");
 *
 * // Extract features in one call
 * auto result = extractor.extract(file_data);
 *
 * // Access results
 * std::cout << "File type: " << file_type_to_string(result.detection.type) << std::endl;
 * std::cout << "Entropy: " << result.vector.base.shannon_entropy << std::endl;
 * std::cout << "Total dimensions: " << result.vector.total_bytes() << " bytes" << std::endl;
 *
 * // Serialize to JSON for storage
 * std::string json_str = result.to_json();
 * ```
 */

#pragma once

#include "BaseFeatureExtractor.hpp"
#include "ExtensionExtractors.hpp"
#include "FeatureVectorV3.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

// ========================================================================
// FEATURE EXTRACTION RESULT STRUCTURE
// ========================================================================

/**
 * @brief Complete result from feature extraction pipeline
 *
 * Contains all extracted features plus metadata about the extraction process.
 */
struct ExtractionResult {
    FeatureVectorV3 vector;        /// Complete feature vector (base + extension)
    DetectionResult detection;     /// Magic Bytes detection details
    double extraction_time_ms{0}; /// Time taken to extract features (milliseconds)
    size_t input_size{0};         /// Original file size in bytes

    auto to_json() const -> std::string;

    /**
     * @brief Get padded array ready for ML model input
     *
     * Convenience wrapper around FeatureVectorV3::to_padded_array()
     */
    auto to_ml_input(size_t target_dim = constants::MAX_PADDED_DIMENSIONS) const
        -> std::vector<float> {
        return vector.to_padded_array(target_dim);
    }

   private:
    static auto write_base_features(std::ostringstream& json,
                                    const BaseSegment& base) -> void;
    static auto write_extension_features(std::ostringstream& json,
                                         const FeatureVectorV3& vec) -> void;
    static auto write_ext_field(std::ostringstream& json, const char* name,
                                float value, bool first = false,
                                bool last = false) -> void;
    static auto get_timestamp() -> std::string;
};

// ========================================================================
// MAIN FEATURE EXTRACTOR CLASS
// ========================================================================

/**
 * @brief Primary interface for v3.0 feature extraction
 *
 * This class orchestrates the entire extraction pipeline:
 * 1. Magic Bytes file type detection
 * 2. Base Segment feature extraction (20 dims)
 * 3. Extension Segment feature extraction (5-8 dims based on type)
 * 4. Result assembly and timing
 *
 * Thread Safety: This class is stateless after construction.
 * Multiple instances can extract features concurrently on different files.
 *
 * Performance Characteristics:
 * - Time Complexity: O(n) single pass through file data
 * - Working Memory: ~130KB (64KB Count-Min Sketch + accumulators)
 * - Output Size: 100-112 bytes per file (padded to 132 bytes for ML)
 */
class FeatureExtractorV3 {
   public:
    FeatureExtractorV3() = default;

    auto extract(const uint8_t* data, size_t size) const -> ExtractionResult;

    auto extract(const std::vector<uint8_t>& data) const -> ExtractionResult;

    auto extract_from_file(const std::string& filepath) const -> ExtractionResult;

    auto batch_extract(const std::vector<std::string>& filepaths) const
        -> std::vector<ExtractionResult>;

    static auto get_memory_statistics() -> std::string;

   private:
    MagicBytesDetector magic_detector_;
    BaseFeatureExtractor base_extractor_;

    TextCodeExtractor text_extractor_;
    ImageExtractor image_extractor_;
    AudioExtractor audio_extractor_;
    VideoExtractor video_extractor_;
    ArchiveExtractor archive_extractor_;
    BinaryExtractor binary_extractor_;

    auto extract_extension(const uint8_t* data, size_t size,
                           const DetectionResult& detection,
                           FeatureVectorV3& vector) const -> void;
};

}  // namespace ade
}  // namespace compressor
