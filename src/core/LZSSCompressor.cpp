#include "LZSSCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "LZSS.hpp"
#include "ICompressor.hpp"

// === DEBUG_BLOCK_BEGIN (可删除) ===
#include <cstdio>
#include <cstdarg>
#include <mutex>
static FILE* g_lzss_comp_log = nullptr;
static std::mutex g_lzss_comp_log_mtx;
static void lzss_comp_log_write(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_lzss_comp_log_mtx);
    if (!g_lzss_comp_log) {
        g_lzss_comp_log = fopen("lzss_gui_debug.log", "a");
    }
    if (g_lzss_comp_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_lzss_comp_log, fmt, args);
        va_end(args);
        fprintf(g_lzss_comp_log, "\n");
        fflush(g_lzss_comp_log);
    }
}
// === DEBUG_BLOCK_END ===

namespace compressor {
namespace core {
auto LZSSCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    // === DEBUG_BLOCK_BEGIN (可删除) ===
    lzss_comp_log_write("[LZSSCompressor::compress] ENTER data_size=%zu dict=%zu min_match=%zu flag=%d",
                        data.size(), dictionary_buffer_size_, min_match_length_, (int)use_flag_encoding_);
    // === DEBUG_BLOCK_END ===

    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    size_t actual_min_match = min_match_length_;
    if (actual_min_match == 0) {
        actual_min_match = 3;
    }
    result.data = algorithm::LZSS::compress(data, dictionary_buffer_size_, actual_min_match, use_flag_encoding_);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();

    // === DEBUG_BLOCK_BEGIN (可删除) ===
    lzss_comp_log_write("[LZSSCompressor::compress] EXIT orig=%zu comp=%zu ratio=%.4f time_ms=%.2f success=%d",
                        result.original_size, result.compressed_size, result.compression_ratio,
                        result.time_ms, (int)result.success);
    // === DEBUG_BLOCK_END ===

    return result;
}

auto LZSSCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    // === DEBUG_BLOCK_BEGIN (可删除) ===
    lzss_comp_log_write("[LZSSCompressor::decompress] ENTER data_size=%zu min_match=%zu flag=%d",
                        data.size(), min_match_length_, (int)use_flag_encoding_);
    // === DEBUG_BLOCK_END ===

    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    size_t actual_min_match = min_match_length_;
    if (actual_min_match == 0) {
        actual_min_match = 3;
    }
    result.data = algorithm::LZSS::decompress(data, actual_min_match, use_flag_encoding_);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();

    // === DEBUG_BLOCK_BEGIN (可删除) ===
    lzss_comp_log_write("[LZSSCompressor::decompress] EXIT orig=%zu decomp=%zu time_ms=%.2f success=%d",
                        result.original_size, result.compressed_size, result.time_ms, (int)result.success);
    // === DEBUG_BLOCK_END ===

    return result;
}

}  // namespace core

}  // namespace compressor