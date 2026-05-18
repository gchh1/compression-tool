#pragma once

#include <cstddef>
#include <cstdint>

namespace compressor::algorithm {

struct StreamingDpCell;

/// binary_64k (65536 B) 专用容器转换调试：输出到单文件。
/// 启用：``WEBCOMPRESS_DPFLATE_BIN64K_DEBUG=1`` 或 ``WEBCOMPRESS_DPFLATE_BIN64K_DEBUG=<log路径>``
/// 可选：``WEBCOMPRESS_DPFLATE_BIN64K_DUMP_MAX`` 每段 hex 最大字节（默认 64）
struct DPFlateBin64kDebug {
    static void init_once();
    static bool enabled();
    static bool active();
    static void arm_session(uint32_t target_total_len, bool use_3hm);
    static void try_arm_from_buffer(bool use_3hm, size_t input_buf_size, uint32_t window_abs,
                                    bool is_last_chunk);
    static void logf(const char* step, const char* fmt, ...);

    static void log_hex(const char* step, const char* tag, const uint8_t* data, size_t len);
    static void log_u32_slice(const char* step, const char* tag, const uint32_t* data, size_t count,
                              size_t offset_in_arr = 0);
    static void log_dp_cells(const char* step, const char* tag, const StreamingDpCell* cells,
                             size_t count, uint32_t abs_base, size_t start_index = 0);

    static size_t dump_max_bytes();
};

#define DPFLATE_BIN64K_LOG(step, fmt, ...)                                            \
    do {                                                                              \
        if (::compressor::algorithm::DPFlateBin64kDebug::active()) {                  \
            ::compressor::algorithm::DPFlateBin64kDebug::logf((step), (fmt),          \
                                                              ##__VA_ARGS__);         \
        }                                                                             \
    } while (0)

void dpflate_bin64k_log_streaming_dp(const char* op, uint32_t commit_or_abs, size_t cur_sz,
                                     size_t next_sz, uint32_t cur_base, uint32_t next_base);

}  // namespace compressor::algorithm

namespace compressor::processor {

void dpflate_bin64k_log_pipeline(const char* step, const char* fmt, ...);
void dpflate_bin64k_log_pipeline_bytes(const char* step, const char* tag,
                                       const uint8_t* data, size_t len);

}  // namespace compressor::processor
