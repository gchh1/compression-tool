#include "FeatureVectorV3.hpp"

#include <cstring>
#include <ostream>

namespace compressor {
namespace ade {

auto file_type_to_string(FileType type) -> std::string {
    switch (type) {
        case FileType::UNKNOWN: return "UNKNOWN";
        case FileType::TEXT_PLAIN: return "TEXT_PLAIN";
        case FileType::IMAGE_PNG: return "PNG";
        case FileType::IMAGE_JPEG: return "JPEG";
        case FileType::AUDIO_WAV: return "WAV";
        case FileType::ARCHIVE_ZIP: return "ZIP";
        case FileType::EXEC_PE: return "PE_EXE";
        case FileType::EXEC_ELF: return "ELF";
        case FileType::DOC_PDF: return "PDF";
        case FileType::DB_SQLITE: return "SQLITE";
        default: return "OTHER";
    }
}

auto operator<<(std::ostream& os, FileType type) -> std::ostream& {
    os << file_type_to_string(type);
    return os;
}

auto extension_type_to_string(ExtensionType type) -> std::string {
    switch (type) {
        case ExtensionType::NONE: return "NONE";
        case ExtensionType::TEXT: return "TEXT";
        case ExtensionType::IMAGE: return "IMAGE";
        case ExtensionType::AUDIO: return "AUDIO";
        case ExtensionType::VIDEO: return "VIDEO";
        case ExtensionType::ARCHIVE: return "ARCHIVE";
        case ExtensionType::BINARY: return "BINARY";
        default: return "UNKNOWN";
    }
}

auto operator<<(std::ostream& os, ExtensionType type) -> std::ostream& {
    os << extension_type_to_string(type);
    return os;
}

auto map_file_type_to_extension(FileType file_type) -> ExtensionType {
    switch (file_type) {
        case FileType::TEXT_PLAIN:
        case FileType::TEXT_UTF8:
        case FileType::SOURCE_C:
        case FileType::SOURCE_CPP:
        case FileType::SOURCE_PYTHON:
        case FileType::SOURCE_JAVA:
        case FileType::SOURCE_RUST:
        case FileType::SOURCE_GO:
        case FileType::MARKDOWN:
        case FileType::HTML:
        case FileType::XML:
        case FileType::JSON:
        case FileType::CSS:
        case FileType::JAVASCRIPT:
            return ExtensionType::TEXT;

        case FileType::IMAGE_PNG:
        case FileType::IMAGE_JPEG:
        case FileType::IMAGE_GIF:
        case FileType::IMAGE_BMP:
        case FileType::IMAGE_TIFF_LE:
        case FileType::IMAGE_TIFF_BE:
        case FileType::IMAGE_WEBP:
        case FileType::IMAGE_AVIF:
            return ExtensionType::IMAGE;

        case FileType::AUDIO_WAV:
        case FileType::AUDIO_MP3:
        case FileType::AUDIO_FLAC:
        case FileType::AUDIO_OGG:
        case FileType::AUDIO_AAC:
            return ExtensionType::AUDIO;

        case FileType::VIDEO_MP4:
        case FileType::VIDEO_MKV:
        case FileType::VIDEO_AVI:
        case FileType::VIDEO_FLV:
        case FileType::VIDEO_WEBM:
            return ExtensionType::VIDEO;

        case FileType::ARCHIVE_ZIP:
        case FileType::ARCHIVE_GZIP:
        case FileType::ARCHIVE_BZIP2:
        case FileType::ARCHIVE_7Z:
        case FileType::ARCHIVE_RAR:
        case FileType::ARCHIVE_TAR:
        case FileType::ARCHIVE_XZ:
            return ExtensionType::ARCHIVE;

        default:
            return ExtensionType::BINARY;
    }
}

auto BaseSegment::reset() -> void {
    std::memset(this, 0, SIZE);
}

auto BaseSegment::operator[](size_t index) -> float& {
    return (&file_size_log2)[index];
}

auto BaseSegment::operator[](size_t index) const -> const float& {
    return (&file_size_log2)[index];
}

auto FeatureVectorV3::total_bytes() const -> size_t {
    return BaseSegment::SIZE + sizeof(ext_type) + sizeof(ext_dim) +
           ext_dim * sizeof(float);
}

auto FeatureVectorV3::to_padded_array(size_t target_dim) const -> std::vector<float> {
    std::vector<float> result(target_dim, 0.0f);
    std::memcpy(result.data(), &base, BaseSegment::SIZE);
    if (ext_type != ExtensionType::NONE && ext_dim > 0) {
        std::memcpy(result.data() + 20, &ext, ext_dim * sizeof(float));
    }
    return result;
}

auto FeatureVectorV3::reset() -> void {
    base.reset();
    ext_type = ExtensionType::NONE;
    ext_dim = 0;
    std::memset(&ext, 0, sizeof(ext));
}

auto FeatureVectorV3::validate() const -> bool {
    if (ext_type == ExtensionType::NONE) {
        return ext_dim == 0;
    }

    size_t expected_dim = 0;
    switch (ext_type) {
        case ExtensionType::TEXT:    expected_dim = TextCodeExtension::NUM_DIMENSIONS; break;
        case ExtensionType::IMAGE:   expected_dim = ImageExtension::NUM_DIMENSIONS; break;
        case ExtensionType::AUDIO:   expected_dim = AudioExtension::NUM_DIMENSIONS; break;
        case ExtensionType::VIDEO:   expected_dim = VideoExtension::NUM_DIMENSIONS; break;
        case ExtensionType::ARCHIVE: expected_dim = ArchiveExtension::NUM_DIMENSIONS; break;
        case ExtensionType::BINARY:  expected_dim = BinaryExtension::NUM_DIMENSIONS; break;
        default: return false;
    }

    return ext_dim == expected_dim;
}

}  // namespace ade
}  // namespace compressor
