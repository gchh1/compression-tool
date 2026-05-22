/**
 * @file ExtensionExtractors.hpp
 * @author ADE Module - File Type-Specific Extension Feature Extractors
 * @brief Specialized extractors for Text, Image, Audio, Video, Archive, Binary
 * @version 3.0
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details Implements 6 specialized feature extractors for file-type-specific
 * extension segments. Each extractor parses format-specific metadata from
 * file headers to populate the corresponding Extension data structure.
 *
 * Extractor List:
 * - TextCodeExtractor (5 dims): Language detection, syntax analysis
 * - ImageExtractor (8 dims): Resolution, bit depth, compression info
 * - AudioExtractor (6 dims): Sample rate, channels, duration
 * - VideoExtractor (7 dims): Resolution, codec, frame rate
 * - ArchiveExtractor (4 dims): Inner format, compression ratio
 * - BinaryExtractor (5 dims): Structure, alignment, executability
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
// TEXT/CODE EXTENSION EXTRACTOR
// ========================================================================

/**
 * @brief Extract features for text files and source code
 *
 * Analyzes character patterns to determine:
 * - Natural language vs code content
 * - Syntax density (brackets, semicolons, etc.)
 * - Line ending conventions (LF vs CRLF)
 * - Indentation style (spaces vs tabs)
 * - Comment density (for source code)
 */
class TextCodeExtractor {
   public:
    /**
     * @brief Extract text/code extension features
     *
     * Scans first N bytes (default 16KB) for efficient analysis.
     * For very large files, sampling is sufficient.
     *
     * @param data File data pointer
     * @param size File size
     * @param type Detected file type (for language hints)
     * @return Filled TextCodeExtension structure
     */
    auto extract(const uint8_t* data, size_t size, FileType type) const
        -> TextCodeExtension {
        TextCodeExtension ext{};
        std::memset(&ext, 0, sizeof(ext));

        if (data == nullptr || size == 0) return ext;

        // Sample size for text analysis (first 16KB or entire file if smaller)
        size_t sample_size = std::min(size, (size_t)16384);

        // Counters for various text metrics
        size_t alpha_count = 0;
        size_t syntax_chars = 0;    // { } ( ) [ ] ; : , < > / \ # @ $ % ^ & * | ~ !
        size_t lf_count = 0;        // Line feed (0x0A)
        size_t cr_count = 0;        // Carriage return (0x0D)
        size_t crlf_count = 0;      // CRLF pairs
        size_t space_indent = 0;    // Space indentation (2+ spaces at line start)
        size_t tab_indent = 0;      // Tab indentation
        size_t comment_lines = 0;   // Lines starting with // or #
        size_t total_lines = 1;     // At least one line
        bool in_line_start = true;

        // Common English letter frequency for entropy calculation
        std::array<uint64_t, 256> char_freq{};
        char_freq.fill(0);

        for (size_t i = 0; i < sample_size; ++i) {
            uint8_t ch = data[i];
            ++char_freq[ch];

            // Alpha characters (letters)
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
                ++alpha_count;
            }

            // Syntax characters (code indicators)
            if (is_syntax_char(ch)) {
                ++syntax_chars;
            }

            // Line ending tracking
            if (ch == 0x0A) {  // LF
                ++lf_count;
                ++total_lines;

                if (i > 0 && data[i - 1] == 0x0D) {
                    ++crlf_count;
                }

                in_line_start = true;
            } else if (ch == 0x0D) {  // CR
                ++cr_count;
            }

            // Indentation tracking
            if (in_line_start) {
                if (ch == ' ') {
                    // Check for 2+ consecutive spaces
                    if (i + 1 < sample_size && data[i + 1] == ' ') {
                        ++space_indent;
                    }
                } else if (ch == '\t') {
                    ++tab_indent;
                    in_line_start = false;  // Tab ends indent check
                } else if (ch != ' ' && ch != '\t' && ch != 0x0D) {
                    in_line_start = false;

                    // Comment detection (only at line start after indent)
                    if (ch == '/' || ch == '#') {
                        if (ch == '/' && i + 1 < sample_size && data[i + 1] == '/') {
                            ++comment_lines;
                        } else if (ch == '#') {
                            ++comment_lines;
                        }
                    }
                }
            }
        }

        // === COMPUTE FEATURES ===

        // [20] Language score: 0=pure code, 1=natural language
        // Based on alpha-to-syntax ratio and entropy of character distribution
        double total_printable = alpha_count + syntax_chars;
        ext.language_score = (total_printable > 0)
                                 ? static_cast<float>(alpha_count) /
                                       static_cast<float>(total_printable)
                                 : 0.5f;

        // Adjust based on file type hint
        if (type == FileType::MARKDOWN || type == FileType::TEXT_PLAIN ||
            type == FileType::TEXT_UTF8) {
            ext.language_score = std::min(1.0f, ext.language_score + 0.2f);
        } else if (is_source_code_type(type)) {
            ext.language_score = std::max(0.0f, ext.language_score - 0.2f);
        }

        // [21] Syntax density
        ext.syntax_density = (sample_size > 0)
                                ? static_cast<float>(syntax_chars) / sample_size
                                : 0.0f;

        // [22] Line ending: 0=LF, 0.5=mixed, 1=CRLF
        if (crlf_count > 0 && lf_count > crlf_count) {
            ext.line_ending = 0.5f;  // Mixed
        } else if (crlf_count > 0 && lf_count == crlf_count) {
            ext.line_ending = 1.0f;  // Pure CRLF
        } else {
            ext.line_ending = 0.0f;  // Pure LF
        }

