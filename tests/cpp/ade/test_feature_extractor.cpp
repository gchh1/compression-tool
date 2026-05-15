/**
 * @file test_feature_extractor.cpp
 * @author ADE Module - Comprehensive Unit Tests for Feature Extraction v3.0
 * @brief Validates all components: Magic Bytes, CMS, Base/Extension Extractors
 * @version 3.0
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * Test Categories:
 * 1. Data Structure Validation (FeatureVectorV3)
 * 2. Magic Bytes Detection (30+ formats)
 * 3. Count-Min Sketch Accuracy
 * 4. Base Segment Feature Extraction (20 dims)
 * 5. Extension Segment Extraction (6 types × 5-8 dims each)
 * 6. End-to-End Pipeline Integration
 * 7. JSON Serialization Correctness
 * 8. Edge Cases and Error Handling
 *
 * Run with: ./test_ade_feature_extractor
 */

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

// Include all ADE headers
#include "CountMinSketch.hpp"
#include "ExtensionExtractors.hpp"
#include "FeatureExtractorV3.hpp"
#include "FeatureVectorV3.hpp"
#include "MagicBytesDetector.hpp"

using namespace compressor::ade;

// ========================================================================
// TEST INFRASTRUCTURE
// ========================================================================

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct TestRunner_##name { \
        TestRunner_##name() { \
            std::cout << "  [RUN ]  " #name << "... "; \
            try { \
                test_##name(); \
                ++tests_passed; \
                std::cout << "[PASS]\n"; \
            } catch (const std::exception& e) { \
                ++tests_failed; \
                std::cout << "[FAIL] " << e.what() << "\n"; \
            } catch (...) { \
                ++tests_failed; \
                std::cout << "[FAIL] Unknown exception\n"; \
            } \
            ++tests_run; \
        } \
    } runner_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

