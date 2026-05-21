#pragma once

#include <atomic>

namespace compressor::core_new {

bool is_streaming_cancel_requested();

void set_streaming_cancel_requested(bool requested);

struct StreamingCancelGuard {
    explicit StreamingCancelGuard(bool initial = false) { set_streaming_cancel_requested(initial); }
    ~StreamingCancelGuard() { set_streaming_cancel_requested(false); }
    StreamingCancelGuard(const StreamingCancelGuard&) = delete;
    StreamingCancelGuard& operator=(const StreamingCancelGuard&) = delete;
};

}  // namespace compressor::core_new