        // [23] Indentation style: 0=spaces, 1=tabs, 0.5=mixed
        size_t total_indents = space_indent + tab_indent;
        if (total_indents > 0) {
            ext.indentation_style = static_cast<float>(tab_indent) / total_indents;
        } else {
            ext.indentation_style = 0.5f;  // Unknown/none
        }

        // [24] Comment ratio
        ext.comment_ratio = (total_lines > 0)
                               ? static_cast<float>(comment_lines) / total_lines
                               : 0.0f;

        return ext;
    }

   private:
    static auto is_syntax_char(uint8_t ch) -> bool {
        switch (ch) {
            case '{': case '}': case '(': case ')':
            case '[': case ']': case ';': case ':':
            case ',': case '<': case '>': case '/':
            case '\\': case '#': case '@': case '$':
            case '%': case '^': case '&': case '*':
            case '|': case '~': case '!': case '=':
            case '+': case '-': case '.': case '?':
                return true;
            default:
                return false;
        }
    }

    static auto is_source_code_type(FileType type) -> bool {
        return type == FileType::SOURCE_C ||
               type == FileType::SOURCE_CPP ||
               type == FileType::SOURCE_PYTHON ||
               type == FileType::SOURCE_JAVA ||
               type == FileType::SOURCE_RUST ||
               type == FileType::SOURCE_GO ||
               type == FileType::JAVASCRIPT ||
               type == FileType::CSS ||
               type == FileType::JSON ||
               type == FileType::XML ||
               type == FileType::HTML;
    }
};

// ========================================================================
// IMAGE EXTENSION EXTRACTOR
// ========================================================================

/**
 * @brief Extract image metadata and compressibility features
 *
 * Parses image file headers to extract resolution, color depth,
 * compression parameters, and estimates recompression potential.
 *
 * KEY FEATURE: is_lossless_original determines if lossy→lossless
 * conversion is viable (e.g., JPEG → PNG conversion).
 */
class ImageExtractor {
   public:
    auto extract(const uint8_t* data, size_t size, FileType type) const
        -> ImageExtension {
        ImageExtension ext{};
        std::memset(&ext, 0, sizeof(ext));

        if (data == nullptr || size < 8) return ext;

        switch (type) {
            case FileType::IMAGE_PNG:
                ext = extract_png(data, size);
                break;
            case FileType::IMAGE_JPEG:
                ext = extract_jpeg(data, size);
                break;
            case FileType::IMAGE_BMP:
                ext = extract_bmp(data, size);
                break;
            case FileType::IMAGE_GIF:
                ext = extract_gif(data, size);
                break;
            default:
                // For other formats, try generic extraction
                ext = extract_generic_image(data, size, type);
                break;
        }

        return ext;
    }

    // ================================================================
    // PUBLIC UTILITY FUNCTIONS (shared by other extractors)
    // ================================================================

    static auto normalize_dimension(uint32_t value, uint32_t norm_base) -> float {
        return std::min(static_cast<float>(value) / static_cast<float>(norm_base),
                       1.0f);
    }

    static auto map_png_color_type(uint8_t ct) -> float {
        switch (ct) {
            case 0: return 0.0f;   // Grayscale
            case 2: return 1.0f;   // RGB
            case 3: return 4.0f;   // Indexed
            case 4: return 0.0f;   // Gray+Alpha (map to gray)
            case 6: return 2.0f;   // RGBA
            default: return 1.0f;
        }
    }

    static auto find_jpeg_marker(const uint8_t* data, size_t size,
                                  uint8_t marker_type) -> size_t {
        for (size_t i = 0; i < size - 1; ++i) {
            if (data[i] == 0xFF && data[i + 1] == marker_type) {
                return i;
            }
            if (data[i] == 0xFF && data[i + 1] == 0xFF) {
                continue;
            }
        }
        return 0;
    }

    static auto read_be16(const uint8_t* data, size_t offset) -> uint16_t {
        return (static_cast<uint16_t>(data[offset]) << 8) |
               static_cast<uint16_t>(data[offset + 1]);
    }

    static auto read_be32(const uint8_t* data, size_t offset) -> uint32_t {
        return (static_cast<uint32_t>(data[offset]) << 24) |
               (static_cast<uint32_t>(data[offset + 1]) << 16) |
               (static_cast<uint32_t>(data[offset + 2]) << 8) |
               static_cast<uint32_t>(data[offset + 3]);
    }

    static auto read_le16(const uint8_t* data, size_t offset) -> uint16_t {
        return static_cast<uint16_t>(data[offset]) |
               (static_cast<uint16_t>(data[offset + 1]) << 8);
    }

    static auto read_le32(const uint8_t* data, size_t offset) -> uint32_t {
        return static_cast<uint32_t>(data[offset]) |
               (static_cast<uint32_t>(data[offset + 1]) << 8) |
               (static_cast<uint32_t>(data[offset + 2]) << 16) |
               (static_cast<uint32_t>(data[offset + 3]) << 24);
    }

    static auto read_le32_signed(const uint8_t* data, size_t offset) -> int32_t {
        return static_cast<int32_t>(read_le32(data, offset));
    }

