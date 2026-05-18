#include "LZDPStreamDebug.hpp"

#include "StreamingDpChunk.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

namespace compressor::algorithm {

namespace {

std::mutex g_mu;
std::ofstream g_out;
bool g_enabled{false};
bool g_armed{false};
uint32_t g_target{0};
uint64_t g_line{0};
size_t g_dump_max{64};

void write_line(const char* step, const char* msg) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_out.is_open()) {
        return;
    }
    ++g_line;
    g_out << '#' << g_line << ' ' << step << ' ' << msg << '\n';
    g_out.flush();
}

}  // namespace

void LZDPStreamDebug::init_once() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    const char* env = std::getenv("WEBCOMPRESS_LZDP_STREAM_DEBUG");
    if (!env || !env[0]) {
        return;
    }
    std::string path;
    if (env[0] == '1' && env[1] == '\0') {
        path = "lzdp_stream_containers.log";
    } else {
        path = env;
    }
    g_out.open(path, std::ios::out | std::ios::trunc);
    if (!g_out) {
        return;
    }
    g_enabled = true;
    g_dump_max = []() {
        const char* dm = std::getenv("WEBCOMPRESS_LZDP_STREAM_DUMP_MAX");
        if (!dm || !dm[0]) {
            return size_t{64};
        }
        const unsigned long v = std::strtoul(dm, nullptr, 10);
        return v == 0 ? size_t{64} : static_cast<size_t>(v > 4096 ? 4096 : v);
    }();
    write_line("INIT", ("path=" + path + " dump_max=" + std::to_string(g_dump_max)).c_str());
}

bool LZDPStreamDebug::enabled() {
    init_once();
    return g_enabled;
}

bool LZDPStreamDebug::active() {
    init_once();
    return g_enabled && g_armed;
}

void LZDPStreamDebug::arm_session(uint32_t target_total_len) {
    init_once();
    if (!g_enabled) {
        return;
    }
    g_target = target_total_len;
    g_armed = true;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "target_total_len=%u", target_total_len);
    write_line("ARM", buf);
}

void LZDPStreamDebug::try_arm_from_buffer(size_t input_buf_size, uint32_t window_abs,
                                          bool is_last_chunk) {
    if (!enabled() || g_armed) {
        return;
    }
    if (g_target == 0) {
        return;
    }
    const uint32_t total = window_abs + static_cast<uint32_t>(input_buf_size);
    if (total >= g_target && (is_last_chunk || input_buf_size >= g_target)) {
        arm_session(g_target);
    }
}

size_t LZDPStreamDebug::dump_max_bytes() {
    init_once();
    return g_dump_max;
}

void LZDPStreamDebug::logf(const char* step, const char* fmt, ...) {
    if (!active()) {
        return;
    }
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    write_line(step, msg);
}

void LZDPStreamDebug::log_hex(const char* step, const char* tag, const uint8_t* data,
                              size_t len) {
    if (!active() || !data) {
        return;
    }
    const size_t n = std::min(len, dump_max_bytes());
    std::string hex;
    hex.reserve(n * 3 + 32);
    for (size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), "%02x ", data[i]);
        hex += b;
    }
    char hdr[256];
    std::snprintf(hdr, sizeof(hdr), "%s len=%zu show=%zu: %s", tag, len, n, hex.c_str());
    logf(step, "%s", hdr);
}

void LZDPStreamDebug::log_dp_cells(const char* step, const char* tag, const StreamingDpCell* cells,
                                   size_t count, uint32_t abs_base, size_t start_index) {
    if (!active() || !cells) {
        return;
    }
    const size_t show = std::min(count - start_index, size_t{8});
    for (size_t i = 0; i < show; ++i) {
        const auto& c = cells[start_index + i];
        logf(step, "%s abs=%u cost=%u len=%u off=%u", tag,
             abs_base + static_cast<uint32_t>(start_index + i), c.cost, c.length, c.offset);
    }
}

void lzdp_stream_log_streaming_dp(const char* op, uint32_t commit_or_abs, size_t cur_sz,
                                size_t next_sz, uint32_t cur_base, uint32_t next_base) {
    LZDP_STREAM_LOG("STREAMING_DP", "%s commit_or_abs=%u cur_sz=%zu next_sz=%zu cur_base=%u next_base=%u",
                    op, commit_or_abs, cur_sz, next_sz, cur_base, next_base);
}

}  // namespace compressor::algorithm

namespace compressor::processor {

void lzdp_stream_log_pipeline(const char* step, const char* fmt, ...) {
    if (!::compressor::algorithm::LZDPStreamDebug::active()) {
        return;
    }
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ::compressor::algorithm::LZDPStreamDebug::logf(step, "%s", msg);
}

void lzdp_stream_log_pipeline_bytes(const char* step, const char* tag, const uint8_t* data,
                                    size_t len) {
    ::compressor::algorithm::LZDPStreamDebug::log_hex(step, tag, data, len);
}

}  // namespace compressor::processor
