#include "StreamingCancel.hpp"

#include <cstdio>

namespace compressor::core_new {

namespace {
std::atomic<bool> g_streaming_cancel_flag{false};
}

bool is_streaming_cancel_requested() {
    bool v = g_streaming_cancel_flag.load(std::memory_order_relaxed);
    if (v) {
        static std::atomic<uint32_t> check_counter{0};
        uint32_t c = check_counter.fetch_add(1, std::memory_order_relaxed);
        if ((c & 0x3FF) == 0) {
            fprintf(stderr, "[CANCEL_TRACE] core_new::is_streaming_cancel_requested() = TRUE (check #%u)\n", c);
            fflush(stderr);
        }
    }
    return v;
}

void set_streaming_cancel_requested(bool requested) {
    if (requested) {
        fprintf(stderr, "[CANCEL_TRACE] core_new::set_streaming_cancel_requested(true) FLAG SET\n");
        fflush(stderr);
    }
    g_streaming_cancel_flag.store(requested, std::memory_order_relaxed);
}

}  // namespace compressor::core_new