   private:
    /**
     * @brief Parse PNG IHDR chunk for width, height, bit depth, color type
     */
    auto extract_png(const uint8_t* data, size_t size) const -> ImageExtension {
        ImageExtension ext{};

        // PNG signature: 8 bytes, then chunk header at offset 8
        // IHDR chunk should be first chunk after signature
        if (size < 24) return ext;

        // Check for "IHDR" at offset 16 (after length + type)
        if (std::memcmp(data + 12, "IHDR", 4) != 0) return ext;

        // Read width (4 bytes big-endian) at offset 16
        uint32_t width = read_be32(data, 16);
        uint32_t height = read_be32(data, 20);

        ext.width_norm = normalize_dimension(width, 4096);
        ext.height_norm = normalize_dimension(height, 4096);
        ext.bit_depth = static_cast<float>(data[24]);  // Bit depth per channel
        ext.has_alpha = (data[25] & 0x04) ? 1.0f : 0.0f;

        // Color type: 0=gray, 2=RGB, 3=indexed, 4=gray+alpha, 6=RGBA
        uint8_t color_type = data[25];
        ext.color_mode = map_png_color_type(color_type);

        // PNG is always lossless
        ext.is_lossless_original = 1.0f;
        ext.jpeg_quality_estimate = 0.0f;  // Not applicable

        // Estimate compression savings (PNG already well-compressed)
        ext.compression_savings = 0.05f;  // Minimal additional savings expected

        return ext;
    }

    /**
     * @brief Parse JPEG SOF marker for dimensions and quality estimation
     */
    auto extract_jpeg(const uint8_t* data, size_t size) const -> ImageExtension {
        ImageExtension ext{};

        // Find SOF0 (Start Of Frame - Baseline DCT) marker
        // Markers: 0xFF 0xC0 (SOF0), 0xFF 0xC2 (SOF2 - Progressive)
        size_t sof_offset = find_jpeg_marker(data, size, 0xC0);
        if (sof_offset == 0) {
            sof_offset = find_jpeg_marker(data, size, 0xC2);  // Try progressive
        }

        if (sof_offset == 0 || sof_offset + 9 > size) return ext;

        // Precision (bits per sample) at SOF+2
        ext.bit_depth = static_cast<float>(data[sof_offset + 2]);

        // Height (2 bytes big-endian) at SOF+3
        uint16_t height = read_be16(data, sof_offset + 3);
        uint16_t width = read_be16(data, sof_offset + 5);

        ext.width_norm = normalize_dimension(width, 4096);
        ext.height_norm = normalize_dimension(height, 4096);

        // Number of components at SOF+7 (1=grayscale, 3=YCbCr/YIQ, 4=CMYK)
        uint8_t num_components = data[sof_offset + 7];

        ext.has_alpha = 0.0f;
        ext.color_mode = (num_components == 1) ? 0.0f :
                         (num_components == 3) ? 1.0f : 3.0f;  // gray/RGB/CMYK

        // JPEG is always lossy (DCT-based)
        ext.is_lossless_original = 0.0f;

        // Estimate JPEG quality from quantization tables (heuristic)
        ext.jpeg_quality_estimate = estimate_jpeg_quality(data, size);

        // Estimate potential savings from re-compression
        // High quality JPEGs can be re-compressed more aggressively
        if (ext.jpeg_quality_estimate > 85.0f) {
            ext.compression_savings = 0.3f;  // Significant savings possible
        } else if (ext.jpeg_quality_estimate > 70.0f) {
            ext.compression_savings = 0.15f;
        } else {
            ext.compression_savings = 0.05f;  // Already compressed well
        }

        return ext;
    }

    /**
     * @brief Parse BMP header (BITMAPINFOHEADER)
     */
    auto extract_bmp(const uint8_t* data, size_t size) const -> ImageExtension {
        ImageExtension ext{};

        if (size < 54) return ext;  // Minimum BMP header size

        // Width at offset 18 (signed 32-bit LE)
        int32_t width = read_le32_signed(data, 18);
        int32_t height = read_le32_signed(data, 22);

        ext.width_norm = normalize_dimension(std::abs(width), 4096);
        ext.height_norm = normalize_dimension(std::abs(height), 4096);

        // Bits per pixel at offset 28
        ext.bit_depth = static_cast<float>(read_le16(data, 28));

        // Compression method at offset 30 (0=uncompressed, 1=RLE8, 2=RLE4)
        uint32_t compression = read_le32(data, 30);

        // Color mode from bit depth
        if (ext.bit_depth <= 8) {
            ext.color_mode = 4.0f;  // Indexed/palette
        } else if (ext.bit_depth == 24) {
            ext.color_mode = 1.0f;  // RGB
        } else if (ext.bit_depth == 32) {
            ext.has_alpha = 1.0f;
            ext.color_mode = 2.0f;  // RGBA
        }

        // Uncompressed BMP is lossless original (excellent candidate for PNG conversion!)
        ext.is_lossless_original = (compression == 0) ? 1.0f : 0.0f;

        // Large uncompressed BMPs can achieve massive savings with PNG
        if (compression == 0 && ext.bit_depth >= 24) {
            ext.compression_savings = 0.7f;  // Very high savings potential
        } else {
            ext.compression_savings = 0.2f;
        }

        return ext;
    }

