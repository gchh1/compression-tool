/**
 * @file ExtensionExtractors.hpp
 * @author ADE Module - Extension Segment Feature Extractors
 * @brief Type-specific extension feature extraction (5-8 dims per file type)
 * @version 3.0
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details Provides header-only inline extractor functions for each file type
 * extension segment. Each extractor parses the relevant file header metadata
 * and produces the corresponding ExtensionData union field.
 *
 * Extension dimension table:
 *   TextCode (text/code files)     — 5 dims
 *   Image    (bitmap/photo files)  — 8 dims
 *   Audio    (sound/music files)   — 6 dims
 *   Video    (movie/video files)   — 7 dims
 *   Archive  (compressed bundles)  — 4 dims
 *   Binary   (PE/ELF/data/etc.)    — 5 dims
 */

#pragma once

#include "FeatureVectorV3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

// ========================================================================
// Helper: byte histogram entropy (8-bit)
// ========================================================================
inline double computeHistogramEntropy(const uint32_t* histogram, size_t total) noexcept {
    if (total == 0) return 0.0;
    double entropy = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (histogram[i] > 0) {
            double p = static_cast<double>(histogram[i]) / static_cast<double>(total);
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

// ========================================================================
// Helper: read big-endian uint16/uint32
// ========================================================================
inline uint16_t readBE16(const uint8_t* ptr) noexcept {
    return static_cast<uint16_t>((ptr[0] << 8) | ptr[1]);
}

inline uint32_t readBE32(const uint8_t* ptr) noexcept {
    return (static_cast<uint32_t>(ptr[0]) << 24) |
           (static_cast<uint32_t>(ptr[1]) << 16) |
           (static_cast<uint32_t>(ptr[2]) << 8)  |
           static_cast<uint32_t>(ptr[3]);
}

inline uint16_t readLE16(const uint8_t* ptr) noexcept {
    return static_cast<uint16_t>(ptr[0] | (ptr[1] << 8));
}

inline uint32_t readLE32(const uint8_t* ptr) noexcept {
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[3]) << 24);
}

// ========================================================================
// Helper: estimate compression savings from entropy
// ========================================================================
inline float estimateCompressionSavings(double entropy, size_t size) noexcept {
    (void)size;
    if (entropy < 6.0) {
        return std::min(0.9f, static_cast<float>((8.0 - entropy) / 8.0 + 0.2));
    }
    if (entropy < 7.0) {
        return std::max(0.0f, static_cast<float>((8.0 - entropy) / 8.0));
    }
    return 0.0f;
}

