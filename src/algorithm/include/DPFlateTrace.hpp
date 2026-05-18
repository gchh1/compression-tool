#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace compressor::algorithm {

class DPFlate;

/// Fingerprint at one pipeline phase (for memory vs streaming diff).
struct DPFlateCheckpoint {
    std::string phase;
    uint32_t total_in_len{0};
    uint64_t total_tokens{0};
    uint64_t temp_a_bytes{0};
    uint64_t temp_b_bytes{0};
    size_t writer_bytes{0};
    uint32_t end_link_len{0};
    uint32_t end_link_off{0};
    uint32_t window_abs_pos{0};
    size_t current_i{0};
    size_t input_buffer_size{0};
    uint64_t forward_token_digest{0};
    uint64_t freq_digest{0};
};

/// Runtime step trace for DPFlate.
/// - File log: set ``WEBCOMPRESS_DPFLATE_TRACE`` to a path; optional ``WEBCOMPRESS_DPFLATE_TRACE_TAG``.
/// - In-memory capture: call ``begin_capture`` / ``end_capture`` from tests (always records checkpoints).
class DPFlateTrace {
public:
    static DPFlateTrace& instance();

    void configure_from_env();
    bool enabled() const { return file_enabled_; }
    const std::string& tag() const { return file_tag_; }

    void set_file_tag(const char* tag);
    void event(const char* phase, const char* fmt, ...);

    void begin_capture(const char* run_id);
    void end_capture();
    bool capturing() const { return !capture_run_id_.empty(); }
    const std::string& capture_run_id() const { return capture_run_id_; }

    void record_checkpoint(DPFlateCheckpoint cp);
    const std::vector<DPFlateCheckpoint>& checkpoints(const char* run_id) const;

    /// First phase where fingerprints differ; empty if all match pairwise by phase name.
    std::string diff_runs(const char* run_a, const char* run_b) const;
    size_t count_phase(const char* run_id, const char* phase) const;
    void clear_all();

private:
    DPFlateTrace() = default;

    bool file_enabled_{false};
    std::string file_tag_{"?"};
    std::ofstream out_;
    std::mutex mu_;
    uint64_t step_{0};

    std::string capture_run_id_;
    std::unordered_map<std::string, std::vector<DPFlateCheckpoint>> capture_store_;
};

#define DPFLATE_TRACE_EVENT(phase, ...)                                           \
    do {                                                                          \
        if (compressor::algorithm::DPFlateTrace::instance().enabled() ||          \
            compressor::algorithm::DPFlateTrace::instance().capturing()) {        \
            compressor::algorithm::DPFlateTrace::instance().event(phase,          \
                                                                  __VA_ARGS__);   \
        }                                                                         \
    } while (0)

#define DPFLATE_TRACE_SNAPSHOT(self, phase)                                       \
    do {                                                                          \
        if (compressor::algorithm::DPFlateTrace::instance().enabled() ||          \
            compressor::algorithm::DPFlateTrace::instance().capturing()) {        \
            compressor::algorithm::dpflate_trace_snapshot((self), (phase));     \
        }                                                                         \
    } while (0)

}  // namespace compressor::algorithm