    /**
     * @brief Parse GIF header (GIF89a/GIF87a)
     */
    auto extract_gif(const uint8_t* data, size_t size) const -> ImageExtension {
        ImageExtension ext{};

        if (size < 10) return ext;

        // Logical Screen Descriptor at offset 6
        uint16_t width = read_le16(data, 6);
        uint16_t height = read_le16(data, 8);

        ext.width_norm = normalize_dimension(width, 4096);
        ext.height_norm = normalize_dimension(height, 4096);

        // Packed byte at offset 10 contains global Color Table Flag and color depth
        uint8_t packed = data[10];
        ext.bit_depth = static_cast<float>(((packed >> 4) & 0x07) + 1);  // Size of GCT

        // GIF supports transparency via GCT but not full alpha channel
        ext.has_alpha = 0.0f;
        ext.color_mode = 4.0f;  // Indexed color table

        // GIF is lossless (LZW compression)
        ext.is_lossless_original = 1.0f;
        ext.compression_savings = 0.1f;  // Already reasonably compressed

        return ext;
    }

    /**
     * @brief Generic fallback for unsupported image formats
     */
    auto extract_generic_image(const uint8_t* data, size_t size,
                               FileType type) const -> ImageExtension {
        ImageExtension ext{};

        // Attempt basic dimension extraction from common container formats
        // This is a best-effort implementation

        ext.width_norm = 0.5f;  // Unknown
        ext.height_norm = 0.5f;
        ext.bit_depth = 24.0f;  // Common default
        ext.has_alpha = 0.0f;
        ext.color_mode = 1.0f;  // Assume RGB
        ext.is_lossless_original = 0.5f;  // Unknown
        ext.jpeg_quality_estimate = 0.0f;
        ext.compression_savings = 0.15f;

        return ext;
    }

    // ================================================================
    // JPEG QUALITY ESTIMATION HEURISTICS
    // ================================================================

    /**
     * @brief Estimate JPEG quality factor from quantization tables
     *
     * Uses heuristic: sum of luminance quantization values correlates
     * inversely with quality setting.
     *
     * Quality mapping (approximate):
     * - Sum < 500: quality ≥ 95
     * - Sum 500-1000: quality 80-94
     * - Sum 1000-2000: quality 60-79
     * - Sum > 2000: quality ≤ 60
     */
    auto estimate_jpeg_quality(const uint8_t* data, size_t size) const -> float {
        // Find DQT (Define Quantization Table) marker: 0xFF 0xDB
        size_t dqt_offset = find_jpeg_marker(data, size, 0xDB);
        if (dqt_offset == 0 || dqt_offset + 70 > size) return 75.0f;  // Default guess

        // Skip marker and length (dqt_offset points to 0xFF, so +2 for FF DB, +2 for length)
        size_t table_start = dqt_offset + 4;

        // First byte after precision/table_id: Pq Tq
        // Pq: precision (0=8-bit, 1=16-bit)
        uint8_t pq = data[table_start] >> 4;
        size_t entry_size = (pq == 0) ? 1 : 2;

        // Sum first 64 quantization values (luminance table)
        uint64_t q_sum = 0;
        size_t entries_to_read = std::min((size_t)64, (size - table_start - 1) / entry_size);

        for (size_t i = 0; i < entries_to_read; ++i) {
            if (entry_size == 1) {
                q_sum += data[table_start + 1 + i];
            } else {
                q_sum += read_be16(data, table_start + 1 + i * 2);
            }
        }

        // Map sum to quality estimate (inverse relationship)
        if (q_sum < 400) return 98.0f;
        if (q_sum < 600) return 92.0f;
        if (q_sum < 800) return 85.0f;
        if (q_sum < 1000) return 78.0f;
        if (q_sum < 1500) return 68.0f;
        if (q_sum < 2000) return 58.0f;
        if (q_sum < 3000) return 45.0f;
        return 35.0f;  // Very low quality
    }
};

// ========================================================================
// AUDIO EXTENSION EXTRACTOR
// ========================================================================

/**
 * @brief Extract audio metadata features
 *
 * Supports WAV (RIFF), MP3 (ID3/MPEG frames), FLAC, OGG formats.
 * Extracts technical audio parameters relevant to compression decisions.
 */
class AudioExtractor {
   public:
    auto extract(const uint8_t* data, size_t size, FileType type) const
        -> AudioExtension {
        AudioExtension ext{};
        std::memset(&ext, 0, sizeof(ext));

        if (data == nullptr || size < 8) return ext;

        switch (type) {
            case FileType::AUDIO_WAV:
                ext = extract_wav(data, size);
                break;
            case FileType::AUDIO_MP3:
                ext = extract_mp3(data, size);
                break;
            case FileType::AUDIO_FLAC:
                ext = extract_flac(data, size);
                break;
            default:
                ext = extract_generic_audio(data, size, type);
                break;
        }

        return ext;
    }