// ========================================================================
// 4.1 TextCode Extension (5 dims)
// ========================================================================
inline void extractTextCodeExtension(
    const uint8_t* data,
    size_t size,
    ExtensionData& ext
) noexcept {
    auto& t = ext.text;
    t.language_score = 0.5f;
    t.syntax_density = 0.0f;
    t.line_ending = 0.0f;
    t.indent_style = 0.0f;
    t.comment_ratio = 0.0f;

    if (size == 0) return;

    const size_t scan = std::min(size, size_t(65536));
    size_t syntax_count = 0;
    const std::array<uint8_t, 9> syntax_chars = { '{', '}', '(', ')', ';', '[', ']', '<', '>' };

    size_t lf_count = 0;
    size_t crlf_count = 0;
    size_t space_indent = 0;
    size_t tab_indent = 0;
    size_t comment_lines = 0;
    size_t total_lines = 0;

    for (size_t i = 0; i < scan; ++i) {
        uint8_t b = data[i];
        if (b == '{' || b == '}' || b == '(' || b == ')' ||
            b == ';' || b == '[' || b == ']' || b == '<' || b == '>') {
            ++syntax_count;
        }
        if (b == '\n') {
            ++lf_count;
            ++total_lines;
        }
    }

    for (size_t i = 0; i + 1 < scan; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            ++crlf_count;
        }
    }

    // Detect line ending type
    if (crlf_count > 0 && lf_count == crlf_count) {
        t.line_ending = 1.0f;
    } else if (lf_count > 0 && crlf_count == 0) {
        t.line_ending = 0.0f;
    } else if (lf_count > 0) {
        t.line_ending = 0.5f;
    }

    // Indentation style from first 200 lines
    size_t lines_scanned = 0;
    size_t pos = 0;
    while (pos < scan && lines_scanned < 200) {
        size_t line_start = pos;
        while (pos < scan && data[pos] != '\n') ++pos;
        size_t line_len = pos - line_start;
        if (line_len > 0 && data[line_start] == '\t') {
            ++tab_indent;
        } else if (line_len > 0 && data[line_start] == ' ') {
            ++space_indent;
        }
        if (pos < scan && data[pos] == '\n') ++pos;
        ++lines_scanned;
    }
    size_t total_indent = space_indent + tab_indent;
    if (total_indent > 0) {
        t.indent_style = static_cast<float>(tab_indent) / static_cast<float>(total_indent);
    }

    // Comment ratio estimation
    const uint8_t comment_patterns[4][4] = {
        {'/', '/'}, {'#', 0}, {'/', '*'}, {'<', '!', '-', '-'}
    };

    pos = 0;
    size_t comment_candidates = 0;
    while (pos < scan) {
        uint8_t b = data[pos];
        // Skip whitespace at line start
        if (b == ' ' || b == '\t') { ++pos; continue; }
        if (b == '\n' || b == '\r') { ++pos; continue; }

        bool is_comment = false;
        if (pos + 1 < scan && data[pos] == '/' && data[pos + 1] == '/') is_comment = true;
        else if (b == '#') is_comment = true;
        else if (pos + 1 < scan && data[pos] == '/' && data[pos + 1] == '*') is_comment = true;
        else if (pos + 3 < scan && data[pos] == '<' && data[pos+1] == '!' && data[pos+2] == '-' && data[pos+3] == '-') is_comment = true;

        if (is_comment) ++comment_candidates;

        while (pos < scan && data[pos] != '\n') ++pos;
        if (pos < scan && data[pos] == '\n') ++pos;
    }
    size_t nonempty_lines = total_lines;
    t.comment_ratio = (nonempty_lines > 0) ?
        std::min(0.5f, static_cast<float>(comment_candidates) / static_cast<float>(nonempty_lines)) : 0.0f;

    t.syntax_density = std::min(1.0f, static_cast<float>(syntax_count) / static_cast<float>(scan) * 5.0f);

    // Language score: 0=code, 1=natural language
    float avg_word_chars = 0.0f;
    // Count spaces/tabs as token separators
    size_t spaces = 0;
    size_t tokens = 0;
    size_t char_sum = 0;
    bool in_word = false;
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == ' ' || data[i] == '\t' || data[i] == '\n') {
            if (in_word) { ++tokens; in_word = false; }
        } else {
            if (!in_word) in_word = true;
            ++char_sum;
        }
    }
    if (in_word) ++tokens;
    if (tokens > 0) {
        avg_word_chars = static_cast<float>(char_sum) / static_cast<float>(tokens) / 20.0f;
    }

    float word_factor = std::min(1.0f, avg_word_chars);
    float syntax_factor = std::min(1.0f, t.syntax_density * 2.0f);
    t.language_score = std::max(0.0f, std::min(1.0f, 1.0f - (syntax_factor + word_factor * 0.5f) / 2.5f));
}

