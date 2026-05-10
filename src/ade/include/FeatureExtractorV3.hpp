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
#include "MagicBytesDetector.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
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

    /**
     * @brief Serialize result to JSON string
     *
     * Produces human-readable JSON with all features and metadata.
     * Format follows specification in Part 5 of v3.0 document.
     *
     * @return JSON-formatted string containing all extracted data
     */
    auto to_json() const -> std::string {
        std::ostringstream json;
        json << std::setprecision(6) << std::fixed;

        json << "{\n";
        json << "  \"version\": \"3.0-Final\",\n";
        json << "  \"timestamp\": \"" << get_timestamp() << "\",\n";
        json << "  \"file_size\": " << input_size << ",\n";

        // Detection info
        json << "  \"detection\": {\n";
        json << "    \"file_type\": \"" << file_type_to_string(detection.type)
             << "\",\n";
        json << "    \"confidence\": " << detection.confidence << ",\n";
        json << "    \"extension_type\": "
             << static_cast<int>(detection.ext_type) << "\n";
        json << "  },\n";

        // Base segment (20 dimensions)
        json << "  \"base_segment\": {\n";
        write_base_features(json, vector.base);
        json << "  },\n";

        // Extension segment (variable dimensions based on type)
        json << "  \"extension_segment\": {\n";
        json << "    \"type\": " << static_cast<int>(vector.ext_type) << ",\n";
        json << "    \"dimensions\": " << static_cast<int>(vector.ext_dim) << ",\n";
        json << "    \"features\": ";
        write_extension_features(json, vector);
        json << "\n  },\n";

        // Metadata
        json << "  \"metadata\": {\n";
        json << "    \"extraction_time_ms\": " << extraction_time_ms << ",\n";
        json << "    \"total_vector_bytes\": " << vector.total_bytes() << ",\n";
        json << "    \"padded_for_ml\": " << constants::MAX_PADDED_DIMENSIONS
             << " dims (" << constants::MAX_PADDED_SIZE << " bytes)\n";
        json << "  }\n";

        json << "}\n";

        return json.str();
    }

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
    /**
     * @brief Write Base Segment features to JSON stream
     */
    static auto write_base_features(std::ostringstream& json,
                                    const BaseSegment& base) -> void {
        const char* names[] = {
            "file_size_log2",       /// [0]
            "magic_confidence",     /// [1]
            "printable_ratio",      /// [2]
            "shannon_entropy",      /// [3]
            "min_entropy",          /// [4]
            "unique_byte_ratio",    /// [5]
            "mean_byte_norm",       /// [6]
            "std_byte_norm",        /// [7]
            "longest_run_log2",     /// [8]
            "zero_byte_ratio",      /// [9]
            "high_bit_ratio",       /// [10]
            "header_entropy",       /// [11]
            "local_entropy_var",    /// [12]
            "block_boundary_density", /// [13]
            "skewness",             /// [14] 🔑 KEY
            "kurtosis",             /// [15]
            "unique_bigram_ratio",  /// [16]
            "bigram_topk_conc",     /// [17]
            "rle_potential",        /// [18]
            "dict_potential"        /// [19]
        };

        const float* values = &base.file_size_log2;

        for (size_t i = 0; i < BaseSegment::NUM_DIMENSIONS; ++i) {
            json << "    \"" << names[i] << "\": " << values[i];
            if (i < BaseSegment::NUM_DIMENSIONS - 1) json << ",";
            json << "\n";
        }
    }

    /**
     * @brief Write Extension Segment features to JSON stream
     */
    static auto write_extension_features(std::ostringstream& json,
                                         const FeatureVectorV3& vec) -> void {
        switch (vec.ext_type) {
            case ExtensionType::TEXT:
                json << "{\n";
                write_ext_field(json, "language_score", vec.ext.text.language_score, true);
                write_ext_field(json, "syntax_density", vec.ext.text.syntax_density);
                write_ext_field(json, "line_ending", vec.ext.text.line_ending);
                write_ext_field(json, "indentation_style", vec.ext.text.indentation_style);
                write_ext_field(json, "comment_ratio", vec.ext.text.comment_ratio, false, true);
                json << "    }";
                break;

            case ExtensionType::IMAGE:
                json << "{\n";
                write_ext_field(json, "width_norm", vec.ext.image.width_norm, true);
                write_ext_field(json, "height_norm", vec.ext.image.height_norm);
                write_ext_field(json, "bit_depth", vec.ext.image.bit_depth);
                write_ext_field(json, "has_alpha", vec.ext.image.has_alpha);
                write_ext_field(json, "color_mode", vec.ext.image.color_mode);
                write_ext_field(json, "is_lossless_original",
                               vec.ext.image.is_lossless_original);  /// 🔑 KEY
                write_ext_field(json, "jpeg_quality_estimate",
                               vec.ext.image.jpeg_quality_estimate);
                write_ext_field(json, "compression_savings",
                               vec.ext.image.compression_savings, false, true);
                json << "    }";
                break;

            case ExtensionType::AUDIO:
                json << "{\n";
                write_ext_field(json, "sample_rate_norm", vec.ext.audio.sample_rate_norm, true);
                write_ext_field(json, "bit_depth", vec.ext.audio.bit_depth);
                write_ext_field(json, "channels", vec.ext.audio.channels);
                write_ext_field(json, "duration_norm", vec.ext.audio.duration_norm);
                write_ext_field(json, "is_lossless", vec.ext.audio.is_lossless);
                write_ext_field(json, "bitrate_norm", vec.ext.audio.bitrate_norm, false, true);
                json << "    }";
                break;

            case ExtensionType::VIDEO:
                json << "{\n";
                write_ext_field(json, "width_norm", vec.ext.video.width_norm, true);
                write_ext_field(json, "height_norm", vec.ext.video.height_norm);
                write_ext_field(json, "fps_norm", vec.ext.video.fps_norm);
                write_ext_field(json, "duration_norm", vec.ext.video.duration_norm);
                write_ext_field(json, "codec_type", vec.ext.video.codec_type);
                write_ext_field(json, "is_lossless", vec.ext.video.is_lossless);
                write_ext_field(json, "compression_savings",
                               vec.ext.video.compression_savings, false, true);
                json << "    }";
                break;

            case ExtensionType::ARCHIVE:
                json << "{\n";
                write_ext_field(json, "inner_format", vec.ext.archive.inner_format, true);
                write_ext_field(json, "current_ratio", vec.ext.archive.current_ratio);
                write_ext_field(json, "file_count_norm", vec.ext.archive.file_count_norm);
                write_ext_field(json, "recompress_potential",
                               vec.ext.archive.recompress_potential, false, true);
                json << "    }";
                break;

            case ExtensionType::BINARY:
                json << "{\n";
                write_ext_field(json, "structure_density",
                               vec.ext.binary.structure_density, true);
                write_ext_field(json, "padding_ratio", vec.ext.binary.padding_ratio);
                write_ext_field(json, "alignment", vec.ext.binary.alignment);
                write_ext_field(json, "endianness", vec.ext.binary.endianness);
                write_ext_field(json, "exec_score",
                               vec.ext.binary.exec_score, false, true);
                json << "    }";
                break;

            default:  // NONE or unknown
                json << "null";
                break;
        }
    }

    /**
     * @brief Helper to write a single extension field to JSON
     */
    static auto write_ext_field(std::ostringstream& json, const char* name,
                                float value, bool first = false,
                                bool last = false) -> void {
        json << "      \"" << name << "\": " << value;
        if (!last) json << ",";
        json << "\n";
    }

    /**
     * @brief Generate ISO 8601 timestamp string
     */
    static auto get_timestamp() -> std::string {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }
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
    /**
     * @brief Construct extractor with pre-initialized sub-components
     */
    FeatureExtractorV3()
        : magic_detector_(),
          base_extractor_(),
          text_extractor_(),
          image_extractor_(),
          audio_extractor_(),
          video_extractor_(),
          archive_extractor_(),
          binary_extractor_() {}

    /**
     * @brief Extract complete feature vector from file data
     *
     * This is the main entry point for feature extraction.
     * Performs all steps in optimized order.
     *
     * @param data Pointer to file contents in memory
     * @param size Size of file in bytes
     * @return Complete ExtractionResult with all features populated
     */
    auto extract(const uint8_t* data, size_t size) const -> ExtractionResult {
        auto start_time = std::chrono::high_resolution_clock::now();

        ExtractionResult result;
        result.vector.reset();

        // Validate inputs
        if (data == nullptr || size == 0) {
            result.input_size = 0;
            result.detection.type = FileType::UNKNOWN;
            result.detection.confidence = 0.0f;
            result.detection.ext_type = ExtensionType::NONE;
            result.extraction_time_ms = 0.0;
            return result;
        }

        result.input_size = size;

        // ==============================================================
        // STEP 1: FILE TYPE DETECTION (Magic Bytes)
        // ==============================================================
        result.detection = magic_detector_.detect(data, size);

        // ==============================================================
        // STEP 2: BASE SEGMENT EXTRACTION (20 dimensions)
        // ==============================================================
        auto base_result = base_extractor_.extract(data, size, result.detection);
        result.vector.base = base_result.base;

        // ==============================================================
        // STEP 3: EXTENSION SEGMENT EXTRACTION (type-dependent)
        // ==============================================================
        extract_extension(data, size, result.detection, result.vector);

        // ==============================================================
        // STEP 4: FINALIZE AND TIME
        // ==============================================================
        auto end_time = std::chrono::high_resolution_clock::now();
        result.extraction_time_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();

        // Validate vector integrity
        if (!result.vector.validate()) {
            // Log warning but continue (should not happen with correct implementation)
        }

        return result;
    }

    /**
     * @brief Convenience overload for std::vector<uint8_t>
     */
    auto extract(const std::vector<uint8_t>& data) const -> ExtractionResult {
        return extract(data.data(), data.size());
    }

    /**
     * @brief Extract features from file on disk
     *
     * Loads file into memory and extracts features.
     * For very large files (>100MB), consider streaming approach.
     *
     * @param filepath Path to file on disk
     * @return ExtractionResult, or error result if file cannot be read
     */
    auto extract_from_file(const std::string& filepath) const -> ExtractionResult {
        // Open file in binary mode
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            ExtractionResult error_result;
            error_result.input_size = 0;
            error_result.detection.type = FileType::UNKNOWN;
            error_result.detection.confidence = 0.0f;
            return error_result;
        }

        // Get file size
        auto file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read entire file into memory
        std::vector<uint8_t> data(static_cast<size_t>(file_size));
        if (!data.empty()) {
            file.read(reinterpret_cast<char*>(data.data()), file_size);
        }
        file.close();

        return extract(data);
    }

    /**
     * @brief Batch extract features from multiple files
     *
     * Useful for building test sets or processing directories.
     *
     * @param filepaths List of file paths to process
     * @return Vector of ExtractionResults (one per file)
     */
    auto batch_extract(const std::vector<std::string>& filepaths) const
        -> std::vector<ExtractionResult> {
        std::vector<ExtractionResult> results;
        results.reserve(filepaths.size());

        for (const auto& path : filepaths) {
            results.push_back(extract_from_file(path));
        }

        return results;
    }

    /**
     * @brief Get statistics about last extraction(s)
     *
     * Returns memory usage information for profiling/monitoring.
     */
    static auto get_memory_statistics() -> std::string {
        std::ostringstream stats;
        stats << "Memory Statistics:\n";
        stats << "  Count-Min Sketch: " << BaseFeatureExtractor::CMSType::memory_size()
             << " bytes (" << (BaseFeatureExtractor::CMSType::memory_size() / 1024)
             << " KB)\n";
        stats << "  Byte Histogram: 1024 bytes (1 KB)\n";
        stats << "  Statistical Accumulators: <256 bytes\n";
        stats << "  Local Entropy Buffer: ~40 KB (max)\n";
        stats << "  Total Working Memory: ~105-130 KB\n";
        stats << "  Output Vector Size: 100-112 bytes (unpadded)\n";
        stats << "  Padded ML Input: " << constants::MAX_PADDED_SIZE << " bytes ("
             << constants::MAX_PADDED_DIMENSIONS << " dims)\n";
        return stats.str();
    }

   private:
    // Sub-component extractors (stateless, can be reused across calls)
    MagicBytesDetector magic_detector_;
    BaseFeatureExtractor base_extractor_;

    TextCodeExtractor text_extractor_;
    ImageExtractor image_extractor_;
    AudioExtractor audio_extractor_;
    VideoExtractor video_extractor_;
    ArchiveExtractor archive_extractor_;
    BinaryExtractor binary_extractor_;

    /**
     * @brief Dispatch to appropriate extension extractor based on file type
     */
    auto extract_extension(const uint8_t* data, size_t size,
                           const DetectionResult& detection,
                           FeatureVectorV3& vector) const -> void {

        vector.ext_type = detection.ext_type;

        switch (detection.ext_type) {
            case ExtensionType::TEXT: {
                auto ext = text_extractor_.extract(data, size, detection.type);
                vector.ext.text = ext;
                vector.ext_dim = TextCodeExtension::NUM_DIMENSIONS;
                break;
            }

            case ExtensionType::IMAGE: {
                auto ext = image_extractor_.extract(data, size, detection.type);
                vector.ext.image = ext;
                vector.ext_dim = ImageExtension::NUM_DIMENSIONS;
                break;
            }

            case ExtensionType::AUDIO: {
                auto ext = audio_extractor_.extract(data, size, detection.type);
                vector.ext.audio = ext;
                vector.ext_dim = AudioExtension::NUM_DIMENSIONS;
                break;
            }

            case ExtensionType::VIDEO: {
                auto ext = video_extractor_.extract(data, size, detection.type);
                vector.ext.video = ext;
                vector.ext_dim = VideoExtension::NUM_DIMENSIONS;
                break;
            }

            case ExtensionType::ARCHIVE: {
                auto ext = archive_extractor_.extract(data, size, detection.type);
                vector.ext.archive = ext;
                vector.ext_dim = ArchiveExtension::NUM_DIMENSIONS;
                break;
            }

            case ExtensionType::BINARY: {
                auto ext = binary_extractor_.extract(data, size, detection.type);
                vector.ext.binary = ext;
                vector.ext_dim = BinaryExtension::NUM_DIMENSIONS;
                break;
            }

            case ExtensionType::NONE:
            default:
                vector.ext_dim = 0;
                break;
        }
    }
};

}  // namespace ade
}  // namespace compressor