   private:
    auto extract_wav(const uint8_t* data, size_t size) const -> AudioExtension {
        AudioExtension ext{};

        // WAV uses RIFF container, look for "fmt " sub-chunk
        // RIFF header: "RIFF" (4) + size (4) + "WAVE" (4) = 12 bytes
        if (size < 44) return ext;  // Need at least fmt chunk

        // Find "fmt " chunk
        size_t fmt_offset = find_riff_chunk(data, size, "fmt ");
        if (fmt_offset == 0 || fmt_offset + 24 > size) return ext;

        // fmt chunk layout (offsets from chunk_id):
        // +0: "fmt " (4 bytes)
        // +4: chunk size (4 bytes)
        // +8: audio format (2 bytes)
        // +10: channels (2 bytes)
        // +12: sample rate (4 bytes)
        // +16: byte rate (4 bytes)
        // +20: block align (2 bytes)
        // +22: bits per sample (2 bytes)
        uint16_t audio_format = ImageExtractor::read_le16(data, fmt_offset + 8);

        // Channels at fmt+10
        ext.channels = static_cast<float>(ImageExtractor::read_le16(data, fmt_offset + 10));

        // Sample rate at fmt+12
        uint32_t sample_rate = ImageExtractor::read_le32(data, fmt_offset + 12);
        ext.sample_rate_norm = std::min(static_cast<float>(sample_rate) / 192000.0f,
                                        1.0f);  // Normalize to max 192kHz

        // Bits per sample at fmt+22
        ext.bit_depth = static_cast<float>(ImageExtractor::read_le16(data, fmt_offset + 22));

        // Duration from file size (approximate)
        // PCM: duration = data_size / (sample_rate * channels * bits_per_sample / 8)
        size_t data_chunk_offset = find_riff_chunk(data, size, "data");
        if (data_chunk_offset != 0 && data_chunk_offset + 4 <= size) {
            uint32_t data_size = ImageExtractor::read_le32(data, data_chunk_offset + 4);
            uint32_t byte_rate = sample_rate * static_cast<uint32_t>(ext.channels) *
                                static_cast<uint32_t>(ext.bit_depth) / 8;

            if (byte_rate > 0) {
                uint32_t duration_sec = data_size / byte_rate;
                ext.duration_norm = std::min(static_cast<float>(duration_sec) / 3600.0f,
                                            1.0f);  // Normalize to 1 hour
            }
        }

        // WAV with PCM is lossless
        ext.is_lossless = (audio_format == 1) ? 1.0f : 0.0f;

        // Bitrate calculation
        uint32_t bitrate = sample_rate * static_cast<uint32_t>(ext.channels) *
                          static_cast<uint32_t>(ext.bit_depth);
        ext.bitrate_norm = std::min(static_cast<float>(bitrate) / (1411200.0f),
                                   1.0f);  // Normalize to ~1.4 Mbps (CD quality × many channels)

        return ext;
    }

    auto extract_mp3(const uint8_t* data, size_t size) const -> AudioExtension {
        AudioExtension ext{};

        // MP3 is lossy by nature
        ext.is_lossless = 0.0f;

        // Find first MPEG audio frame sync (0xFF 0xFB or similar)
        size_t frame_offset = 0;
        for (size_t i = 0; i < size - 1; ++i) {
            if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0) {
                frame_offset = i;
                break;
            }
        }

        if (frame_offset == 0 || frame_offset + 4 > size) return ext;

        // MPEG version (bits 19-20 of frame header)
        uint8_t mpeg_version = (data[frame_offset + 1] >> 3) & 0x03;
        // Layer description (bits 17-18)
        uint8_t layer = (data[frame_offset + 1] >> 1) & 0x03;
        // Bitrate index (bits 12-15)
        uint8_t bitrate_index = (data[frame_offset + 2] >> 4) & 0x0F;
        // Sample rate index (bits 10-11)
        uint8_t sr_index = (data[frame_offset + 2] >> 2) & 0x03;
        // Channel mode (bits 6-7 of third byte)
        uint8_t channel_mode = (data[frame_offset + 3] >> 6) & 0x03;

        // Map sample rate index to actual value (MPEG1)
        uint32_t sample_rate = 44100;  // Default fallback
        if (mpeg_version == 0x03) {  // MPEG Version 1
            switch (sr_index) {
                case 0: sample_rate = 44100; break;
                case 1: sample_rate = 48000; break;
                case 2: sample_rate = 32000; break;
            }
        }

        ext.sample_rate_norm = std::min(static_cast<float>(sample_rate) / 192000.0f, 1.0f);

        // Map channel mode
        switch (channel_mode) {
            case 0: ext.channels = 2.0f; break;  // Stereo
            case 1: ext.channels = 2.0f; break;  // Joint stereo
            case 2: ext.channels = 2.0f; break;  // Dual channel
            case 3: ext.channels = 1.0f; break;  // Mono
        }

        // Bitrate lookup (simplified MPEG1 Layer III table)
        uint32_t bitrate_kbps = 128;  // Default
        if (bitrate_index > 0 && bitrate_index < 15) {
            static const uint16_t BITRATE_TABLE[] = {
                0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
            };
            bitrate_kbps = BITRATE_TABLE[bitrate_index];
        }

        ext.bit_depth = 16.0f;  // MP3 typically decodes to 16-bit PCM
        ext.bitrate_norm = std::min(static_cast<float>(bitrate_kbps * 1000) / 320000.0f,
                                   1.0f);  // Normalize to 320 kbps

        // Approximate duration from file size and average bitrate
        if (bitrate_kbps > 0) {
            uint32_t duration_sec = static_cast<uint32_t>(
                (size * 8) / (bitrate_kbps * 1000));
            ext.duration_norm = std::min(static_cast<float>(duration_sec) / 3600.0f, 1.0f);
        }

