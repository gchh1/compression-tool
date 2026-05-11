#include "FeatureExtractorV3.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace compressor {
namespace ade {

auto ExtractionResult::to_json() const -> std::string {
    std::ostringstream json;
    json << std::setprecision(6) << std::fixed;

    json << "{\n";
    json << "  \"version\": \"3.0-Final\",\n";
    json << "  \"timestamp\": \"" << get_timestamp() << "\",\n";
    json << "  \"file_size\": " << input_size << ",\n";

    json << "  \"detection\": {\n";
    json << "    \"file_type\": \"" << file_type_to_string(detection.type)
         << "\",\n";
    json << "    \"confidence\": " << detection.confidence << ",\n";
    json << "    \"extension_type\": "
         << static_cast<int>(detection.ext_type) << "\n";
    json << "  },\n";

    json << "  \"base_segment\": {\n";
    write_base_features(json, vector.base);
    json << "  },\n";

    json << "  \"extension_segment\": {\n";
    json << "    \"type\": " << static_cast<int>(vector.ext_type) << ",\n";
    json << "    \"dimensions\": " << static_cast<int>(vector.ext_dim) << ",\n";
    json << "    \"features\": ";
    write_extension_features(json, vector);
    json << "\n  },\n";

    json << "  \"metadata\": {\n";
    json << "    \"extraction_time_ms\": " << extraction_time_ms << ",\n";
    json << "    \"total_vector_bytes\": " << vector.total_bytes() << ",\n";
    json << "    \"padded_for_ml\": " << constants::MAX_PADDED_DIMENSIONS
         << " dims (" << constants::MAX_PADDED_SIZE << " bytes)\n";
    json << "  }\n";

    json << "}\n";

    return json.str();
}

auto ExtractionResult::write_base_features(std::ostringstream& json,
                                           const BaseSegment& base) -> void {
    const char* names[] = {
        "file_size_log2",
        "magic_confidence",
        "printable_ratio",
        "shannon_entropy",
        "min_entropy",
        "unique_byte_ratio",
        "mean_byte_norm",
        "std_byte_norm",
        "longest_run_log2",
        "zero_byte_ratio",
        "high_bit_ratio",
        "header_entropy",
        "local_entropy_var",
        "block_boundary_density",
        "skewness",
        "kurtosis",
        "unique_bigram_ratio",
        "bigram_topk_conc",
        "rle_potential",
        "dict_potential"
    };

    const float* values = &base.file_size_log2;

    for (size_t i = 0; i < BaseSegment::NUM_DIMENSIONS; ++i) {
        json << "    \"" << names[i] << "\": " << values[i];
        if (i < BaseSegment::NUM_DIMENSIONS - 1) json << ",";
        json << "\n";
    }
}

auto ExtractionResult::write_extension_features(std::ostringstream& json,
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
                           vec.ext.image.is_lossless_original);
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

        default:
            json << "null";
            break;
    }
}

auto ExtractionResult::write_ext_field(std::ostringstream& json, const char* name,
                                       float value, bool first, bool last) -> void {
    (void)first;
    json << "      \"" << name << "\": " << value;
    if (!last) json << ",";
    json << "\n";
}

auto ExtractionResult::get_timestamp() -> std::string {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

auto FeatureExtractorV3::extract(const uint8_t* data, size_t size) const -> ExtractionResult {
    auto start_time = std::chrono::high_resolution_clock::now();

    ExtractionResult result;
    result.vector.reset();

    if (data == nullptr || size == 0) {
        result.input_size = 0;
        result.detection.type = FileType::UNKNOWN;
        result.detection.confidence = 0.0f;
        result.detection.ext_type = ExtensionType::NONE;
        result.extraction_time_ms = 0.0;
        return result;
    }

    result.input_size = size;

    result.detection = magic_detector_.detect(data, size);

    auto base_result = base_extractor_.extract(data, size, result.detection);
    result.vector.base = base_result.base;

    extract_extension(data, size, result.detection, result.vector);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.extraction_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    (void)result.vector.validate();

    return result;
}

auto FeatureExtractorV3::extract(const std::vector<uint8_t>& data) const -> ExtractionResult {
    return extract(data.data(), data.size());
}

auto FeatureExtractorV3::extract_from_file(const std::string& filepath) const
    -> ExtractionResult {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ExtractionResult error_result;
        error_result.input_size = 0;
        error_result.detection.type = FileType::UNKNOWN;
        error_result.detection.confidence = 0.0f;
        return error_result;
    }

    auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(file_size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), file_size);
    }
    file.close();

    return extract(data);
}

auto FeatureExtractorV3::batch_extract(const std::vector<std::string>& filepaths) const
    -> std::vector<ExtractionResult> {
    std::vector<ExtractionResult> results;
    results.reserve(filepaths.size());

    for (const auto& path : filepaths) {
        results.push_back(extract_from_file(path));
    }

    return results;
}

auto FeatureExtractorV3::get_memory_statistics() -> std::string {
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

auto FeatureExtractorV3::extract_extension(const uint8_t* data, size_t size,
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

}  // namespace ade
}  // namespace compressor
