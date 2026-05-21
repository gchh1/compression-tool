#include "StreamingCancel.hpp"

namespace compressor::core_new {

namespace {
std::atomic<bool> g_streaming_cancel_flag{false};
}

bool is_streaming_cancel_requested() {
    return g_streaming_cancel_flag.load(std::memory_order_relaxed);
}

void set_streaming_cancel_requested(bool requested) {
    g_streaming_cancel_flag.store(requested, std::memory_order_relaxed);
}

}  // namespace compressor::core_new