        return ext;
    }

    auto extract_flac(const uint8_t* data, size_t size) const -> AudioExtension {
        AudioExtension ext{};

        // FLAC starts with "fLaC"
        if (size < 42) return ext;

        // STREAMINFO metadata block follows fLaC (4 bytes)
        // Last flag + block type at offset 4
        // Block length at offset 5-6 (big-endian)

        // Minimum block size at offset 10 (16 bits BE)
        uint16_t min_block = ImageExtractor::read_be16(data, 10);
        // Maximum block size at offset 12
        uint16_t max_block = ImageExtractor::read_be16(data, 12);
        // Minimum frame size at offset 14 (24 bits BE)
        // Maximum frame size at offset 17 (24 bits BE)
        // Sample rate at offset 20 (20 bits, upper 4 bits of byte 20 + 21-22)
        uint32_t sample_rate = ((uint32_t)(data[20] & 0x0F) << 16) |
                              ((uint32_t)data[21] << 8) | data[22];
        // Channels-1 at offset 23 (upper nibble: 0=mono, 1=stereo, etc.)
        ext.channels = static_cast<float>(((data[23] >> 4) & 0x0F) + 1);
        // Bits per sample-1 at offset 23 (lower nibble)
        ext.bit_depth = static_cast<float>(((data[23] & 0x0F) + 1));

        // Total samples at offset 24-27 (36 bits, upper 4 bits of byte 24 + 25-27)
        uint64_t total_samples = ((uint64_t)(data[24] & 0x0F) << 32) |
                                ((uint64_t)data[25] << 24) |
                                ((uint64_t)data[26] << 16) |
                                ((uint64_t)data[27] << 8) | data[28];

        ext.sample_rate_norm = std::min(static_cast<float>(sample_rate) / 192000.0f, 1.0f);

        uint32_t duration_sec = 0;
        if (sample_rate > 0 && total_samples > 0) {
            duration_sec = static_cast<uint32_t>(total_samples / sample_rate);
            ext.duration_norm = std::min(static_cast<float>(duration_sec) / 3600.0f, 1.0f);
        }

        // FLAC is always lossless
        ext.is_lossless = 1.0f;

        // Calculate approximate bitrate
        if (duration_sec > 0) {
            uint32_t bitrate = static_cast<uint32_t>((size * 8) / duration_sec);
            ext.bitrate_norm = std::min(static_cast<float>(bitrate) / 1411200.0f, 1.0f);
        }

        return ext;
    }

    auto extract_generic_audio(const uint8_t* data, size_t size,
                               FileType type) const -> AudioExtension {
        AudioExtension ext{};
        ext.sample_rate_norm = 0.69f;  // Assume 44.1kHz as common default
        ext.bit_depth = 16.0f;
        ext.channels = 2.0f;
        ext.duration_norm = std::min(static_cast<float>(size) / (44100.0f * 2.0f * 2.0f),
                                    1.0f);  // Rough estimate
        ext.is_lossless = (type == FileType::AUDIO_OGG) ? 0.5f : 0.0f;
        ext.bitrate_norm = 0.09f;  // Assume ~128 kbps
        return ext;
    }

    static auto find_riff_chunk(const uint8_t* data, size_t size,
                                const char* chunk_id) -> size_t {
        // Search for 4-byte chunk ID after RIFF header
        for (size_t i = 12; i < size - 4; ++i) {
            if (std::memcmp(data + i, chunk_id, 4) == 0) {
                return i;
            }
        }
        return 0;
    }
};

// ========================================================================
// VIDEO EXTENSION EXTRACTOR
// ========================================================================

class VideoExtractor {
   public:
    auto extract(const uint8_t* data, size_t size, FileType type) const
        -> VideoExtension {
        VideoExtension ext{};
        std::memset(&ext, 0, sizeof(ext));

        if (data == nullptr || size < 16) return ext;

        switch (type) {
            case FileType::VIDEO_MP4:
                ext = extract_mp4(data, size);
                break;
            case FileType::VIDEO_MKV:
                ext = extract_mkv(data, size);
                break;
            default:
                ext = extract_generic_video(data, size, type);
                break;
        }

        return ext;
    }

   private:
    auto extract_mp4(const uint8_t* data, size_t size) const -> VideoExtension {
        VideoExtension ext{};

        // MP4/ISO Base Media File Format structure is complex
        // Simplified: Look for track information in moov atom

        // For now, provide reasonable defaults based on typical MP4 characteristics
        ext.codec_type = 1.0f;  // Assume H.264 most common
        ext.is_lossless = 0.0f;  // Almost all video codecs are lossy
        ext.fps_norm = 0.42f;    // Assume 30fps normalized to ~71fps (common max)
        ext.compression_savings = 0.1f;  // Video already heavily compressed

        // TODO: Implement proper MP4 box parsing for accurate extraction
        // Would need to parse: ftyp, moov/trak/mdia/minf/stbl/stsd atoms

        return ext;
    }

    auto extract_mkv(const uint8_t* data, size_t size) const -> VideoExtension {
        VideoExtension ext{};

        // MKV/WebM uses EBML structure, complex to parse fully
        ext.codec_type = 2.0f;  // Could be VP8/VP9/AV1
        ext.is_lossless = 0.0f;
        ext.fps_norm = 0.42f;
        ext.compression_savings = 0.08f;

        return ext;
    }

