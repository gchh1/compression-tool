/**
 * @file FeatureVectorV3.hpp
 * @author ADE Module - Feature Vector Data Structures (v3.0 Final)
 * @brief Segmented feature vector: Base(20 dims) + Dynamic Extension(5-8 dims)
 * @version 3.0 Final
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details This header defines the complete data structures for the v3.0
 * segmented feature vector specification. The design follows a Base + Extension
 * architecture where all files share a common 20-dimensional base segment,
 * and file-type-specific extension segments provide specialized features.
 *
 * Memory Layout:
 * - Base Segment: 80 bytes (20 × 4 bytes float)
 * - Extension Segment: 20-32 bytes (5-8 × 4 bytes float)
 * - Total Output: 100-112 bytes per file (without padding)
 * - Padded for ML: 132 bytes (33 dimensions)
 */

#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

// ===== Forward Declarations =====
enum class FileType : uint8_t;
enum class ExtensionType : uint8_t;

// ========================================================================
// FILE TYPE ENUMERATION (30+ supported formats)
// ========================================================================

/**
 * @brief File type enumeration for Magic Bytes detection and Extension routing
 *
 * These types determine which Extension segment to use and influence ADE's
 * compression algorithm selection.
 */
enum class FileType : uint8_t {
    UNKNOWN = 0,

    // Text/Code files → TextExtension
    TEXT_PLAIN, TEXT_UTF8,
    SOURCE_C, SOURCE_CPP, SOURCE_PYTHON, SOURCE_JAVA, SOURCE_RUST, SOURCE_GO,
    MARKDOWN, HTML, XML, JSON, CSS, JAVASCRIPT,

    // Image files → ImageExtension
    IMAGE_PNG, IMAGE_JPEG, IMAGE_GIF, IMAGE_BMP,
    IMAGE_TIFF_LE, IMAGE_TIFF_BE, IMAGE_WEBP, IMAGE_AVIF,

    // Audio files → AudioExtension
    AUDIO_WAV, AUDIO_MP3, AUDIO_FLAC, AUDIO_OGG, AUDIO_AAC,

    // Video files → VideoExtension
    VIDEO_MP4, VIDEO_MKV, VIDEO_AVI, VIDEO_FLV, VIDEO_WEBM,

    // Archive/Compressed files → ArchiveExtension
    ARCHIVE_ZIP, ARCHIVE_GZIP, ARCHIVE_BZIP2, ARCHIVE_7Z,
    ARCHIVE_RAR, ARCHIVE_TAR, ARCHIVE_XZ,

    // Executable files → BinaryExtension
    EXEC_PE, EXEC_ELF, EXEC_MACHO, EXEC_MACHO_FAT,

    // Document files → BinaryExtension or specialized
    DOC_PDF, DOC_DOCX, DOC_XLSX, DOC_OLE2,

    // Database → BinaryExtension
    DB_SQLITE,

    // Generic binary/encrypted → BinaryExtension
    BINARY_GENERIC, ENCRYPTED,

    COUNT  // Total count of file types
};

auto file_type_to_string(FileType type) -> std::string;

auto operator<<(std::ostream& os, FileType type) -> std::ostream&;

// ========================================================================
// EXTENSION TYPE ENUMERATION (6 categories)
// ========================================================================

/**
 * @brief Extension segment type selector
 *
 * Each file uses exactly one extension type based on its detected FileType.
 */
enum class ExtensionType : uint8_t {
    NONE = 0,       // No extension (unknown file type)
    TEXT,           // TextCodeSegment (5 dims)
    IMAGE,          // ImageSegment (8 dims)
    AUDIO,          // AudioSegment (6 dims)
    VIDEO,          // VideoSegment (7 dims)
    ARCHIVE,        // ArchiveSegment (4 dims)
    BINARY          // BinarySegment (5 dims)
};

auto extension_type_to_string(ExtensionType type) -> std::string;

auto operator<<(std::ostream& os, ExtensionType type) -> std::ostream&;

auto map_file_type_to_extension(FileType file_type) -> ExtensionType;

// ========================================================================
// BASE SEGMENT (20 dimensions, 80 bytes fixed)
// ========================================================================

/**
 * @brief Base feature segment shared by ALL file types
 *
 * Contains universal features that are meaningful for any file:
 * - Metadata (3 dims): size, magic confidence, printable ratio
 * - Statistics (8 dims): entropy, byte distribution metrics
 * - Structure (5 dims): header entropy, local variance, skewness/kurtosis
 * - N-gram aggregates (4 dims): bigram uniqueness, concentration, RLE/dict potential
 *
 * Memory: Exactly 80 bytes (20 × sizeof(float))
 * Alignment: Packed structure for serialization
 */
#pragma pack(push, 1)
struct BaseSegment {
    // === Metadata Features [0-2] ===
    float file_size_log2;         /// [0] log₂(file_size + 1) / 32.0, range [0, 1+]
    float magic_confidence;       /// [1] Magic Bytes match score, range [0, 1]
    float printable_ratio;        /// [2] ASCII printable char ratio, range [0, 1]

