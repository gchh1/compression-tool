#include "DPFlateBin64kDebug.hpp"



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



constexpr uint32_t kBinary64kBytes = 64u * 1024u;

constexpr size_t kArmBufMin = kBinary64kBytes;

constexpr size_t kArmBufMax = kBinary64kBytes + 512u;

constexpr size_t kDefaultDumpMax = 64u;



std::mutex g_mu;

std::ofstream g_out;

bool g_enabled{false};

bool g_armed{false};

uint32_t g_target{0};

uint64_t g_line{0};

size_t g_dump_max{kDefaultDumpMax};



void write_line(const char* step, const char* msg) {

    std::lock_guard<std::mutex> lock(g_mu);

    if (!g_out.is_open()) {

        return;

    }

    ++g_line;

    g_out << '#' << g_line << ' ' << step << ' ' << msg << '\n';

    g_out.flush();

}



size_t parse_dump_max_env() {

    const char* env = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_DUMP_MAX");

    if (!env || !env[0]) {

        return kDefaultDumpMax;

    }

    char* end = nullptr;

    const unsigned long v = std::strtoul(env, &end, 10);

    if (end == env || v == 0) {

        return kDefaultDumpMax;

    }

    return static_cast<size_t>(v > 4096u ? 4096u : v);

}



}  // namespace



void DPFlateBin64kDebug::init_once() {

    static bool done = false;

    if (done) {

        return;

    }

    done = true;

    g_dump_max = parse_dump_max_env();

    const char* env = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_DEBUG");

    if (!env || !env[0]) {

        return;

    }

    std::string path;

    if (env[0] == '1' && env[1] == '\0') {

        path = "dpflate_bin64k_containers.log";

    } else {

        path = env;

    }

    {

        std::lock_guard<std::mutex> lock(g_mu);

        g_out.open(path, std::ios::out | std::ios::trunc);

        g_enabled = g_out.is_open();

        if (g_enabled) {

            g_out << "=== DPFlate binary_64k container debug ===\n";

            g_out << "env=WEBCOMPRESS_DPFLATE_BIN64K_DEBUG path=" << path << '\n';

            g_out << "dump_max_bytes=" << g_dump_max << '\n';

            g_out.flush();

        }

    }

    if (g_enabled) {

        write_line("INIT", "logger enabled");

        if (const char* only = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_ONLY");

            only && only[0] == '1') {

            g_armed = true;

            g_target = kBinary64kBytes;

            write_line("ARM", "WEBCOMPRESS_DPFLATE_BIN64K_ONLY=1 pre-arm");

        }

        if (const char* tgt = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_TARGET");

            tgt && tgt[0]) {

            char* end = nullptr;

            const unsigned long v = std::strtoul(tgt, &end, 10);

            if (end != tgt && v == kBinary64kBytes) {

                g_armed = true;

                g_target = kBinary64kBytes;

                write_line("ARM", "WEBCOMPRESS_DPFLATE_BIN64K_TARGET=65536 pre-arm");

            }

        }

    }

}



bool DPFlateBin64kDebug::enabled() {

    init_once();

    return g_enabled;

}



bool DPFlateBin64kDebug::active() {

    init_once();

    return g_enabled && g_armed;

}



size_t DPFlateBin64kDebug::dump_max_bytes() {

    init_once();

    return g_dump_max;

}



void DPFlateBin64kDebug::arm_session(uint32_t target_total_len, bool use_3hm) {

    init_once();

    if (!g_enabled || !use_3hm) {

        return;

    }

    if (target_total_len != kBinary64kBytes) {

        return;

    }

    {

        std::lock_guard<std::mutex> lock(g_mu);

        g_armed = true;

        g_target = target_total_len;

    }

    char buf[128];

    std::snprintf(buf, sizeof(buf), "target_total=%u use_3hm=1", target_total_len);

    write_line("ARM", buf);

}



void DPFlateBin64kDebug::try_arm_from_buffer(bool use_3hm, size_t input_buf_size,

                                            uint32_t window_abs, bool is_last_chunk) {

    init_once();

    if (!g_enabled || g_armed || !use_3hm) {

        return;

    }

    if (const char* only = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_ONLY");

        only && only[0] == '1') {

        arm_session(kBinary64kBytes, true);

        return;

    }

    if (const char* tgt = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_TARGET");

        tgt && tgt[0]) {

        char* end = nullptr;

        const unsigned long v = std::strtoul(tgt, &end, 10);

        if (end != tgt && v == kBinary64kBytes) {

            arm_session(kBinary64kBytes, true);

            return;

        }

    }

    if (window_abs != 0) {

        return;

    }

    if (!is_last_chunk) {

        return;

    }

    if (input_buf_size < kArmBufMin || input_buf_size > kArmBufMax) {

        return;

    }

    arm_session(kBinary64kBytes, true);

}