    auto extract_generic_video(const uint8_t* data, size_t size,
                               FileType type) const -> VideoExtension {
        VideoExtension ext{};
        ext.width_norm = 0.5f;  // Unknown
        ext.height_norm = 0.5f;
        ext.fps_norm = 0.42f;
        ext.duration_norm = std::min(static_cast<float>(size) / (1000000.0f), 1.0f);
        ext.codec_type = 0.0f;  // Unknown
        ext.is_lossless = 0.0f;
        ext.compression_savings = 0.1f;
        return ext;
    }
};

// ========================================================================
// ARCHIVE EXTENSION EXTRACTOR
// ========================================================================

class ArchiveExtractor {
   public:
    auto extract(const uint8_t* data, size_t size, FileType type) const
        -> ArchiveExtension {
        ArchiveExtension ext{};
        std::memset(&ext, 0, sizeof(ext));

        if (data == nullptr || size < 4) return ext;

        switch (type) {
            case FileType::ARCHIVE_ZIP:
                ext = extract_zip(data, size);
                break;
            case FileType::ARCHIVE_GZIP:
                ext = extract_gzip(data, size);
                break;
            default:
                ext = extract_generic_archive(data, size, type);
                break;
        }

        return ext;
    }

   private:
    auto extract_zip(const uint8_t* data, size_t size) const -> ArchiveExtension {
        ArchiveExtension ext{};

        // ZIP Local File Header at offset 0
        // Signature: PK\x03\x04 (already verified by Magic Bytes detector)

        // Compression method at offset 8 (0=stored, 8=deflated)
        uint16_t compression_method = ImageExtractor::read_le16(data, 8);

        // Compressed size at offset 18
        uint32_t compressed_size = ImageExtractor::read_le32(data, 18);
        // Uncompressed size at offset 22
        uint32_t uncompressed_size = ImageExtractor::read_le32(data, 22);

        if (compressed_size > 0) {
            ext.current_ratio = static_cast<float>(uncompressed_size) / compressed_size;
        }

        // Determine inner format from filename (offset 30, variable length)
        // For simplicity, assume mixed content
        ext.inner_format = 0.5f;  // Mixed/unknown

        // Estimate file count (very rough heuristic based on size)
        ext.file_count_norm = std::min(
            static_cast<float>(size) / 10000.0f, 1.0f);

        // Recompression potential: ZIP with deflate is already good
        // Only benefit if switching to LZMA/ZSTD or using stronger settings
        if (compression_method == 0) {  // Stored (no compression)
            ext.recompress_potential = 0.9f;  // Huge potential
        } else if (compression_method == 8) {  // Deflate
            ext.recompress_potential = 0.2f;  // Limited improvement
        } else {
            ext.recompress_potential = 0.3f;
        }

        return ext;
    }

    auto extract_gzip(const uint8_t* data, size_t size) const -> ArchiveExtension {
        ArchiveExtension ext{};

        // GZIP has simple single-stream structure
        // Original size stored in last 4 bytes (ISIZE field, modulo 2^32)
        if (size >= 4) {
            uint32_t original_size = ImageExtractor::read_le32(data, size - 4);

            if (original_size > 0 && size > 18) {  // 18 bytes GZIP header overhead
                uint32_t compressed_data_size = size - 18;  // Approximate
                ext.current_ratio = static_cast<float>(original_size) / compressed_data_size;
            }
        }

        ext.inner_format = 0.0f;  // Single stream, no inner format
        ext.file_count_norm = 0.0f;  // Single file

        // GZIP uses deflate, similar recompression potential to ZIP
        ext.recompress_potential = 0.15f;

        return ext;
    }

    auto extract_generic_archive(const uint8_t* data, size_t size,
                                 FileType type) const -> ArchiveExtension {
        ArchiveExtension ext{};
        ext.inner_format = 0.5f;
        ext.current_ratio = 1.5f;  // Guess moderate compression
        ext.file_count_norm = 0.3f;
        ext.recompress_potential = 0.2f;
        return ext;
    }
};

// ========================================================================
// BINARY/EXECUTABLE EXTENSION EXTRACTOR
// ========================================================================

class BinaryExtractor {
   public:
    auto extract(const uint8_t* data, size_t size, FileType type) const
        -> BinaryExtension {
        BinaryExtension ext{};
        std::memset(&ext, 0, sizeof(ext));

        if (data == nullptr || size < 4) return ext;

        switch (type) {
            case FileType::EXEC_PE:
                ext = extract_pe(data, size);
                break;
            case FileType::EXEC_ELF:
                ext = extract_elf(data, size);
                break;
            case FileType::DOC_PDF:
                ext = extract_pdf(data, size);
                break;
            case FileType::DB_SQLITE:
                ext = extract_sqlite(data, size);
                break;
            default:
                ext = extract_generic_binary(data, size, type);
                break;
        }

        return ext;
    }