// ========================================================================
// 4.2 Image Extension (8 dims)
// ========================================================================
inline void extractImageExtension(
    const uint8_t* data,
    size_t size,
    ExtensionData& ext,
    const std::string& /*path_hint*/ = ""
) noexcept {
    auto& img = ext.image;
    img.width_norm = 0.0f;
    img.height_norm = 0.0f;
    img.bit_depth = 8.0f;
    img.has_alpha = 0.0f;
    img.color_mode = 1.0f;
    img.is_lossless_orig = 1.0f;
    img.jpeg_quality = 0.0f;
    img.compression_savings = 0.0f;

    if (size < 16) return;

    float width = 0.0f, height = 0.0f;
    float bit_depth = 8.0f;
    float has_alpha = 0.0f;
    float color_mode = 1.0f;
    float is_lossless = 1.0f;
    float jpeg_quality = 0.0f;

    // JPEG
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        is_lossless = 0.0f;
        // Search for SOF0/SOF1/SOF2 markers
        for (size_t i = 0; i + 9 < size; ++i) {
            if (data[i] == 0xFF &&
                (data[i+1] == 0xC0 || data[i+1] == 0xC1 || data[i+1] == 0xC2)) {
                height = static_cast<float>((data[i+5] << 8) | data[i+6]);
                width = static_cast<float>((data[i+7] << 8) | data[i+8]);
                break;
            }
        }
        if (width > 0.0f && height > 0.0f) {
            float bpp = (static_cast<float>(size) * 8.0f) / (width * height);
            if (bpp < 1.0f) jpeg_quality = 10.0f;
            else if (bpp < 2.0f) jpeg_quality = 40.0f;
            else if (bpp < 4.0f) jpeg_quality = 70.0f;
            else if (bpp < 8.0f) jpeg_quality = 85.0f;
            else jpeg_quality = 95.0f;
        } else {
            jpeg_quality = 75.0f;
        }
        bit_depth = 24.0f;
        color_mode = 1.0f;
    }
    // PNG
    else if (size >= 37 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        for (size_t i = 8; i + 16 <= size; ++i) {
            if (data[i] == 'I' && data[i+1] == 'H' && data[i+2] == 'D' && data[i+3] == 'R') {
                width = static_cast<float>(readBE32(data + i + 4));
                height = static_cast<float>(readBE32(data + i + 8));
                bit_depth = static_cast<float>(data[i + 12]);
                uint8_t color_type = data[i + 13];
                has_alpha = (color_type == 4 || color_type == 6) ? 1.0f : 0.0f;
                if (color_type == 0) color_mode = 0.0f;
                else if (color_type == 2) color_mode = 1.0f;
                else if (color_type == 4 || color_type == 6) color_mode = 2.0f;
                else if (color_type == 3) color_mode = 4.0f;
                break;
            }
        }
        is_lossless = 1.0f;
    }
    // BMP
    else if (size >= 30 && data[0] == 'B' && data[1] == 'M') {
        width = static_cast<float>(readLE32(data + 18));
        height = static_cast<float>(readLE32(data + 22));
        bit_depth = static_cast<float>(readLE16(data + 28));
        is_lossless = 1.0f;
        color_mode = 1.0f;
    }
    // GIF
    else if (size >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8') {
        is_lossless = 0.0f;
        color_mode = 4.0f;
    }
    // WebP lossy
    else if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
        uint32_t riff_size = readLE32(data + 4);
        if (riff_size + 8 <= size && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
            is_lossless = 0.0f;
        }
    }

    img.width_norm = std::min(width / 4096.0f, 1.0f);
    img.height_norm = std::min(height / 4096.0f, 1.0f);
    img.bit_depth = bit_depth;
    img.has_alpha = has_alpha;
    img.color_mode = std::min(color_mode / 4.0f, 1.0f);
    img.is_lossless_orig = is_lossless;
    img.jpeg_quality = jpeg_quality / 100.0f;

    float savings = 0.0f;
    if (is_lossless > 0.5f && width > 0.0f && height > 0.0f) {
        float raw_bpp = (bit_depth > 0.0f) ? bit_depth : 24.0f;
        double unpacked = static_cast<double>(width) * static_cast<double>(height) * static_cast<double>(raw_bpp) / 8.0;
        if (unpacked > 0.0) {
            savings = static_cast<float>((unpacked - static_cast<double>(size)) / unpacked);
        }
    }
    img.compression_savings = std::max(0.0f, std::min(1.0f, savings));
}

