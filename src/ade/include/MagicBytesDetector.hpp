/**
 * @file MagicBytesDetector.hpp
 * @author ADE Module - Magic Byte Detection
 * @brief File type detection via magic byte signatures
 * @version 3.0
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details Detects file types by matching magic byte signatures (~40 patterns).
 * Provides confidence scores and type classification for the feature extraction
 * pipeline. Supports all major file categories: images, audio, video, archives,
 * executables, documents, and text-based formats.
 */

#pragma once

#include "ade_debug_log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

// ========================================================================
// File Type Enum (for feature extraction and rule engine)
// ========================================================================
enum class FileType : uint8_t {
    UNKNOWN = 0,
    TEXT,
    HTML,
    CSS,
    JAVASCRIPT,
    JSON,
    XML,
    IMAGE,
    AUDIO,
    VIDEO,
    BINARY,
    COMPRESSED,
    PDF,
    OFFICE,
    EXECUTABLE,
};

auto file_type_to_string(FileType type) -> std::string;

// ========================================================================
// Detection Result
// ========================================================================
struct DetectionResult {
    FileType type{FileType::UNKNOWN};
    uint8_t file_type_id{0};       // mapped type index
    std::string type_name;         // human-readable type name
    float confidence{0.0f};        // detection confidence [0, 1]
    std::string magic_hex;         // detected magic bytes hex string (debug)
};

// ========================================================================
// Magic signature entry
// ========================================================================
static constexpr size_t MAX_SIG_LEN = 32;

struct MagicSignature {
    uint8_t data[MAX_SIG_LEN]; // signature bytes (inline storage)
    size_t length;             // signature length
    uint8_t file_type_id;      // mapped type index
    const char* type_name;     // human-readable name
    float confidence;          // base confidence
};

// ========================================================================
// Magic Bytes Detector
// ========================================================================
class MagicBytesDetector {
public:
    MagicBytesDetector();

    auto detect(const uint8_t* data, size_t size) const -> DetectionResult;

    auto detect_with_fallback(const uint8_t* data, size_t size) const -> DetectionResult;

    static auto get_signature_count() -> size_t { return kSignatureCount; }

    static auto get_signature(size_t index) -> const MagicSignature*;

private:
    static const MagicSignature kSignatures[];
    static constexpr size_t kSignatureCount = 39;

    auto detect_text_or_binary(const uint8_t* data, size_t size) const -> DetectionResult;
    auto hex_string(const uint8_t* data, size_t length) const -> std::string;
};

} // namespace ade
} // namespace compressor

// ========================================================================
// Inline Implementations
// ========================================================================