    // === Statistical Features [3-10] ===
    float shannon_entropy;        /// [3] Shannon entropy in bits/byte, range [0, 8]
    float min_entropy;            /// [4] Min-entropy (worst-case), range [0, 8]
    float unique_byte_ratio;      /// [5] Unique byte values / 256, range [0.0039, 1]
    float mean_byte_norm;         /// [6] Mean byte value / 255.0, range [0, 1]
    float std_byte_norm;          /// [7] Std dev / 115.0, range [0, 1]
    float longest_run_log2;       /// [8] log₂(longest_run + 1) / 20.0, range [0, 1]
    float zero_byte_ratio;        /// [9] Zero byte (0x00) ratio, range [0, 1]
    float high_bit_ratio;         /// [10] High bit (≥128) ratio, range [0, 1]

    // === Structural Features [11-15] ===
    float header_entropy;         /// [11] First 1024 bytes entropy, range [0, 8]
    float local_entropy_var;      /// [12] Local entropy variance, typically [0, 2]
    float block_boundary_density; /// [13] Block boundary density, range [0, 1]
    float skewness;               /// [14] Distribution skewness (normalized [-1, 1])
                                ///     🔑 KEY: Encryption/compressed/random detector
    float kurtosis;               /// [15] Excess kurtosis (normalized)

    // === N-gram Aggregation Features [16-19] ===
    float unique_bigram_ratio;    /// [16] Bigram uniqueness via Count-Min Sketch
    float bigram_topk_conc;       /// [17] Top-K bigram concentration (K=10)
    float rle_potential;          /// [18] RLE compressibility estimate [0=good, 1=bad]
    float dict_potential;         /// [19] LZ77 dictionary potential [0=good, 1=bad]

    static constexpr size_t NUM_DIMENSIONS = 20;
    static constexpr size_t SIZE = NUM_DIMENSIONS * sizeof(float);  // 80 bytes

    auto reset() -> void;

    auto operator[](size_t index) -> float&;

    auto operator[](size_t index) const -> const float&;
};
#pragma pack(pop)

static_assert(sizeof(BaseSegment) == 80, "BaseSegment must be exactly 80 bytes");

// ========================================================================
// EXTENSION DATA UNION (variable 5-8 dimensions, 20-32 bytes)
// ========================================================================

#pragma pack(push, 1)

/**
 * @brief Text/Code extension features (5 dimensions, 20 bytes)
 *
 * For source code, markup languages, plain text, configuration files.
 */
struct TextCodeExtension {
    float language_score;     /// [20] Language entropy score (0=pure code, 1=natural lang)
    float syntax_density;     /// [21] Syntax symbol density ({}, ;, (), [] frequency)
    float line_ending;        /// [22] Line ending: 0=LF, 0.5=mixed, 1=CRLF
    float indentation_style;  /// [23] Indent: 0=spaces, 1=tabs, 0.5=mixed
    float comment_ratio;      /// [24] Comment lines ratio (meaningful for source code)

    static constexpr size_t NUM_DIMENSIONS = 5;
    static constexpr size_t SIZE = NUM_DIMENSIONS * sizeof(float);  // 20 bytes
};

/**
 * @brief Image extension features (8 dimensions, 32 bytes)
 *
 * For PNG, JPEG, BMP, GIF, TIFF, WebP, AVIF images.
 * KEY FEATURE: is_lossless_original determines if format conversion is viable.
 */
struct ImageExtension {
    float width_norm;             /// [20] Width normalized to 4096
    float height_norm;            /// [21] Height normalized to 4096
    float bit_depth;              /// [22] Bits per pixel: {8, 16, 24, 32}
    float has_alpha;              /// [23] Alpha channel presence: {0, 1}
    float color_mode;             /// [24] Color mode: 0=gray, 1=RGB, 2=RGBA, 3=CMYK, 4=indexed
    float is_lossless_original;   /// [25] 🔑 KEY: 1=BMP/RAW (convertible), 0=JPEG (already compressed)
    float jpeg_quality_estimate;  /// [26] JPEG quality estimate [0,100] (0 if not JPEG)
    float compression_savings;    /// [27] Estimated space savings ratio [0, 1]

    static constexpr size_t NUM_DIMENSIONS = 8;
    static constexpr size_t SIZE = NUM_DIMENSIONS * sizeof(float);  // 32 bytes
};

/**
 * @brief Audio extension features (6 dimensions, 24 bytes)
 *
 * For WAV, MP3, FLAC, OGG, AAC audio files.
 */
struct AudioExtension {
    float sample_rate_norm;   /// [20] Sample rate normalized (44.1kHz→0.69)
    float bit_depth;          /// [21] Bit depth: {8, 16, 24, 32}
    float channels;           /// [22] Channel count: {1, 2, 6, 8}
    float duration_norm;      /// [23] Duration normalized (3600s→0.68)
    float is_lossless;        /// [24] Lossless flag: 1=WAV/FLAC, 0=MP3/AAC
    float bitrate_norm;       /// [25] Bitrate normalized

    static constexpr size_t NUM_DIMENSIONS = 6;
    static constexpr size_t SIZE = NUM_DIMENSIONS * sizeof(float);  // 24 bytes
};