// ========================================================================
// 4.3 Audio Extension (6 dims)
// ========================================================================
inline void extractAudioExtension(
    const uint8_t* data,
    size_t size,
    ExtensionData& ext
) noexcept {
    auto& a = ext.audio;
    a.sample_rate_norm = 44100.0f / 96000.0f;
    a.bit_depth = 16.0f;
    a.channels = 2.0f / 8.0f;
    a.duration_norm = 0.0f;
    a.is_lossless = 0.0f;
    a.bitrate_norm = 192.0f / 1000.0f;

    if (size < 16) return;

    float sample_rate = 44100.0f;
    float bit_depth = 16.0f;
    float channels = 2.0f;
    float duration_sec = 0.0f;
    float is_lossless = 0.0f;

    // WAV: RIFF + WAVE
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
        for (size_t i = 12; i + 24 <= size; ++i) {
            if (data[i] == 'f' && data[i+1] == 'm' && data[i+2] == 't' && data[i+3] == ' ') {
                sample_rate = static_cast<float>(readLE32(data + i + 12));
                channels = static_cast<float>(readLE16(data + i + 10));
                bit_depth = static_cast<float>(readLE16(data + i + 22));
                if (sample_rate > 0.0f && channels > 0.0f && bit_depth > 0.0f) {
                    uint32_t bytes_per_sec = readLE32(data + i + 16);
                    if (bytes_per_sec > 0) {
                        duration_sec = static_cast<float>(size - 44) / static_cast<float>(bytes_per_sec);
                    }
                }
                break;
            }
        }
        is_lossless = 1.0f;
    }
    // FLAC
    else if (size >= 42 && data[0] == 'f' && data[1] == 'L' && data[2] == 'a' && data[3] == 'C') {
        // STREAMINFO at offset 8 (after fLaC + metadata block header)
        const uint8_t* si = data + 8;
        if (size >= 42) {
            sample_rate = static_cast<float>((si[14] << 12) | (si[15] << 4) | (si[16] >> 4));
            channels = static_cast<float>(((si[13] >> 1) & 0x07) + 1);
            bit_depth = static_cast<float>(((si[13] & 0x01) << 4) | (si[14] >> 4)) + 1.0f;
            uint32_t total_samples = readBE32(si + 17);
            if (sample_rate > 0.0f) {
                duration_sec = static_cast<float>(total_samples) / sample_rate;
            }
        }
        is_lossless = 1.0f;
    }
    // MP3 (ID3v2 or sync pattern)
    else if (size >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        is_lossless = 0.0f;
    }

    a.sample_rate_norm = std::min(sample_rate / 96000.0f, 1.0f);
    a.bit_depth = bit_depth;
    a.channels = std::min(channels / 8.0f, 1.0f);
    a.duration_norm = std::min(duration_sec / 3600.0f, 1.0f);
    a.is_lossless = is_lossless;
    if (duration_sec > 0.0f) {
        float bitrate = (static_cast<float>(size) * 8.0f) / duration_sec / 1000.0f;
        a.bitrate_norm = std::min(bitrate / 1000.0f, 1.0f);
    }
}