void DPFlateBin64kDebug::logf(const char* step, const char* fmt, ...) {

    if (!active() || !step || !fmt) {

        return;

    }

    char msg[2048];

    va_list ap;

    va_start(ap, fmt);

    std::vsnprintf(msg, sizeof(msg), fmt, ap);

    va_end(ap);

    char line[2200];

    if (g_target != 0) {

        std::snprintf(line, sizeof(line), "target=%u %s", g_target, msg);

    } else {

        std::snprintf(line, sizeof(line), "%s", msg);

    }

    write_line(step, line);

}



void DPFlateBin64kDebug::log_hex(const char* step, const char* tag, const uint8_t* data,

                                 size_t len) {

    if (!active() || !step || !tag) {

        return;

    }

    const size_t n = data ? std::min(len, g_dump_max) : 0;

    char msg[4096];

    int off = std::snprintf(msg, sizeof(msg), "[%s] len=%zu hex=", tag, len);

    for (size_t i = 0; i < n && off > 0 && static_cast<size_t>(off) < sizeof(msg) - 4; ++i) {

        off += std::snprintf(msg + off, sizeof(msg) - static_cast<size_t>(off), "%02x%s",

                             data[i], (i + 1 < n) ? " " : "");

    }

    if (len > n) {

        std::snprintf(msg + off, sizeof(msg) - static_cast<size_t>(off), " ...");

    }

    logf(step, "%s", msg);

}



void DPFlateBin64kDebug::log_u32_slice(const char* step, const char* tag,

                                      const uint32_t* data, size_t count, size_t offset_in_arr) {

    if (!active() || !step || !tag || !data || count == 0) {

        return;

    }

    const size_t n = std::min(count, static_cast<size_t>(16));

    char msg[1024];

    int off = std::snprintf(msg, sizeof(msg), "[%s] off=%zu count=%zu u32=", tag, offset_in_arr,

                            count);

    for (size_t i = 0; i < n && off > 0 && static_cast<size_t>(off) < sizeof(msg) - 8; ++i) {

        off += std::snprintf(msg + off, sizeof(msg) - static_cast<size_t>(off), "%u%s",

                             data[i], (i + 1 < n) ? " " : "");

    }

    if (count > n) {

        std::snprintf(msg + off, sizeof(msg) - static_cast<size_t>(off), " ...");

    }

    logf(step, "%s", msg);

}



void DPFlateBin64kDebug::log_dp_cells(const char* step, const char* tag,

                                      const StreamingDpCell* cells, size_t count,

                                      uint32_t abs_base, size_t start_index) {

    if (!active() || !step || !tag || !cells || count == 0) {

        return;

    }

    const size_t n = std::min(count - start_index, static_cast<size_t>(12));

    for (size_t i = 0; i < n; ++i) {

        const StreamingDpCell& c = cells[start_index + i];

        logf(step, "[%s] abs=%u cost=%u len=%u off=%u", tag,

             abs_base + static_cast<uint32_t>(start_index + i), c.cost, c.length, c.offset);

    }

    if (count - start_index > n) {

        logf(step, "[%s] ... %zu more cells", tag, count - start_index - n);

    }

}



void dpflate_bin64k_log_streaming_dp(const char* op, uint32_t commit_or_abs, size_t cur_sz,

                                     size_t next_sz, uint32_t cur_base, uint32_t next_base) {

    if (!DPFlateBin64kDebug::active()) {

        return;

    }

    DPFlateBin64kDebug::logf(

        "STREAMING_DP", "op=%s abs/commit=%u cur.size=%zu next.size=%zu cur_base=%u next_base=%u",

        op, commit_or_abs, cur_sz, next_sz, cur_base, next_base);

}



}  // namespace compressor::algorithm



namespace compressor::processor {



void dpflate_bin64k_log_pipeline(const char* step, const char* fmt, ...) {

    if (!algorithm::DPFlateBin64kDebug::active() || !step || !fmt) {

        return;

    }

    char msg[2048];

    va_list ap;

    va_start(ap, fmt);

    std::vsnprintf(msg, sizeof(msg), fmt, ap);

    va_end(ap);

    algorithm::DPFlateBin64kDebug::logf(step, "%s", msg);

}



void dpflate_bin64k_log_pipeline_bytes(const char* step, const char* tag, const uint8_t* data,

                                       size_t len) {

    if (!algorithm::DPFlateBin64kDebug::active()) {

        return;

    }

    algorithm::DPFlateBin64kDebug::log_hex(step, tag, data, len);

}



}  // namespace compressor::processor