/**
 * @brief Video extension features (7 dimensions, 28 bytes)
 *
 * For MP4, MKV, AVI, FLV, WebM video files.
 */
struct VideoExtension {
    float width_norm;          /// [20] Video width normalized to 4K
    float height_norm;         /// [21] Video height normalized to 4K
    float fps_norm;            /// [22] Frame rate normalized (60fps→0.75)
    float duration_norm;       /// [23] Duration normalized
    float codec_type;          /// [24] Codec enum: H.264/H.265/VP9/AV1/etc.
    float is_lossless;         /// [25] Lossless encoding flag
    float compression_savings; /// [26] Estimated recompression savings [0, 1]

    static constexpr size_t NUM_DIMENSIONS = 7;
    static constexpr size_t SIZE = NUM_DIMENSIONS * sizeof(float);  // 28 bytes
};

/**
 * @brief Archive/Compressed extension features (4 dimensions, 16 bytes)
 *
 * For ZIP, GZIP, 7Z, RAR, TAR, XZ archives.
 */
struct ArchiveExtension {
    float inner_format;         /// [20] Primary internal file format type
    float current_ratio;        /// [21] Current compression ratio (original/compressed)
    float file_count_norm;      /// [22] Internal file count normalized
    float recompress_potential; /// [23] Recompression potential [0=none, 1=high]

    static constexpr size_t NUM_DIMENSIONS = 4;
    static constexpr size_t SIZE = NUM_DIMENSIONS * sizeof(float);  // 16 bytes
};

/**
 * @brief Binary/Executable extension features (5 dimensions, 20 bytes)
 *
 * For PE, ELF, Mach-O executables, databases, generic binary data.
 */
struct BinaryExtension {
    float structure_density;  /// [20] Structured vs random binary ratio
    float padding_ratio;      /// [21] Alignment padding byte ratio
    float alignment;          /// [22] Alignment granularity (512/4096/etc.)
    float endianness;         /// [23] Byte order: 0=LE, 1=BE, 0.5=mixed
    float exec_score;         /// [24] Valid machine code score [0, 1]

    static constexpr size_t NUM_DIMENSIONS = 5;
    static constexpr size_t SIZE = NUM_DIMENSIONS * sizeof(float);  // 20 bytes
};

/**
 * @brief Union of all possible extension segments
 *
 * Uses maximum space (32 bytes for ImageExtension) but actual usage
 * depends on ext_dim field which specifies valid dimensions.
 */
union ExtensionData {
    TextCodeExtension text;
    ImageExtension image;
    AudioExtension audio;
    VideoExtension video;
    ArchiveExtension archive;
    BinaryExtension binary;

    ExtensionData() : text{} {}  // Default initialize as zeros
};
#pragma pack(pop)

// ========================================================================
// COMPLETE FEATURE VECTOR V3
// ========================================================================

#pragma pack(push, 1)

/**
 * @brief Complete v3.0 segmented feature vector
 *
 * Memory layout (packed):
 * - BaseSegment:     80 bytes (offset 0)
 * - ExtensionType:    1 byte  (offset 80)
 * - ExtensionDim:     1 byte  (offset 81)
 * - ExtensionData:   20-32 bytes (offset 82, variable)
 *
 * Total: 103-114 bytes per vector (depends on extension type)
 * When padded to 33 dimensions for ML: 132 bytes fixed
 */
struct FeatureVectorV3 {
    BaseSegment base;

    ExtensionType ext_type;  /// Which extension segment is active
    uint8_t ext_dim;         /// Actual number of extension dimensions used
    ExtensionData ext;       /// Union holding the active extension data

    auto total_bytes() const -> size_t;

    auto to_padded_array(size_t target_dim = 33) const -> std::vector<float>;

    auto reset() -> void;

    auto validate() const -> bool;
};
#pragma pack(pop)

// ========================================================================
// UTILITY CONSTANTS
// ========================================================================

namespace constants {

/// Maximum padded dimension for ML models
constexpr size_t MAX_PADDED_DIMENSIONS = 33;

/// Size of padded vector in bytes (for ML input)
constexpr size_t MAX_PADDED_SIZE = MAX_PADDED_DIMENSIONS * sizeof(float);  // 132 bytes

/// Count-Min Sketch parameters (for N-gram extraction)
constexpr size_t CMS_WIDTH = 4096;   /// Sketch width (columns)
constexpr size_t CMS_DEPTH = 4;      /// Sketch depth (hash functions)
constexpr size_t CMS_SIZE = CMS_WIDTH * CMS_DEPTH * sizeof(uint32_t);  // 64 KB

/// Local entropy window size (bytes)
constexpr size_t LOCAL_ENTROPY_WINDOW = 1024;

/// Header analysis size (bytes)
constexpr size_t HEADER_ANALYSIS_SIZE = 1024;

/// Block boundary detection block size (bytes)
constexpr size_t BLOCK_SIZE = 1024;

/// Minimum consecutive zeros to qualify as boundary
constexpr size_t MIN_ZERO_RUN_FOR_BOUNDARY = 8;

}  // namespace constants

}  // namespace ade
}  // namespace compressor