namespace compressor {
namespace ade {

inline auto file_type_to_string(FileType type) -> std::string {
    switch (type) {
        case FileType::TEXT:        return "TEXT";
        case FileType::HTML:        return "HTML";
        case FileType::CSS:         return "CSS";
        case FileType::JAVASCRIPT:  return "JAVASCRIPT";
        case FileType::JSON:        return "JSON";
        case FileType::XML:         return "XML";
        case FileType::IMAGE:       return "IMAGE";
        case FileType::AUDIO:       return "AUDIO";
        case FileType::VIDEO:       return "VIDEO";
        case FileType::BINARY:      return "BINARY";
        case FileType::COMPRESSED:  return "COMPRESSED";
        case FileType::PDF:         return "PDF";
        case FileType::OFFICE:      return "OFFICE";
        case FileType::EXECUTABLE:  return "EXECUTABLE";
        default:                    return "UNKNOWN";
    }
}

// ========================================================================
// Magic signature table (40 signatures covering major file formats)
// ========================================================================

// Constexpr helper to create MagicSignature with inline storage
static constexpr MagicSignature make_sig(const uint8_t* arr, size_t len,
                                         uint8_t id, const char* name, float conf) {
    MagicSignature sig{};
    sig.length = len;
    sig.file_type_id = id;
    sig.type_name = name;
    sig.confidence = conf;
    for (size_t i = 0; i < len; ++i) {
        sig.data[i] = arr[i];
    }
    return sig;
}

// Static storage for signature data
namespace {

const uint8_t SIG_PNG[]      = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
const uint8_t SIG_JPEG[]     = {0xFF, 0xD8, 0xFF};
const uint8_t SIG_GIF[]      = {0x47, 0x49, 0x46, 0x38};
const uint8_t SIG_BMP[]      = {0x42, 0x4D};
const uint8_t SIG_WEBP[]     = {0x52, 0x49, 0x46, 0x46};
const uint8_t SIG_TIFF_LE[]  = {0x49, 0x49, 0x2A, 0x00};
const uint8_t SIG_TIFF_BE[]  = {0x4D, 0x4D, 0x00, 0x2A};
const uint8_t SIG_RIFF_WAV[] = {0x52, 0x49, 0x46, 0x46};
const uint8_t SIG_FLAC[]     = {0x66, 0x4C, 0x61, 0x43};
const uint8_t SIG_MP3_ID3[]  = {0x49, 0x44, 0x33};
const uint8_t SIG_MP3_SYNC[] = {0xFF, 0xFB};
const uint8_t SIG_OGG[]      = {0x4F, 0x67, 0x67, 0x53};
const uint8_t SIG_WMA[]      = {0x30, 0x26, 0xB2, 0x75};
const uint8_t SIG_MP4[]      = {0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70};
const uint8_t SIG_AVI[]      = {0x52, 0x49, 0x46, 0x46};
const uint8_t SIG_MKV[]      = {0x1A, 0x45, 0xDF, 0xA3};
const uint8_t SIG_WMV[]      = {0x30, 0x26, 0xB2, 0x75};
const uint8_t SIG_ZIP[]      = {0x50, 0x4B, 0x03, 0x04};
const uint8_t SIG_ZIP_EMPTY[] = {0x50, 0x4B, 0x05, 0x06};
const uint8_t SIG_ZIP_SPAN[] = {0x50, 0x4B, 0x07, 0x08};
const uint8_t SIG_GZIP[]     = {0x1F, 0x8B};
const uint8_t SIG_BZ2[]      = {0x42, 0x5A, 0x68};
const uint8_t SIG_LZMA[]     = {0x5D, 0x00, 0x00};
const uint8_t SIG_XZ[]       = {0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00};
const uint8_t SIG_ZSTD[]     = {0x28, 0xB5, 0x2F, 0xFD};
const uint8_t SIG_RAR[]      = {0x52, 0x61, 0x72, 0x21};
const uint8_t SIG_7Z[]       = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};
const uint8_t SIG_PDF[]      = {0x25, 0x50, 0x44, 0x46};
const uint8_t SIG_DOC[]      = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
const uint8_t SIG_DOCX[]     = {0x50, 0x4B, 0x03, 0x04};
const uint8_t SIG_PE[]       = {0x4D, 0x5A};
const uint8_t SIG_ELF[]      = {0x7F, 0x45, 0x4C, 0x46};
const uint8_t SIG_MACHO[]    = {0xFE, 0xED, 0xFA, 0xCE};
const uint8_t SIG_MACHO64[]  = {0xFE, 0xED, 0xFA, 0xCF};
const uint8_t SIG_MACHO_LE[] = {0xCE, 0xFA, 0xED, 0xFE};
const uint8_t SIG_MACHO64_LE[] = {0xCF, 0xFA, 0xED, 0xFE};
const uint8_t SIG_HTML[]     = {0x3C, 0x68, 0x74, 0x6D, 0x6C};
const uint8_t SIG_XML[]      = {0x3C, 0x3F, 0x78, 0x6D, 0x6C};
const uint8_t SIG_RTF[]      = {0x7B, 0x5C, 0x72, 0x74, 0x66};

} // anonymous namespace

inline const MagicSignature MagicBytesDetector::kSignatures[] = {
    // Images
    make_sig(SIG_PNG,        sizeof(SIG_PNG),        6,  "PNG Image",             0.98f),
    make_sig(SIG_JPEG,       sizeof(SIG_JPEG),       6,  "JPEG Image",            0.95f),
    make_sig(SIG_GIF,        sizeof(SIG_GIF),        6,  "GIF Image",             0.95f),
    make_sig(SIG_BMP,        sizeof(SIG_BMP),        6,  "BMP Image",             0.90f),
    make_sig(SIG_WEBP,       sizeof(SIG_WEBP),       6,  "WebP Image",            0.85f),
    make_sig(SIG_TIFF_LE,    sizeof(SIG_TIFF_LE),    6,  "TIFF Image (LE)",       0.90f),
    make_sig(SIG_TIFF_BE,    sizeof(SIG_TIFF_BE),    6,  "TIFF Image (BE)",       0.90f),

    // Audio
    make_sig(SIG_RIFF_WAV,   sizeof(SIG_RIFF_WAV),   7,  "WAV Audio",             0.85f),
    make_sig(SIG_FLAC,       sizeof(SIG_FLAC),       7,  "FLAC Audio",            0.95f),
    make_sig(SIG_MP3_ID3,    sizeof(SIG_MP3_ID3),    7,  "MP3 Audio (ID3)",       0.90f),
    make_sig(SIG_MP3_SYNC,   sizeof(SIG_MP3_SYNC),   7,  "MP3 Audio",             0.80f),
    make_sig(SIG_OGG,        sizeof(SIG_OGG),        7,  "OGG Audio",             0.90f),
    make_sig(SIG_WMA,        sizeof(SIG_WMA),        7,  "WMA Audio",             0.70f),

    // Video
    make_sig(SIG_MP4,        sizeof(SIG_MP4),        8,  "MP4 Video",             0.85f),
    make_sig(SIG_AVI,        sizeof(SIG_AVI),        8,  "AVI Video",             0.80f),
    make_sig(SIG_MKV,        sizeof(SIG_MKV),        8,  "MKV Video",             0.90f),
    make_sig(SIG_WMV,        sizeof(SIG_WMV),        8,  "WMV Video",             0.70f),

    // Archives / Compressed
    make_sig(SIG_ZIP,        sizeof(SIG_ZIP),        11, "ZIP Archive",           0.95f),
    make_sig(SIG_ZIP_EMPTY,  sizeof(SIG_ZIP_EMPTY),  11, "ZIP Archive (empty)",   0.90f),
    make_sig(SIG_ZIP_SPAN,   sizeof(SIG_ZIP_SPAN),   11, "ZIP Archive (span)",    0.90f),
    make_sig(SIG_GZIP,       sizeof(SIG_GZIP),       11, "GZip Compressed",       0.95f),
    make_sig(SIG_BZ2,        sizeof(SIG_BZ2),        11, "BZip2 Compressed",      0.95f),
    make_sig(SIG_LZMA,       sizeof(SIG_LZMA),       11, "LZMA Compressed",       0.80f),
    make_sig(SIG_XZ,         sizeof(SIG_XZ),         11, "XZ Compressed",         0.90f),
    make_sig(SIG_ZSTD,       sizeof(SIG_ZSTD),       11, "Zstd Compressed",       0.90f),
    make_sig(SIG_RAR,        sizeof(SIG_RAR),        11, "RAR Archive",           0.95f),
    make_sig(SIG_7Z,         sizeof(SIG_7Z),         11, "7z Archive",            0.95f),

    // Documents
    make_sig(SIG_PDF,        sizeof(SIG_PDF),        12, "PDF Document",          0.98f),
    make_sig(SIG_DOC,        sizeof(SIG_DOC),        13, "OLE2 Document",         0.85f),
    make_sig(SIG_DOCX,       sizeof(SIG_DOCX),       13, "OOXML Document",        0.80f),
    make_sig(SIG_RTF,        sizeof(SIG_RTF),        13, "RTF Document",          0.85f),

    // Executables
    make_sig(SIG_PE,         sizeof(SIG_PE),         14, "PE Executable",         0.90f),
    make_sig(SIG_ELF,        sizeof(SIG_ELF),        14, "ELF Executable",        0.95f),
    make_sig(SIG_MACHO,      sizeof(SIG_MACHO),      14, "Mach-O (PPC BE)",       0.90f),
    make_sig(SIG_MACHO64,    sizeof(SIG_MACHO64),    14, "Mach-O 64 (PPC BE)",    0.90f),
    make_sig(SIG_MACHO_LE,   sizeof(SIG_MACHO_LE),   14, "Mach-O (x86 LE)",       0.90f),
    make_sig(SIG_MACHO64_LE, sizeof(SIG_MACHO64_LE), 14, "Mach-O 64 (x86 LE)",    0.90f),

    // Markup / Text
    make_sig(SIG_HTML,       sizeof(SIG_HTML),       1,  "HTML Document",         0.85f),
    make_sig(SIG_XML,        sizeof(SIG_XML),        5,  "XML Document",          0.85f),
};

inline MagicBytesDetector::MagicBytesDetector() {}

inline auto MagicBytesDetector::detect(const uint8_t* data, size_t size) const -> DetectionResult {
    DetectionResult result;

    if (data == nullptr || size == 0) {
        result.type = FileType::UNKNOWN;
        result.type_name = "UNKNOWN";
        return result;
    }

    ade_debug_writef("detect: ENTER size=%zu sig_count=%zu", size, kSignatureCount);

    size_t best_match_len = 0;

    for (size_t i = 0; i < kSignatureCount; ++i) {
        const auto& sig = kSignatures[i];

        if (size < sig.length) {
            ade_debug_writef("detect: [%zu] skip %s len=%zu > size", i, sig.type_name, sig.length);
            continue;
        }

        ade_debug_writef("detect: [%zu] memcmp %s len=%zu data_ptr=%p sig_ptr=%p",
                i, sig.type_name, sig.length, (const void*)data, (const void*)sig.data);

        if (std::memcmp(data, sig.data, sig.length) == 0) {
            ade_debug_writef("detect: [%zu] MATCH %s", i, sig.type_name);
            if (sig.length > best_match_len) {
                best_match_len = sig.length;
                result.type = static_cast<FileType>(sig.file_type_id);
                result.file_type_id = sig.file_type_id;
                result.type_name = sig.type_name;
                result.confidence = sig.confidence;
                result.magic_hex = hex_string(sig.data, sig.length);
            }
        }
    }

    ade_debug_writef("detect: loop done best_match_len=%zu confidence=%.2f",
            best_match_len, result.confidence);

    if (result.confidence == 0.0f) {
        ade_debug_write("detect: calling detect_text_or_binary");
        return detect_text_or_binary(data, size);
    }

    return result;
}

inline auto MagicBytesDetector::detect_with_fallback(const uint8_t* data, size_t size) const -> DetectionResult {
    ade_debug_write("detect_with_fallback: ENTER");
    auto result = detect(data, size);
    ade_debug_writef("detect_with_fallback: detect done type=%d conf=%.2f",
            static_cast<int>(result.type), result.confidence);

    if (result.confidence < 0.5f) {
        ade_debug_write("detect_with_fallback: trying text_or_binary fallback");
        auto fallback = detect_text_or_binary(data, size);
        if (fallback.confidence > result.confidence) {
            result = fallback;
            ade_debug_writef("detect_with_fallback: fallback chosen type=%d conf=%.2f",
                    static_cast<int>(result.type), result.confidence);
        }
    }

    ade_debug_write("detect_with_fallback: EXIT");
    return result;
}

inline auto MagicBytesDetector::get_signature(size_t index) -> const MagicSignature* {
    if (index >= kSignatureCount) return nullptr;
    return &kSignatures[index];
}

inline auto MagicBytesDetector::detect_text_or_binary(const uint8_t* data, size_t size) const -> DetectionResult {
    DetectionResult result;

    if (data == nullptr || size == 0) {
        result.type = FileType::UNKNOWN;
        result.type_name = "UNKNOWN";
        return result;
    }

    // Analyze first 4096 bytes for text/binary heuristics
    size_t scan = std::min(size, size_t(4096));
    size_t printable = 0;
    size_t control = 0;
    size_t null_bytes = 0;

    for (size_t i = 0; i < scan; ++i) {
        uint8_t b = data[i];
        if (b >= 0x20 && b <= 0x7E) {
            ++printable;
        } else if (b == 0x09 || b == 0x0A || b == 0x0D) {
            ++printable;  // tab, LF, CR count as printable for text detection
        } else if (b == 0x00) {
            ++null_bytes;
        } else if (b < 0x20) {
            ++control;
        }
    }

    float printable_ratio = static_cast<float>(printable) / static_cast<float>(scan);

    // Heuristic thresholds
    if (null_bytes > scan / 10) {
        // Binary: significant null byte presence
        result.type = FileType::BINARY;
        result.file_type_id = 9;
        result.type_name = "Binary Data";
        result.confidence = std::min(0.8f, 0.5f + static_cast<float>(null_bytes) / static_cast<float>(scan));
    } else if (printable_ratio > 0.85f) {
        // Text: high ratio of printable characters
        result.type = FileType::TEXT;
        result.file_type_id = 1;
        result.type_name = "Text File";
        result.confidence = std::min(0.7f, printable_ratio - 0.3f);

        // Check for HTML/XML patterns in text
        for (size_t i = 0; i + 4 < scan; ++i) {
            if (data[i] == '<' && data[i+1] == 'h' && data[i+2] == 't' && data[i+3] == 'm' && data[i+4] == 'l') {
                result.type = FileType::HTML;
                result.file_type_id = 1;
                result.type_name = "HTML Document";
                result.confidence = 0.85f;
                break;
            }
            if (data[i] == '<' && data[i+1] == '?' && data[i+2] == 'x' && data[i+3] == 'm' && data[i+4] == 'l') {
                result.type = FileType::XML;
                result.file_type_id = 5;
                result.type_name = "XML Document";
                result.confidence = 0.85f;
                break;
            }
        }
    } else {
        // Likely binary
        result.type = FileType::BINARY;
        result.file_type_id = 9;
        result.type_name = "Binary Data";
        result.confidence = 0.5f + (1.0f - printable_ratio) * 0.3f;
    }

    return result;
}

inline auto MagicBytesDetector::hex_string(const uint8_t* data, size_t length) const -> std::string {
    const char hex_chars[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(length * 3);
    for (size_t i = 0; i < length; ++i) {
        if (i > 0) result += ' ';
        result += hex_chars[(data[i] >> 4) & 0x0F];
        result += hex_chars[data[i] & 0x0F];
    }
    return result;
}

} // namespace ade
} // namespace compressor