   private:
    auto extract_pe(const uint8_t* data, size_t size) const -> BinaryExtension {
        BinaryExtension ext{};

        // PE structure: MZ header (64 bytes min) + PE signature + COFF header + Optional header
        if (size < 512) return ext;

        // PE signature offset at MZ+0x3C
        uint32_t pe_offset = ImageExtractor::read_le32(data, 0x3C);
        if (pe_offset + 4 > size || pe_offset < 0x40) return ext;

        // Machine type at PE+4 (COFF header start)
        uint16_t machine = ImageExtractor::read_le16(data, pe_offset + 4);

        // Number of sections at PE+6
        uint16_t num_sections = ImageExtractor::read_le16(data, pe_offset + 6);

        // Optional header size at PE+20
        uint16_t opt_header_size = ImageExtractor::read_le16(data, pe_offset + 20);

        // Check for PE32+ (PE32+) magic at optional header start
        uint16_t pe_magic = ImageExtractor::read_le16(data, pe_offset + 24);

        // Executables have high structure density (sections, imports, exports)
        ext.structure_density = std::min(
            static_cast<float>(num_sections) / 20.0f, 1.0f);

        // Alignment granularity from Optional Header
        if (pe_magic == 0x10B) {  // PE32
            // Section alignment at PE+32 (OptionalHeader start + 32)
            if (pe_offset + 56 <= size) {
                uint32_t section_alignment = ImageExtractor::read_le32(data, pe_offset + 56);
                ext.alignment = map_alignment(section_alignment);
            }
        }

        // PE is little-endian
        ext.endianness = 0.0f;

        // Executable score: high for valid PE files with sections
        ext.exec_score = (num_sections > 0 && num_sections < 50) ? 0.9f : 0.5f;

        // Padding ratio: PE files typically have some alignment padding
        ext.padding_ratio = estimate_padding_ratio(data, size);

        return ext;
    }

    auto extract_elf(const uint8_t* data, size_t size) const -> BinaryExtension {
        BinaryExtension ext{};

        if (size < 52) return ext;  // ELF header minimum size

        // ELF identification at offset 0
        // EI_DATA (byte 5): 1=LE, 2=BE
        uint8_t ei_data = data[5];
        ext.endianness = (ei_data == 1) ? 0.0f : 1.0f;

        // e_type (object file type) at offset 16
        uint16_t elf_type = ImageExtractor::read_le16(data, 16);

        // e_machine at offset 18
        uint16_t machine = ImageExtractor::read_le16(data, 18);

        // e_phoff (program header table offset) at offset 28
        uint32_t phoff = ImageExtractor::read_le32(data, 28);
        // e_phnum (number of program headers) at offset 44
        uint16_t phnum = ImageExtractor::read_le16(data, 44);

        // e_shoff (section header table offset) at offset 40
        uint32_t shoff = ImageExtractor::read_le32(data, 40);
        // e_shnum (number of section headers) at offset 48
        uint16_t shnum = ImageExtractor::read_le16(data, 48);

        // Structure density based on section count
        ext.structure_density = std::min(
            static_cast<float>(shnum) / 30.0f, 1.0f);

        // Alignment from program headers
        ext.alignment = 0.5f;  // Typical page alignment

        // Executable score
        bool is_executable = (elf_type == 2);  // ET_EXEC
        ext.exec_score = (is_executable && (phnum > 0 || shnum > 0)) ? 0.9f : 0.6f;

        ext.padding_ratio = estimate_padding_ratio(data, size);

        return ext;
    }

    auto extract_pdf(const uint8_t* data, size_t size) const -> BinaryExtension {
        BinaryExtension ext{};

        // PDF is a structured text-based binary format
        ext.structure_density = 0.7f;  // PDF has clear object structure
        ext.padding_ratio = 0.05f;     // Low padding
        ext.alignment = 0.0f;          // No alignment concept
        ext.endianness = 0.0f;         // PDF numbers are big-endian
        ext.exec_score = 0.0f;         // Not executable

        return ext;
    }

    auto extract_sqlite(const uint8_t* data, size_t size) const -> BinaryExtension {
        BinaryExtension ext{};

        // SQLite databases are highly structured B-tree formats
        ext.structure_density = 0.9f;
        ext.padding_ratio = 0.02f;     // Minimal padding
        ext.alignment = 0.5f;          // Page-aligned (typically 4096)
        ext.endianness = 0.0f;         // SQLite is big-endian for integers
        ext.exec_score = 0.0f;         // Not executable

        return ext;
    }

    auto extract_generic_binary(const uint8_t* data, size_t size,
                                FileType type) const -> BinaryExtension {
        BinaryExtension ext{};

        // Generic analysis for unknown binary types
        ext.structure_density = 0.3f;
        ext.padding_ratio = estimate_padding_ratio(data, size);
        ext.alignment = 0.5f;
        ext.endianness = 0.0f;  // Assume LE on Windows
        ext.exec_score = 0.0f;

        return ext;
    }

    // ================================================================
    // HELPER FUNCTIONS
    // ================================================================

    static auto map_alignment(uint32_t alignment_value) -> float {
        // Map common alignment values to normalized range
        switch (alignment_value) {
            case 0x200:  return 0.1f;   // 512 bytes
            case 0x1000: return 0.5f;   // 4096 bytes (common)
            case 0x2000: return 0.7f;   // 8192 bytes
            default:
                if (alignment_value <= 0x200) return 0.05f;
                if (alignment_value <= 0x1000) return 0.3f;
                return 0.9f;
        }
    }

    static auto estimate_padding_ratio(const uint8_t* data, size_t size) -> float {
        // Sample last 1024 bytes for zero-padding detection
        size_t sample_start = (size > 1024) ? size - 1024 : 0;
        size_t sample_size = size - sample_start;

        size_t zero_count = 0;
        for (size_t i = sample_start; i < size; ++i) {
            if (data[i] == 0x00) {
                ++zero_count;
            }
        }

        return (sample_size > 0)
                   ? static_cast<float>(zero_count) / sample_size
                   : 0.0f;
    }
};

}  // namespace ade
}  // namespace compressor