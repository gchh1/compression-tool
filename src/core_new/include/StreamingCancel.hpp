#pragma once

#include <atomic>

namespace compressor::core_new {

namespace detail_cancel {
inline std::atomic<bool>& flag() {
    static std::atomic<bool> v{false};
    return v;
}
}  // namespace detail_cancel

inline bool is_streaming_cancel_requested() {
    return detail_cancel::flag().load(std::memory_order_relaxed);
}

inline void set_streaming_cancel_requested(bool requested) {
    detail_cancel::flag().store(requested, std::memory_order_relaxed);
}

struct StreamingCancelGuard {
    explicit StreamingCancelGuard(bool initial = false) { set_streaming_cancel_requested(initial); }
    ~StreamingCancelGuard() { set_streaming_cancel_requested(false); }
    StreamingCancelGuard(const StreamingCancelGuard&) = delete;
    StreamingCancelGuard& operator=(const StreamingCancelGuard&) = delete;
};

}  // namespace compressor::core_new
