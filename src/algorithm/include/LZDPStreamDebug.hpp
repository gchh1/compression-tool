#pragma once

#include <cstddef>
#include <cstdint>

namespace compressor::algorithm {

struct StreamingDpCell;

/// LZDP 流式路径调试（借鉴 ``DPFlateBin64kDebug``）。
/// ``WEBCOMPRESS_LZDP_STREAM_DEBUG=1`` 或绝对路径；``WEBCOMPRESS_LZDP_STREAM_DUMP_MAX``（默认 64）。
struct LZDPStreamDebug {
    static void init_once();
    static bool enabled();
    static bool active();
    static void arm_session(uint32_t target_total_len);
    static void try_arm_from_buffer(size_t input_buf_size, uint32_t window_abs, bool is_last_chunk);
    static void logf(const char* step, const char* fmt, ...);
    static void log_hex(const char* step, const char* tag, const uint8_t* data, size_t len);
    static void log_dp_cells(const char* step, const char* tag, const StreamingDpCell* cells,
                             size_t count, uint32_t abs_base, size_t start_index = 0);
    static size_t dump_max_bytes();
};

#define LZDP_STREAM_LOG(step, fmt, ...)                                               \
    do {                                                                              \
        if (::compressor::algorithm::LZDPStreamDebug::active()) {                     \
            ::compressor::algorithm::LZDPStreamDebug::logf((step), (fmt),             \
                                                            ##__VA_ARGS__);           \
        }                                                                             \
    } while (0)

void lzdp_stream_log_streaming_dp(const char* op, uint32_t commit_or_abs, size_t cur_sz,
                                 size_t next_sz, uint32_t cur_base, uint32_t next_base);

}  // namespace compressor::algorithm

namespace compressor::processor {

void lzdp_stream_log_pipeline(const char* step, const char* fmt, ...);
void lzdp_stream_log_pipeline_bytes(const char* step, const char* tag, const uint8_t* data,
                                    size_t len);

}  // namespace compressor::processor