// ========================================================================
// 4.4 Video Extension (7 dims)
// ========================================================================
inline void extractVideoExtension(
    const uint8_t* data,
    size_t size,
    ExtensionData& ext,
    const std::string& /*path_hint*/ = ""
) noexcept {
    auto& v = ext.video;
    v.width_norm = 0.0f;
    v.height_norm = 0.0f;
    v.fps_norm = 0.0f;
    v.duration_norm = 0.0f;
    v.codec_type = 0.0f;
    v.is_lossless = 0.0f;
    v.compression_savings = 0.0f;

    if (size < 32) return;

    float width = 0.0f, height = 0.0f;

    // MP4 / QuickTime
    if (data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
        // Search for moov → tkhd
        for (size_t i = 0; i + 8 < size; ++i) {
            if (data[i] == 'm' && data[i+1] == 'o' && data[i+2] == 'o' && data[i+3] == 'v') {
                for (size_t j = i; j + 84 < size; ++j) {
                    if (data[j] == 't' && data[j+1] == 'k' && data[j+2] == 'h' && data[j+3] == 'd') {
                        width = static_cast<float>(readBE32(data + j + 76));
                        height = static_cast<float>(readBE32(data + j + 80));
                        if (width > 65535.0f) width /= 65536.0f;
                        if (height > 65535.0f) height /= 65536.0f;
                        break;
                    }
                }
                break;
            }
        }
    }

    v.width_norm = std::min(width / 3840.0f, 1.0f);
    v.height_norm = std::min(height / 2160.0f, 1.0f);
    v.codec_type = 0.0f;

    uint32_t histogram[256] = {0};
    size_t hsize = std::min(size, size_t(4096));
    for (size_t i = 0; i < hsize; ++i) histogram[data[i]]++;
    double entropy = computeHistogramEntropy(histogram, hsize);
    v.compression_savings = estimateCompressionSavings(entropy, size);
}

// ========================================================================
// 4.5 Archive Extension (4 dims)
// ========================================================================
inline void extractArchiveExtension(
    const uint8_t* data,
    size_t size,
    ExtensionData& ext
) noexcept {
    auto& ar = ext.archive;
    ar.inner_format = 0.0f;
    ar.current_ratio = 0.0f;
    ar.file_count_norm = 0.0f;
    ar.recompress_potential = 0.0f;

    if (size < 4) return;

    float inner = 0.0f;
    if (data[0] == 'P' && data[1] == 'K') inner = 0.0f;           // ZIP
    else if (data[0] == 'R' && data[1] == 'a' && data[2] == 'r') inner = 1.0f;  // RAR
    else if (size >= 3 && data[0] == '7' && data[1] == 'z' && data[2] == 0xBC) inner = 2.0f;  // 7z
    else if (data[0] == 0x1F && data[1] == 0x9D) inner = 3.0f;   // compress
    else if (size >= 3 && ((data[0] == 0x1F && data[1] == 0x8B) || (data[0] == 'B' && data[1] == 'Z'))) inner = 4.0f;  // gzip/bzip2
    else if (size >= 6 && data[0] == 0xFD && data[1] == '7' && data[2] == 'z' && data[3] == 'X' && data[4] == 'Z') inner = 5.0f;  // XZ
    else inner = 6.0f;

    uint32_t histogram[256] = {0};
    size_t hsize = std::min(size, size_t(4096));
    for (size_t i = 0; i < hsize; ++i) histogram[data[i]]++;
    double entropy = computeHistogramEntropy(histogram, hsize);
    double compactness = entropy / 8.0;

    ar.inner_format = inner / 10.0f;
    ar.current_ratio = std::max(0.0f, std::min(1.0f, 1.0f - static_cast<float>(compactness) * 0.5f));
    ar.file_count_norm = 0.0f;
    double potential = (compactness > 0.3) ? std::max(0.0, (compactness - 0.4) / 0.6) : 0.0;
    ar.recompress_potential = std::min(1.0f, static_cast<float>(potential));
}