#define ASSERT_FALSE(cond) \
    if ((cond)) throw std::runtime_error("Assertion failed (should be false): " #cond)

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::ostringstream oss; \
        oss << "Expected " << (b) << " but got " << (a); \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_NEAR(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { \
        std::ostringstream oss; \
        oss << "Expected ~" << (b) << " but got " << (a) << " (eps=" << (eps) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_THROW(expr, exc_type) \
    { bool caught = false; \
      try { expr; } catch (const exc_type&) { caught = true; } \
      if (!caught) throw std::runtime_error("Expected exception not thrown"); }

// ========================================================================
// CATEGORY 1: DATA STRUCTURE VALIDATION
// ========================================================================

TEST(base_segment_size_and_layout) {
    // Verify BaseSegment is exactly 80 bytes
    ASSERT_EQ(sizeof(BaseSegment), 80);

    BaseSegment base{};
    base.reset();

    // Verify all fields are zero after reset
    for (size_t i = 0; i < BaseSegment::NUM_DIMENSIONS; ++i) {
        ASSERT_EQ(base[i], 0.0f);
    }
}

TEST(feature_vector_v3_total_size) {
    FeatureVectorV3 vec{};

    // Empty vector should have minimal size (base + type + dim + no extension)
    vec.ext_type = ExtensionType::NONE;
    vec.ext_dim = 0;
    ASSERT_EQ(vec.total_bytes(), 82);  // 80 + 1 + 1

    // Text extension (5 dims = 20 bytes)
    vec.reset();
    vec.ext_type = ExtensionType::TEXT;
    vec.ext_dim = TextCodeExtension::NUM_DIMENSIONS;
    ASSERT_EQ(vec.total_bytes(), 102);  // 82 + 20

    // Image extension (8 dims = 32 bytes - largest)
    vec.reset();
    vec.ext_type = ExtensionType::IMAGE;
    vec.ext_dim = ImageExtension::NUM_DIMENSIONS;
    ASSERT_EQ(vec.total_bytes(), 114);  // 82 + 32

    // Verify padded array size
    auto padded = vec.to_padded_array(33);
    ASSERT_EQ(padded.size(), 33);
}

TEST(feature_vector_validation) {
    FeatureVectorV3 vec{};

    // Valid configuration with text extension
    vec.ext_type = ExtensionType::TEXT;
    vec.ext_dim = TextCodeExtension::NUM_DIMENSIONS;
    ASSERT_TRUE(vec.validate());

    // Invalid: dimension mismatch
    vec.ext_type = ExtensionType::IMAGE;
    // ext_dim still 5 from TEXT, but IMAGE needs 8 → should fail
    ASSERT_FALSE(vec.validate());

    // Fix it
    vec.ext_dim = ImageExtension::NUM_DIMENSIONS;
    ASSERT_TRUE(vec.validate());

    // NONE type should have 0 dimensions
    vec.ext_type = ExtensionType::NONE;
    vec.ext_dim = 0;
    ASSERT_TRUE(vec.validate());
}

// ========================================================================
// CATEGORY 2: MAGIC BYTES DETECTION
// ========================================================================

TEST(magic_detect_png) {
    uint8_t png_sig[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52  // IHDR follows
    };

    MagicBytesDetector detector;
    auto result = detector.detect(png_sig, sizeof(png_sig));

    ASSERT_EQ(result.type, FileType::IMAGE_PNG);
    ASSERT_TRUE(result.confidence > 0.95f);
    ASSERT_EQ(result.ext_type, ExtensionType::IMAGE);
}

TEST(magic_detect_jpeg) {
    uint8_t jpeg_sig[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10,
                          0x4A, 0x46, 0x49, 0x46, 0x00};  // JFIF

    MagicBytesDetector detector;
    auto result = detector.detect(jpeg_sig, sizeof(jpeg_sig));

    ASSERT_EQ(result.type, FileType::IMAGE_JPEG);
    ASSERT_TRUE(result.confidence > 0.9f);
}

TEST(magic_detect_zip) {
    uint8_t zip_sig[] = {0x50, 0x4B, 0x03, 0x04};  // PK\x03\x04

    MagicBytesDetector detector;
    auto result = detector.detect(zip_sig, sizeof(zip_sig));

    ASSERT_EQ(result.type, FileType::ARCHIVE_ZIP);
    ASSERT_TRUE(result.confidence > 0.9f);
}

TEST(magic_detect_pdf) {
    uint8_t pdf_sig[] = {'%', 'P', 'D', 'F', '-'};

    MagicBytesDetector detector;
    auto result = detector.detect(pdf_sig, sizeof(pdf_sig));

    ASSERT_EQ(result.type, FileType::DOC_PDF);
    ASSERT_TRUE(result.confidence > 0.98f);
}

TEST(magic_detect_elf) {
    uint8_t elf_sig[] = {0x7F, 0x45, 0x4C, 0x46, 0x02, 0x01};
    // 64-bit Little Endian ELF

    MagicBytesDetector detector;
    auto result = detector.detect(elf_sig, sizeof(elf_sig));

    ASSERT_EQ(result.type, FileType::EXEC_ELF);
    ASSERT_TRUE(result.confidence > 0.95f);
}

TEST(magic_detect_text_heuristic) {
    // Create a simple text file content
    std::string text_content =
        "#include <iostream>\n"
        "int main() {\n"
        "    std::cout << \"Hello World\" << std::endl;\n"
        "    return 0;\n"
        "}\n";

    MagicBytesDetector detector;
    auto result = detector.detect(
        reinterpret_cast<const uint8_t*>(text_content.data()),
        text_content.size());

    // Should detect as text via heuristic (high printable ratio)
    ASSERT_TRUE(result.type == FileType::TEXT_PLAIN ||
                result.type == FileType::SOURCE_CPP ||
                result.confidence < 0.8f);  // Low confidence acceptable for heuristic
}

TEST(magic_detect_unknown_empty) {
    uint8_t empty_data[] = {0x00, 0x01, 0x02, 0x03};

    MagicBytesDetector detector;
    auto result = detector.detect(empty_data, sizeof(empty_data));

    // Should be UNKNOWN or BINARY_GENERIC (heuristic fallback)
    ASSERT_TRUE(result.type == FileType::UNKNOWN ||
                result.type == FileType::BINARY_GENERIC);
}

// ========================================================================
// CATEGORY 3: COUNT-MIN SKETCH ACCURACY
// ========================================================================

TEST(cms_basic_operations) {
    CountMinSketch<> cms;

    // Initially empty
    ASSERT_EQ(cms.query(12345), 0);
    ASSERT_EQ(cms.total_count(), 0);

    // Add one item
    cms.update(42);
    ASSERT_EQ(cms.query(42), 1);
    ASSERT_EQ(cms.total_count(), 1);

    // Add same item multiple times
    for (int i = 0; i < 100; ++i) {
        cms.update(42);
    }
    ASSERT_TRUE(cms.query(42) >= 100);  // May overestimate due to collisions
    ASSERT_EQ(cms.total_count(), 101);
}

TEST(cms_memory_size) {
    // Verify memory usage matches specification
    ASSERT_EQ(CountMinSketch<>::memory_size(), 65536);  // 64 KB exactly
}

TEST(cms_uniqueness_estimation) {
    CountMinSketch<> cms;

    // Add many unique items (simulating high entropy data)
    for (uint32_t i = 0; i < 10000; ++i) {
        cms.update(i);
    }

    float uniqueness = cms.estimate_uniqueness_ratio();
    // High entropy data should have moderate-to-high uniqueness ratio
    ASSERT_TRUE(uniqueness > 0.3f && uniqueness < 1.0f);
}

TEST(cms_concentration_calculation) {
    CountMinSketch<> cms;

    // Create skewed distribution: item 0 appears 90% of time
    for (uint32_t i = 0; i < 9000; ++i) {
        cms.update(0);  // Dominant item
    }
    for (uint32_t i = 1; i <= 1000; ++i) {
        cms.update(i);  // Rare items
    }

    float concentration = cms.calculate_topk_concentration(10);
    // Should show high concentration due to dominant item
    ASSERT_TRUE(concentration > 0.5f);
}

// ========================================================================
// CATEGORY 4: BASE SEGMENT FEATURE EXTRACTION
// ========================================================================

TEST(base_extract_empty_file) {
    BaseFeatureExtractor extractor;
    DetectionResult detection{};
    detection.type = FileType::UNKNOWN;
    detection.confidence = 0.0f;

    auto result = extractor.extract(nullptr, 0, detection);

    // All features should be zero
    for (size_t i = 0; i < BaseSegment::NUM_DIMENSIONS; ++i) {
        ASSERT_EQ(result.base[i], 0.0f);
    }
}

TEST(base_extract_simple_text) {
    const char* text_data =
        "Hello, World! This is a simple test string for feature extraction.\n"
        "It contains ASCII printable characters and some line breaks.\n"
        "The quick brown fox jumps over the lazy dog.\n";

    BaseFeatureExtractor extractor;
    DetectionResult detection{};
    detection.type = FileType::TEXT_PLAIN;
    detection.confidence = 0.8f;

    auto result = extractor.extract(
        reinterpret_cast<const uint8_t*>(text_data),
        strlen(text_data),
        detection);

    // Verify basic properties of text data
    ASSERT_TRUE(result.base.printable_ratio > 0.85f);  // High printable ratio
    ASSERT_TRUE(result.base.shannon_entropy > 4.0f);   // Some entropy
    ASSERT_TRUE(result.base.shannon_entropy < 7.0f);   // But not maxed out
    ASSERT_TRUE(result.base.zero_byte_ratio < 0.05f);  // Few zeros in text
}

TEST(base_extract_high_entropy_random) {
    // Generate pseudo-random data (simulating encrypted/compressed data)
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int> dist(0, 255);

    size_t size = 4096;
    std::vector<uint8_t> random_data(size);
    for (auto& byte : random_data) {
        byte = static_cast<uint8_t>(dist(rng));
    }

    BaseFeatureExtractor extractor;
    DetectionResult detection{};
    detection.type = FileType::ENCRYPTED;
    detection.confidence = 0.7f;

    auto result = extractor.extract(random_data.data(), size, detection);

    // Random data characteristics
    ASSERT_TRUE(result.base.shannon_entropy > 7.5f);  // Near maximum (8 bits)
    ASSERT_TRUE(result.base.unique_byte_ratio > 0.9f);  // Most byte values present
    ASSERT_TRUE(std::abs(result.base.skewness) < 0.3f);  // Near-symmetric distribution
}

TEST(base_extract_repetitive_data) {
    // Create highly repetitive data (good compression candidate)
    std::vector<uint8_t> repetitive_data(4096, 0xAB);  // All same byte

    BaseFeatureExtractor extractor;
    DetectionResult detection{};
    detection.type = FileType::BINARY_GENERIC;
    detection.confidence = 0.5f;

    auto result = extractor.extract(repetitive_data.data(),
                                     repetitive_data.size(), detection);

    // Repetitive data characteristics
    ASSERT_TRUE(result.base.shannon_entropy < 1.0f);  // Very low entropy
    ASSERT_TRUE(result.base.unique_byte_ratio < 0.01f);  // Only 1 unique byte
    ASSERT_TRUE(result.base.longest_run_log2 > 0.3f);  // Long runs present (4096 same bytes → log2(4097)/20 ≈ 0.6)
}

TEST(base_extract_mixed_content) {
    // Simulate HTML/CSS/JS mixed content
    std::string mixed_content =
        "<!DOCTYPE html>\n"
        "<html><head><style>body{margin:0;padding:0;}</style></head>\n"
        "<body><script>alert('test');</script></body></html>";

    BaseFeatureExtractor extractor;
    DetectionResult detection{};
    detection.type = FileType::HTML;
    detection.confidence = 0.83f;

    auto result = extractor.extract(
        reinterpret_cast<const uint8_t*>(mixed_content.data()),
        mixed_content.size(),
        detection);

    // Mixed content should have moderate entropy and higher local variance
    ASSERT_TRUE(result.base.local_entropy_var > 0.0f);  // Some variation expected
}

// ========================================================================
// CATEGORY 5: EXTENSION SEGMENT EXTRACTION
// ========================================================================

TEST(extension_text_code) {
    std::string cpp_code =
        "#include <iostream>\n"
        "// Comment line\n"
        "class MyClass {\n"
        "public:\n"
        "    void method() {\n"
        "        int x = 42;\n"
        "    }\n"
        "};\n";

    TextCodeExtractor extractor;
    auto ext = extractor.extract(
        reinterpret_cast<const uint8_t*>(cpp_code.data()),
        cpp_code.size(),
        FileType::SOURCE_CPP);

    // Code-like characteristics
    ASSERT_TRUE(ext.syntax_density > 0.05f);  // Has syntax characters
    ASSERT_TRUE(ext.language_score < 0.8f);   // More code than natural language
    ASSERT_TRUE(ext.comment_ratio > 0.0f);     // Has comments
}

TEST(extension_image_png) {
    // Minimal valid PNG file header (8-byte signature + minimal IHDR)
    uint8_t png_data[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG signature
        0x00, 0x00, 0x00, 0x0D,  // Length of IHDR (13 bytes)
        0x49, 0x48, 0x44, 0x52,  // "IHDR"
        0x00, 0x00, 0x01, 0x00,  // Width: 256 pixels
        0x00, 0x00, 0x01, 0x00,  // Height: 256 pixels
        0x08,                      // Bit depth: 8
        0x06,                      // Color type: RGBA (has alpha)
        0x00, 0x00, 0x00          // Compression, filter, interlace
    };

    ImageExtractor extractor;
    auto ext = extractor.extract(png_data, sizeof(png_data), FileType::IMAGE_PNG);

    ASSERT_TRUE(ext.width_norm > 0.0f && ext.width_norm < 1.0f);
    ASSERT_TRUE(ext.height_norm > 0.0f && ext.height_norm < 1.0f);
    ASSERT_EQ(static_cast<int>(ext.bit_depth), 8);
    ASSERT_TRUE(ext.has_alpha > 0.5f);  // RGBA has alpha
    ASSERT_TRUE(ext.is_lossless_original > 0.5f);  // PNG is lossless
}

TEST(extension_image_bmp) {
    // Minimal BMP header (54 bytes DIB header)
    std::vector<uint8_t> bmp_data(138, 0);  // File header (14) + DIB header (40) + padding

    // BMP signature
    bmp_data[0] = 'B';
    bmp_data[1] = 'M';

    // File size at offset 2 (little-endian)
    bmp_data[2] = 138;  // 0x8A
    bmp_data[3] = 0x00;
    bmp_data[4] = 0x00;
    bmp_data[5] = 0x00;

    // Pixel data offset at offset 10
    bmp_data[10] = 54;  // 0x36
    bmp_data[11] = 0x00;
    bmp_data[12] = 0x00;
    bmp_data[13] = 0x00;

    // DIB header size at offset 14 (BITMAPINFOHEADER = 40)
    bmp_data[14] = 40;
    bmp_data[15] = 0x00;
    bmp_data[16] = 0x00;
    bmp_data[17] = 0x00;

    // Width at offset 18 (100 pixels)
    bmp_data[18] = 100;
    bmp_data[19] = 0x00;
    bmp_data[20] = 0x00;
    bmp_data[21] = 0x00;

    // Height at offset 22 (100 pixels)
    bmp_data[22] = 100;
    bmp_data[23] = 0x00;
    bmp_data[24] = 0x00;
    bmp_data[25] = 0x00;

    // Planes at offset 26 (must be 1)
    bmp_data[26] = 1;
    bmp_data[27] = 0x00;

    // Bits per pixel at offset 28 (24-bit RGB)
    bmp_data[28] = 24;
    bmp_data[29] = 0x00;

    // Compression at offset 30 (0 = uncompressed)
    bmp_data[30] = 0;
    bmp_data[31] = 0;
    bmp_data[32] = 0;
    bmp_data[33] = 0;

    ImageExtractor extractor;
    auto ext = extractor.extract(bmp_data.data(), bmp_data.size(), FileType::IMAGE_BMP);

    ASSERT_TRUE(ext.is_lossless_original > 0.5f);  // Uncompressed BMP is lossless original
    ASSERT_EQ(static_cast<int>(ext.bit_depth), 24);
    ASSERT_TRUE(ext.compression_savings > 0.5f);  // BMP→PNG has huge savings potential
}

TEST(extension_audio_wav) {
    // Minimal WAV file header (44 bytes)
    std::vector<uint8_t> wav_data(44, 0);

    // RIFF header
    wav_data[0] = 'R'; wav_data[1] = 'I'; wav_data[2] = 'F'; wav_data[3] = 'F';
    // File size (placeholder)
    wav_data[4] = 36; wav_data[5] = 0; wav_data[6] = 0; wav_data[7] = 0;
    // WAVE format
    wav_data[8] = 'W'; wav_data[9] = 'A'; wav_data[10] = 'V'; wav_data[11] = 'E';

    // "fmt " sub-chunk
    wav_data[12] = 'f'; wav_data[13] = 'm'; wav_data[14] = 't'; wav_data[15] = ' ';
    // Sub-chunk size (16 bytes for PCM)
    wav_data[16] = 16; wav_data[17] = 0; wav_data[18] = 0; wav_data[19] = 0;
    // Audio format: 1 = PCM
    wav_data[20] = 1; wav_data[21] = 0;
    // Channels: 2 (stereo)
    wav_data[22] = 2; wav_data[23] = 0;
    // Sample rate: 44100 Hz
    wav_data[24] = 0x44; wav_data[25] = 0xAC; wav_data[26] = 0; wav_data[27] = 0;
    // Byte rate: 176400 (44100 × 2 × 2)
    wav_data[28] = 0x44; wav_data[29] = 0xAC; wav_data[30] = 0; wav_data[31] = 0;
    // Block align: 4
    wav_data[32] = 4; wav_data[33] = 0;
    // Bits per sample: 16
    wav_data[34] = 16; wav_data[35] = 0;

    // "data" sub-chunk
    wav_data[36] = 'd'; wav_data[37] = 'a'; wav_data[38] = 't'; wav_data[39] = 'a';
    // Data size (placeholder)
    wav_data[40] = 0; wav_data[41] = 0; wav_data[42] = 0; wav_data[43] = 0;

    AudioExtractor extractor;
    auto ext = extractor.extract(wav_data.data(), wav_data.size(), FileType::AUDIO_WAV);

    ASSERT_TRUE(ext.is_lossless > 0.5f);  // PCM WAV is lossless
    ASSERT_EQ(static_cast<int>(ext.channels), 2);  // Stereo
    ASSERT_EQ(static_cast<int>(ext.bit_depth), 16);
    ASSERT_TRUE(ext.sample_rate_norm > 0.2f && ext.sample_rate_norm < 0.25f);  // ~44.1kHz normalized
}

TEST(extension_archive_zip) {
    // Minimal ZIP local file header
    std::vector<uint8_t> zip_data(30, 0);

    // ZIP signature
    zip_data[0] = 0x50; zip_data[1] = 0x4B; zip_data[2] = 0x03; zip_data[3] = 0x04;
    // Version needed to extract (20)
    zip_data[4] = 20; zip_data[5] = 0;
    // General purpose bit flag
    zip_data[6] = 0; zip_data[7] = 0;
    // Compression method: 8 = Deflate
    zip_data[8] = 8; zip_data[9] = 0;
    // Last mod time/date (zeros OK)
    // CRC-32 (zeros OK)
    // Compressed size: 100
    zip_data[18] = 100; zip_data[19] = 0; zip_data[20] = 0; zip_data[21] = 0;
    // Uncompressed size: 200
    zip_data[22] = 200; zip_data[23] = 0; zip_data[24] = 0; zip_data[25] = 0;

    ArchiveExtractor extractor;
    auto ext = extractor.extract(zip_data.data(), zip_data.size(), FileType::ARCHIVE_ZIP);

    ASSERT_TRUE(ext.current_ratio >= 1.5f);  // Compression ratio > 1.5
    ASSERT_TRUE(ext.recompress_potential < 0.5f);  // Already compressed with deflate
}

TEST(extension_binary_pe) {
    // Minimal PE executable header
    std::vector<uint8_t> pe_data(512, 0);

    // MZ signature
    pe_data[0] = 'M'; pe_data[1] = 'Z';

    // PE header offset at 0x3C (let's say at 0x80)
    pe_data[0x3C] = 0x80; pe_data[0x3D] = 0; pe_data[0x3E] = 0; pe_data[0x3F] = 0;

    // PE signature at offset 0x80
    pe_data[0x80] = 'P'; pe_data[0x81] = 'E'; pe_data[0x82] = 0; pe_data[0x83] = 0;

    // COFF header starts at 0x84
    // Machine: 0x14C (i386)
    pe_data[0x84] = 0x4C; pe_data[0x85] = 0x01;
    // Number of sections: 3
    pe_data[0x86] = 3; pe_data[0x87] = 0;
    // Timestamp (zeros OK)
    // Pointer to symbol table (zeros)
    // Number of symbols (zeros)
    // Optional header size: 224 (0xE0)
    pe_data[0x96] = 0xE0; pe_data[0x97] = 0;
    // Characteristics: 0x102 (EXECUTABLE_IMAGE | 32BIT_MACHINE)
    pe_data[0x98] = 0x02; pe_data[0x99] = 0x01;

    BinaryExtractor extractor;
    auto ext = extractor.extract(pe_data.data(), pe_data.size(), FileType::EXEC_PE);

    ASSERT_TRUE(ext.exec_score > 0.5f);  // Recognized as executable
    ASSERT_TRUE(ext.structure_density > 0.0f);  // Has sections
    ASSERT_TRUE(std::abs(ext.endianness) < 0.1f);  // PE is little-endian
}

// ========================================================================
// CATEGORY 6: END-TO-END PIPELINE INTEGRATION
// ========================================================================

TEST(end_to_end_png_extraction) {
    // Use the PNG test data from earlier
    uint8_t png_data[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x08, 0x06, 0x00, 0x00, 0x00
    };

    FeatureExtractorV3 extractor;
    auto result = extractor.extract(png_data, sizeof(png_data));

    // Verify pipeline completed successfully
    ASSERT_EQ(result.detection.type, FileType::IMAGE_PNG);
    ASSERT_EQ(result.vector.ext_type, ExtensionType::IMAGE);
    ASSERT_EQ(result.vector.ext_dim, ImageExtension::NUM_DIMENSIONS);
    ASSERT_TRUE(result.vector.validate());
    ASSERT_TRUE(result.extraction_time_ms >= 0.0f);
    ASSERT_EQ(result.input_size, sizeof(png_data));
}

TEST(end_to_end_text_extraction) {
    std::string text = "This is a plain text file for testing.\nLine 2 here.";

    FeatureExtractorV3 extractor;
    auto result = extractor.extract(
        reinterpret_cast<const uint8_t*>(text.data()),
        text.size());

    ASSERT_TRUE(result.detection.ext_type == ExtensionType::TEXT ||
                result.detection.ext_type == ExtensionType::NONE);
    ASSERT_TRUE(result.vector.base.printable_ratio > 0.8f);
}

TEST(end_to_end_json_serialization) {
    std::string test_data = "Test data for JSON serialization";

    FeatureExtractorV3 extractor;
    auto result = extractor.extract(
        reinterpret_cast<const uint8_t*>(test_data.data()),
        test_data.size());

    // Serialize to JSON
    std::string json_str = result.to_json();

    // Verify JSON structure
    ASSERT_TRUE(json_str.find("\"version\": \"3.0-Final\"") != std::string::npos);
    ASSERT_TRUE(json_str.find("\"base_segment\"") != std::string::npos);
    ASSERT_TRUE(json_str.find("\"extension_segment\"") != std::string::npos);
    ASSERT_TRUE(json_str.find("\"detection\"") != std::string::npos);
    ASSERT_TRUE(json_str.find("\"shannon_entropy\"") != std::string::npos);
}

TEST(end_to_end_ml_padded_array) {
    std::vector<uint8_t> dummy_data(1024, 0x55);

    FeatureExtractorV3 extractor;
    auto result = extractor.extract(dummy_data.data(), dummy_data.size());

    // Get ML-ready padded array
    auto ml_input = result.to_ml_input();

    // Should be exactly MAX_PADDED_DIMENSIONS (33)
    ASSERT_EQ(ml_input.size(), constants::MAX_PADDED_DIMENSIONS);

    // First 20 values should come from base segment
    ASSERT_EQ(ml_input[0], result.vector.base.file_size_log2);
    ASSERT_EQ(ml_input[19], result.vector.base.dict_potential);

    // Remaining positions depend on extension type (may be zero-padded)
}

// ========================================================================
// CATEGORY 7: EDGE CASES AND ERROR HANDLING
// ========================================================================

TEST(edge_case_null_pointer) {
    FeatureExtractorV3 extractor;
    auto result = extractor.extract(nullptr, 100);

    ASSERT_EQ(result.input_size, 0);
    ASSERT_EQ(result.detection.type, FileType::UNKNOWN);
}

TEST(edge_case_zero_size) {
    std::vector<uint8_t> empty_data;
    FeatureExtractorV3 extractor;
    auto result = extractor.extract(empty_data);

    ASSERT_EQ(result.input_size, 0);
    ASSERT_EQ(result.vector.ext_dim, 0);
}

TEST(edge_case_single_byte) {
    uint8_t single_byte[] = {0x42};

    FeatureExtractorV3 extractor;
    auto result = extractor.extract(single_byte, 1);

    ASSERT_EQ(result.input_size, 1);
    ASSERT_TRUE(result.vector.base.unique_byte_ratio <= (1.0f / 256.0f));
}

TEST(edge_case_large_file_simulation) {
    // Simulate a 1MB file (don't actually allocate 1MB in unit test)
    // Instead, verify the system can handle reasonable sizes
    std::vector<uint8_t> large_data(100000, 0xAA);  // 100KB of repeated pattern

    FeatureExtractorV3 extractor;
    auto result = extractor.extract(large_data.data(), large_data.size());

    ASSERT_EQ(result.input_size, 100000);
    ASSERT_TRUE(result.extraction_time_ms < 1000.0f);  // Should complete within 1 second
    ASSERT_TRUE(result.vector.base.shannon_entropy < 1.0f);  // Repetitive data
}

// ========================================================================
// MAIN - TEST RUNNER
// ========================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║  ADE Feature Vector Extraction v3.0 - Unit Tests     ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
    std::cout << "\nRunning " << tests_run << " registered tests...\n\n";

    // Note: Tests are automatically run by TestRunner constructors above
    // This main function just prints summary

    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "Test Results Summary:\n";
    std::cout << "  Total:  " << tests_run << "\n";
    std::cout << "  Passed: " << tests_passed << " ✓\n";
    std::cout << "  Failed: " << tests_failed << " ✗\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    if (tests_failed > 0) {
        std::cout << "\n⚠️  SOME TESTS FAILED! Review output above.\n";
        return 1;
    }

    std::cout << "\n✅ ALL TESTS PASSED!\n";
    std::cout << "\nMemory Statistics:\n";
    std::cout << FeatureExtractorV3::get_memory_statistics();

    return 0;
}