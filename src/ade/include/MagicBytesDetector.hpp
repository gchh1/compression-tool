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

#include <array>
#include <cstdint>
#include <cstring>
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
    /**
     * @brief Get singleton instance of the signature database
     * @return Const reference to sorted signature list
     */
    static auto get_signatures() -> const std::vector<MagicSignature>& {
        static const std::vector<MagicSignature> signatures = initialize();
        return signatures;
    }

   private:
    static auto initialize() -> std::vector<MagicSignature> {
        std::vector<MagicSignature> sigs;

        // ===== ARCHIVE / COMPRESSED FORMATS =====

        // ZIP/PKZIP (also used by JAR, APK, DOCX, XLSX, etc.)
        sigs.push_back({
            {0x50, 0x4B, 0x03, 0x04},  // "PK\x03\x04"
            0,
            FileType::ARCHIVE_ZIP,
            0.95f,
            "ZIP archive (PK signature)"
        });

        // GZIP
        sigs.push_back({
            {0x1F, 0x8B},
            0,
            FileType::ARCHIVE_GZIP,
            0.95f,
            "GZIP compressed data"
        });

        // BZIP2
        sigs.push_back({
            {0x42, 0x5A, 0x68},  // "BZh"
            0,
            FileType::ARCHIVE_BZIP2,
            0.95f,
            "BZIP2 compressed data"
        });

        // 7-Zip
        sigs.push_back({
            {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C},
            0,
            FileType::ARCHIVE_7Z,
            0.95f,
            "7-Zip archive"
        });

        // RAR v5.0+
        sigs.push_back({
            {0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00},  // "Rar!\x1a\x07\x01\x00"
            0,
            FileType::ARCHIVE_RAR,
            0.95f,
            "RAR v5.0+ archive"
        });

        // RAR v1.5-v4.0
        sigs.push_back({
            {0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00},  // "Rar!\x1a\x07\x00"
            0,
            FileType::ARCHIVE_RAR,
            0.90f,
            "RAR v1.5-v4.0 archive"
        });

        // TAR (ustar)
        sigs.push_back({
            {'u', 's', 't', 'a', 'r'},
            257,
            FileType::ARCHIVE_TAR,
            0.90f,
            "TAR archive (ustar)"
        });

        // XZ
        sigs.push_back({
            {0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00},  // "\xfd7zXZ\x00"
            0,
            FileType::ARCHIVE_XZ,
            0.95f,
            "XZ compressed data"
        });

        // ===== IMAGE FORMATS =====

        // PNG
        sigs.push_back({
            {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A},
            0,
            FileType::IMAGE_PNG,
            0.99f,
            "PNG image"
        });

        // JPEG/JFIF
        sigs.push_back({
            {0xFF, 0xD8, 0xFF},
            0,
            FileType::IMAGE_JPEG,
            0.98f,
            "JPEG image"
        });

        // GIF87a
        sigs.push_back({
            {'G', 'I', 'F', '8', '7', 'a'},
            0,
            FileType::IMAGE_GIF,
            0.98f,
            "GIF87a image"
        });

        // GIF89a
        sigs.push_back({
            {'G', 'I', 'F', '8', '9', 'a'},
            0,
            FileType::IMAGE_GIF,
            0.98f,
            "GIF89a image"
        });

        // BMP
        sigs.push_back({
            {'B', 'M'},
            0,
            FileType::IMAGE_BMP,
            0.97f,
            "Windows Bitmap image"
        });

        // TIFF (Little Endian)
        sigs.push_back({
            {0x49, 0x49, 0x2A, 0x00},  // "II*\0" = Little Endian TIFF
            0,
            FileType::IMAGE_TIFF_LE,
            0.96f,
            "TIFF image (Little Endian)"
        });

        // TIFF (Big Endian)
        sigs.push_back({
            {0x4D, 0x4D, 0x00, 0x2A},  // "MM\0*" = Big Endian TIFF
            0,
            FileType::IMAGE_TIFF_BE,
            0.96f,
            "TIFF image (Big Endian)"
        });

        // WebP
        sigs.push_back({
            {'R', 'I', 'F', 'F'},  // RIFF container
            0,
            FileType::IMAGE_WEBP,
            0.90f,
            "WebP image (RIFF container)"
        });
        // Note: WebP needs additional check for "WEBP" at offset 8

        // AVIF (HEIF with AV1 codec)
        sigs.push_back({
            {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66},
            0,
            FileType::IMAGE_AVIF,
            0.92f,
            "AVIF image (HEIF/AV1)"
        });

        // ===== AUDIO FORMATS =====

        // WAV/RIFF audio
        sigs.push_back({
            {'R', 'I', 'F', 'F'},
            0,
            FileType::AUDIO_WAV,
            0.88f,
            "WAV audio (RIFF container)"
        });
        // Note: Needs additional check for "WAVE" at offset 8

        // MP3 (ID3v2 tag)
        sigs.push_back({
            {0x49, 0x44, 0x33},  // "ID3"
            0,
            FileType::AUDIO_MP3,
            0.93f,
            "MP3 audio (ID3v2 tag)"
        });

        // MP3 (MPEG Audio frame sync)
        sigs.push_back({
            {0xFF, 0xFB},
            0,
            FileType::AUDIO_MP3,
            0.85f,
            "MP3 audio (MPEG frame sync)"
        });

        // FLAC
        sigs.push_back({
            {'f', 'L', 'a', 'C'},
            0,
            FileType::AUDIO_FLAC,
            0.99f,
            "FLAC lossless audio"
        });

        // OGG Vorbis/Opus
        sigs.push_back({
            {'O', 'g', 'g', 'S'},
            0,
            FileType::AUDIO_OGG,
            0.97f,
            "OGG audio container"
        });

        // AAC (ADTS frame)
        sigs.push_back({
            {0xFF, 0xF1},  // ADTS sync word
            0,
            FileType::AUDIO_AAC,
            0.85f,
            "AAC audio (ADTS frame)"
        });

        // ===== VIDEO FORMATS =====

        // MP4/MOV (ISO Base Media File Format)
        sigs.push_back({
            {0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70, 0x6D, 0x70, 0x34, 0x32},
            0,
            FileType::VIDEO_MP4,
            0.94f,
            "MP4 video (ftyp mp42)"
        });

        // MP4 alternative (isom)
        sigs.push_back({
            {0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70, 0x69, 0x73, 0x6F, 0x6D},
            0,
            FileType::VIDEO_MP4,
            0.92f,
            "MP4 video (ftyp isom)"
        });

        // MKV/WebM (EBML)
        sigs.push_back({
            {0x1A, 0x45, 0xDF, 0xA3},  // EBML ID
            0,
            FileType::VIDEO_MKV,
            0.95f,
            "MKV/WebM video (EBML)"
        });

        // AVI
        sigs.push_back({
            {'R', 'I', 'F', 'F'},
            0,
            FileType::VIDEO_AVI,
            0.85f,
            "AVI video (RIFF container)"
        });
        // Note: Needs check for "AVI " at offset 8

        // Flash Video (FLV)
        sigs.push_back({
            {'F', 'L', 'V'},
            0,
            FileType::VIDEO_FLV,
            0.95f,
            "Flash Video (FLV)"
        });

        // ===== EXECUTABLE FORMATS =====

        // Windows PE (MZ header + PE signature at offset 0x3C)
        sigs.push_back({
            {'M', 'Z'},
            0,
            FileType::EXEC_PE,
            0.80f,
            "Windows PE executable (MZ header)"
        });
        // Note: Needs secondary validation of PE signature at offset specified in MZ header

        // ELF (32-bit Little Endian)
        sigs.push_back({
            {0x7F, 0x45, 0x4C, 0x46, 0x01, 0x01},  // "\x7FELF\x01\x01"
            0,
            FileType::EXEC_ELF,
            0.97f,
            "ELF executable (32-bit LE)"
        });

        // ELF (64-bit Little Endian)
        sigs.push_back({
            {0x7F, 0x45, 0x4C, 0x46, 0x02, 0x01},  // "\x7FELF\x02\x01"
            0,
            FileType::EXEC_ELF,
            0.97f,
            "ELF executable (64-bit LE)"
        });

        // ELF (32-bit Big Endian)
        sigs.push_back({
            {0x7F, 0x45, 0x4C, 0x46, 0x01, 0x02},  // "\x7FELF\x01\x02"
            0,
            FileType::EXEC_ELF,
            0.97f,
            "ELF executable (32-bit BE)"
        });

        // ELF (64-bit Big Endian)
        sigs.push_back({
            {0x7F, 0x45, 0x4C, 0x46, 0x02, 0x02},  // "\x7FELF\x02\x02"
            0,
            FileType::EXEC_ELF,
            0.97f,
            "ELF executable (64-bit BE)"
        });

        // Mach-O (32-bit)
        sigs.push_back({
            {0xFE, 0xED, 0xFA, 0xCE},
            0,
            FileType::EXEC_MACHO,
            0.95f,
            "Mach-O executable (32-bit)"
        });

        // Mach-O (64-bit)
        sigs.push_back({
            {0xFE, 0xED, 0xFA, 0xCF},
            0,
            FileType::EXEC_MACHO,
            0.95f,
            "Mach-O executable (64-bit)"
        });

        // Mach-O Fat Binary (Universal)
        sigs.push_back({
            {0xCA, 0xFE, 0xBA, 0xBE},
            0,
            FileType::EXEC_MACHO_FAT,
            0.95f,
            "Mach-O Fat Universal binary"
        });

        // ===== DOCUMENT FORMATS =====

        // PDF
        sigs.push_back({
            {'%', 'P', 'D', 'F', '-'},
            0,
            FileType::DOC_PDF,
            0.99f,
            "PDF document"
        });

        // Microsoft Office (OLE2): DOC, XLS, PPT
        sigs.push_back({
            {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1},
            0,
            FileType::DOC_OLE2,
            0.95f,
            "Microsoft Office OLE2 document"
        });

        // DOCX/XLSX/PPTX (actually ZIP-based, already caught above)

        // ===== DATABASE =====

        // SQLite
        sigs.push_back({
            {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '\x03', '\x00'},
            0,
            FileType::DB_SQLITE,
            0.99f,
            "SQLite database"
        });

        // ===== TEXT/CODE DETECTION HEURISTICS =====

        // UTF-8 BOM
        sigs.push_back({
            {0xEF, 0xBB, 0xBF},
            0,
            FileType::TEXT_UTF8,
            0.75f,
            "UTF-8 text with BOM"
        });

        // XML declaration
        sigs.push_back({
            {'<', '?', 'x', 'm', 'l'},
            0,
            FileType::XML,
            0.82f,
            "XML document"
        });

        // HTML
        sigs.push_back({
            {'<', '!','D', 'O', 'C', 'T', 'Y', 'P', 'E'},
            0,
            FileType::HTML,
            0.83f,
            "HTML document (DOCTYPE)"
        });

        // JSON object start
        sigs.push_back({
            {'{'},
            0,
            FileType::JSON,
            0.65f,
            "JSON object (heuristic)"
        });

        return sigs;
    }
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
    /**
     * @brief Default constructor - loads signature database
     */
    MagicBytesDetector() : signatures_(MagicBytesDatabase::get_signatures()) {}

    /**
     * @brief Detect file type from raw file data
     *
     * @param data Pointer to file contents
     * @param size Size of data in bytes
     * @return DetectionResult containing type, confidence, and mapped extension
     */
    auto detect(const uint8_t* data, size_t size) const -> DetectionResult {
        DetectionResult result;

        if (data == nullptr || size < 4) {
            result.type = FileType::UNKNOWN;
            result.confidence = 0.0f;
            result.ext_type = ExtensionType::NONE;
            return result;
        }

        // Try all signatures in order (most specific first)
        for (const auto& sig : signatures_) {
            if (matches_signature(data, size, sig)) {
                result.type = sig.detected_type;
                result.confidence = sig.confidence;

                // Special handling for ambiguous containers
                apply_special_rules(data, size, result);

                break;  // Return first match (signatures ordered by priority)
            }
        }

        // Fallback: heuristic analysis for unknown files
        if (result.type == FileType::UNKNOWN) {
            result = detect_by_heuristics(data, size);
        }

        // Map file type to extension segment type
        result.ext_type = map_file_type_to_extension(result.type);

        return result;
    }

    /**
     * @brief Convenience overload for std::vector<uint8_t>
     */
    auto detect(const std::vector<uint8_t>& data) const -> DetectionResult {
        return detect(data.data(), data.size());
    }

    /**
     * @brief Get human-readable description of last detection
     */
    static auto get_type_description(FileType type) -> std::string {
        for (const auto& sig : MagicBytesDatabase::get_signatures()) {
            if (sig.detected_type == type) {
                return sig.description;
            }
        }
        return "Unknown or heuristic-detected";
    }

   private:
    const std::vector<MagicSignature>& signatures_;

    /**
     * @brief Check if data matches a specific signature pattern
     */
    auto matches_signature(const uint8_t* data, size_t size,
                           const MagicSignature& sig) const -> bool {
        // Check if we have enough data for this signature
        if (size < sig.offset + sig.pattern.size()) {
            return false;
        }

        // Compare bytes at the specified offset
        return std::memcmp(data + sig.offset, sig.pattern.data(),
                          sig.pattern.size()) == 0;
    }

    /**
     * @ Apply special disambiguation rules for container formats
     *
     * Some formats share common headers (e.g., RIFF for both WAV and AVI).
     * This function performs secondary checks to resolve ambiguity.
     */
    auto apply_special_rules(const uint8_t* data, size_t size,
                             DetectionResult& result) const -> void {

        // RIFF container disambiguation (offset 8 contains subtype)
        if (size >= 12 && result.type == FileType::AUDIO_WAV) {
            uint8_t subtype[4];
            std::memcpy(subtype, data + 8, 4);

            if (std::memcmp(subtype, "WAVE", 4) == 0) {
                result.confidence = 0.95f;  // Confirmed as WAV
            } else if (std::memcmp(subtype, "AVI ", 4) == 0) {
                result.type = FileType::VIDEO_AVI;
                result.confidence = 0.90f;
            }
        }

        // WebP verification (must have "WEBP" at offset 8)
        if (size >= 16 && result.type == FileType::IMAGE_WEBP) {
            uint8_t webp_tag[4];
            std::memcpy(webp_tag, data + 8, 4);
            if (std::memcmp(webp_tag, "WEBP", 4) != 0) {
                result.type = FileType::UNKNOWN;
                result.confidence = 0.0f;
            } else {
                result.confidence = 0.96f;
            }
        }

        // PE executable secondary validation
        if (size >= 64 && result.type == FileType::EXEC_PE) {
            // Read PE header offset from MZ header at offset 0x3C
            uint32_t pe_offset;
            std::memcpy(&pe_offset, data + 0x3C, sizeof(pe_offset));

            if (size >= pe_offset + 4) {
                uint32_t pe_sig;
                std::memcpy(&pe_sig, data + pe_offset, sizeof(pe_sig));
                if (pe_sig != 0x00004550) {  // "PE\0\0"
                    result.type = FileType::UNKNOWN;
                    result.confidence = 0.0f;
                } else {
                    result.confidence = 0.92f;
                }
            }
        }
    }

    /**
     * @brief Heuristic detection for files without known signatures
     *
     * Analyzes byte distribution patterns to make educated guesses:
     * - High printable ratio → likely text/code
     * - High entropy → encrypted/compressed/binary
     * - Structured patterns → possibly binary format
     */
    auto detect_by_heuristics(const uint8_t* data, size_t size) const
        -> DetectionResult {
        DetectionResult result;
        result.type = FileType::UNKNOWN;
        result.confidence = 0.3f;  // Low confidence for heuristic detection

        // Analyze first 4096 bytes (or entire file if smaller)
        size_t sample_size = std::min(size, (size_t)4096);

        size_t printable_count = 0;
        size_t whitespace_count = 0;

        for (size_t i = 0; i < sample_size; ++i) {
            uint8_t byte = data[i];

            if ((byte >= 0x20 && byte <= 0x7E) || byte >= 0x80) {
                printable_count++;
            }
            if (byte == 0x09 || byte == 0x0A || byte == 0x0D || byte == 0x20) {
                whitespace_count++;
            }
        }

        float printable_ratio = static_cast<float>(printable_count) / sample_size;
        float whitespace_ratio = static_cast<float>(whitespace_count) / sample_size;

        // Text-like: high printable ratio (>85%) and reasonable whitespace
        if (printable_ratio > 0.85f && whitespace_ratio > 0.05f) {
            result.type = FileType::TEXT_PLAIN;
            result.confidence = 0.6f;
        }
        // Binary-like: low printable ratio
        else if (printable_ratio < 0.3f) {
            result.type = FileType::BINARY_GENERIC;
            result.confidence = 0.5f;
        }

        result.ext_type = map_file_type_to_extension(result.type);

        return result;
    }
};

}  // namespace ade
}  // namespace compressor