// ========================================================================
// 4.6 Binary Extension (5 dims)
// ========================================================================
inline void extractBinaryExtension(
    const uint8_t* data,
    size_t size,
    ExtensionData& ext
) noexcept {
    auto& b = ext.binary;
    b.structure_density = 0.0f;
    b.padding_ratio = 0.0f;
    b.alignment = 0.0f;
    b.endianness = 0.5f;
    b.exec_score = 0.0f;

    if (size < 64) return;

    const size_t scan = std::min(size, size_t(4096));

    // Structure density & padding
    size_t section_starts = 0;
    for (size_t i = 1; i < scan; ++i) {
        if (data[i - 1] == 0 && data[i] != 0) ++section_starts;
    }
    b.structure_density = std::min(1.0f, static_cast<float>(section_starts) / 50.0f);

    size_t zero_bytes = 0;
    size_t limit = std::min(size, size_t(65536));
    for (size_t i = 0; i < limit; ++i) {
        if (data[i] == 0) ++zero_bytes;
    }
    b.padding_ratio = std::min(1.0f, static_cast<float>(zero_bytes) / static_cast<float>(limit) * 5.0f);

    // Alignment at 512/1024/2048/4096
    size_t align_hits = 0;
    if (512 < size && data[512] == 0) ++align_hits;
    if (1024 < size && data[1024] == 0) ++align_hits;
    if (2048 < size && data[2048] == 0) ++align_hits;
    if (4096 < size && data[4096] == 0) ++align_hits;
    b.alignment = static_cast<float>(align_hits) / 4.0f;

    // Endianness heuristic
    size_t le_score = 0, be_score = 0;
    size_t e_scan = std::min(size, size_t(64)) & ~3;
    for (size_t i = 0; i + 4 <= e_scan; i += 4) {
        uint32_t val_le = readLE32(data + i);
        uint32_t val_be = readBE32(data + i);
        if (val_le < 256 && val_le > 0) ++le_score;
        if (val_be < 256 && val_be > 0) ++be_score;
    }
    if (le_score + be_score > 0) {
        b.endianness = (le_score >= be_score) ? 0.0f : 1.0f;
    }

    // Executable score
    if (size >= 2 && data[0] == 'M' && data[1] == 'Z') {
        b.exec_score = 0.9f;
    } else if (size >= 4 && data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        b.exec_score = 0.95f;
    } else if (size >= 4 &&
               ((data[0] == 0xCF && data[1] == 0xFA && data[2] == 0xED && data[3] == 0xFE) ||
                (data[0] == 0xCE && data[1] == 0xFA && data[2] == 0xED && data[3] == 0xFE))) {
        b.exec_score = 0.85f;
    } else {
        // Heuristic: common x86 instruction opcodes
        size_t instr_count = 0;
        const std::array<uint8_t, 10> opcodes = {0x55, 0x89, 0x83, 0x8B, 0x48, 0xE9, 0xEB, 0x74, 0x75, 0x90};
        for (size_t i = 0; i < scan; ++i) {
            for (auto op : opcodes) {
                if (data[i] == op) { ++instr_count; break; }
            }
        }
        b.exec_score = std::min(0.7f, static_cast<float>(instr_count) / 50.0f);
    }
}

// ========================================================================
// Main extension dispatcher
// ========================================================================
/**
 * @brief Fill the ExtensionData portion of a FeatureVectorV3 based on file type.
 *
 * @param data      raw file bytes
 * @param size      number of bytes
 * @param file_type ExtensionType enum value
 * @param ext       [out] ExtensionData union to populate
 * @param path_hint optional file path for extension-based fallback
 */
inline void extractExtensionFeatures(
    const uint8_t* data,
    size_t size,
    ExtensionType file_type,
    ExtensionData& ext,
    const std::string& path_hint = ""
) noexcept {
    switch (file_type) {
        case ExtensionType::TEXT:
            extractTextCodeExtension(data, size, ext);
            break;
        case ExtensionType::IMAGE:
            extractImageExtension(data, size, ext, path_hint);
            break;
        case ExtensionType::AUDIO:
            extractAudioExtension(data, size, ext);
            break;
        case ExtensionType::VIDEO:
            extractVideoExtension(data, size, ext, path_hint);
            break;
        case ExtensionType::ARCHIVE:
            extractArchiveExtension(data, size, ext);
            break;
        case ExtensionType::BINARY:
            extractBinaryExtension(data, size, ext);
            break;
        default:
            break; // NONE
    }
}

} // namespace ade
} // namespace compressor