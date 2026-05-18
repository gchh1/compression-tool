#include "DPFlateTrace.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "DPFlate.hpp"
#include "TempFile.hpp"

namespace compressor::algorithm {

namespace {

auto env_cstr(const char* name) -> const char* {
    const char* v = std::getenv(name);
    return (v && v[0]) ? v : nullptr;
}

auto temp_file_size_bytes(const TempFile& tf) -> uint64_t {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(tf.path_, ec);
    return ec ? 0ULL : static_cast<uint64_t>(sz);
}

auto mix_u64(uint64_t h, uint64_t v) -> uint64_t {
    h ^= v;
    h *= 1099511628211ULL;
    return h;
}

auto mix_token(uint64_t h, uint16_t length, uint16_t offset) -> uint64_t {
    return mix_u64(h, (static_cast<uint64_t>(length) << 16) | static_cast<uint64_t>(offset));
}

}  // namespace

DPFlateTrace& DPFlateTrace::instance() {
    static DPFlateTrace inst;
    return inst;
}

void DPFlateTrace::configure_from_env() {
    if (file_enabled_) {
        return;
    }
    const char* path = env_cstr("WEBCOMPRESS_DPFLATE_TRACE");
    if (!path) {
        return;
    }
    const char* tag = env_cstr("WEBCOMPRESS_DPFLATE_TRACE_TAG");
    file_tag_ = tag ? tag : "run";
    out_.open(path, std::ios::out | std::ios::trunc);
    file_enabled_ = out_.is_open();
    step_ = 0;
    if (file_enabled_) {
        event("INIT", "path=%s", path);
    }
}

void DPFlateTrace::set_file_tag(const char* tag) {
    file_tag_ = tag ? tag : "?";
}

void DPFlateTrace::event(const char* phase, const char* fmt, ...) {
    configure_from_env();
    if (!file_enabled_ && !capturing()) {
        return;
    }
    char body[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    if (file_enabled_) {
        std::lock_guard<std::mutex> lock(mu_);
        out_ << "step=" << (++step_) << " tag=" << file_tag_ << " phase=" << phase << " " << body
             << '\n';
        out_.flush();
    }
}

void DPFlateTrace::begin_capture(const char* run_id) {
    std::lock_guard<std::mutex> lock(mu_);
    capture_run_id_ = run_id ? run_id : "run";
    capture_store_[capture_run_id_].clear();
}

void DPFlateTrace::end_capture() {
    std::lock_guard<std::mutex> lock(mu_);
    capture_run_id_.clear();
}

void DPFlateTrace::record_checkpoint(DPFlateCheckpoint cp) {
    event("SNAPSHOT", "phase=%s in_len=%u tokens=%llu tok_digest=0x%llx freq_digest=0x%llx "
                      "writer=%zu tempA=%llu tempB=%llu",
          cp.phase.c_str(), cp.total_in_len,
          static_cast<unsigned long long>(cp.total_tokens),
          static_cast<unsigned long long>(cp.forward_token_digest),
          static_cast<unsigned long long>(cp.freq_digest),
          cp.writer_bytes, static_cast<unsigned long long>(cp.temp_a_bytes),
          static_cast<unsigned long long>(cp.temp_b_bytes));

    std::lock_guard<std::mutex> lock(mu_);
    if (!capture_run_id_.empty()) {
        capture_store_[capture_run_id_].push_back(std::move(cp));
    }
}

const std::vector<DPFlateCheckpoint>& DPFlateTrace::checkpoints(const char* run_id) const {
    static const std::vector<DPFlateCheckpoint> kEmpty;
    const auto it = capture_store_.find(run_id ? run_id : "");
    return it == capture_store_.end() ? kEmpty : it->second;
}

std::string DPFlateTrace::diff_runs(const char* run_a, const char* run_b) const {
    const auto& a = checkpoints(run_a);
    const auto& b = checkpoints(run_b);
    std::ostringstream oss;
    if (a.empty() || b.empty()) {
        oss << "missing checkpoints (a=" << a.size() << " b=" << b.size() << ")";
        return oss.str();
    }

    static const char* kFinalPhases[] = {"COLLECT_DONE", "BACKTRACK_DONE", "BUILD_TREE_DONE",
                                         "EMIT_DONE"};
    auto fail_field = [&](const char* phase, const char* field) {
        oss << "diverge at phase=" << phase << " field=" << field;
    };
    for (const char* phase : kFinalPhases) {
        const DPFlateCheckpoint* ca = nullptr;
        const DPFlateCheckpoint* cb = nullptr;
        for (const auto& c : a) {
            if (c.phase == phase) {
                ca = &c;
                break;
            }
        }
        for (const auto& c : b) {
            if (c.phase == phase) {
                cb = &c;
                break;
            }
        }
        if (!ca || !cb) {
            oss << "missing phase=" << phase << " (a=" << (ca ? 1 : 0) << " b=" << (cb ? 1 : 0)
                << ")";
            return oss.str();
        }
        if (ca->total_in_len != cb->total_in_len) {
            fail_field(phase, "total_in_len");
            return oss.str();
        }
        if (ca->total_tokens != cb->total_tokens) {
            fail_field(phase, "total_tokens");
            return oss.str();
        }
        if (ca->forward_token_digest != cb->forward_token_digest) {
            fail_field(phase, "forward_token_digest");
            return oss.str();
        }
        if (ca->freq_digest != cb->freq_digest) {
            fail_field(phase, "freq_digest");
            return oss.str();
        }
        if (ca->end_link_len != cb->end_link_len || ca->end_link_off != cb->end_link_off) {
            fail_field(phase, "end_link");
            return oss.str();
        }
    }

    return {};
}

size_t DPFlateTrace::count_phase(const char* run_id, const char* phase) const {
    size_t n = 0;
    for (const auto& c : checkpoints(run_id)) {
        if (c.phase == phase) {
            ++n;
        }
    }
    return n;
}

void DPFlateTrace::clear_all() {
    std::lock_guard<std::mutex> lock(mu_);
    capture_store_.clear();
    capture_run_id_.clear();
}

void dpflate_trace_snapshot(DPFlate& enc, const char* phase) {
    DPFlateCheckpoint cp;
    cp.phase = phase ? phase : "?";
    cp.total_in_len = enc.total_in_len_;
    cp.total_tokens = enc.total_tokens_;
    cp.temp_a_bytes = temp_file_size_bytes(enc.temp_file_A_);
    cp.temp_b_bytes = temp_file_size_bytes(enc.temp_file_B_);
    cp.writer_bytes = enc.writer_.getBytesWritten();
    cp.window_abs_pos = enc.window_abs_pos_;
    cp.current_i = enc.current_i_;
    cp.input_buffer_size = enc.input_buffer_.size();

    if (cp.total_in_len > 0) {
        const auto& end_cell = enc.streaming_dp_.cell_at(cp.total_in_len);
        cp.end_link_len = end_cell.length;
        cp.end_link_off = end_cell.offset;
    }

    uint64_t tok_h = 14695981039346656037ULL;
    for (uint64_t k = 0; k < enc.total_tokens_; ++k) {
        uint16_t len = 0;
        uint16_t off = 0;
        enc.temp_tokens_b_.readBySeqIndex(enc.total_tokens_ - 1 - k, len, off);
        tok_h = mix_token(tok_h, len, off);
    }
    cp.forward_token_digest = tok_h;

    uint64_t freq_h = 14695981039346656037ULL;
    if (enc.use_3hfmtree_) {
        for (uint32_t v : enc.literal_freq_3hm_) {
            freq_h = mix_u64(freq_h, v);
        }
        for (uint32_t v : enc.offset_freq_3hm_) {
            freq_h = mix_u64(freq_h, v);
        }
        for (uint32_t v : enc.length_freq_3hm_) {
            freq_h = mix_u64(freq_h, v);
        }
    } else {
        for (uint32_t v : enc.freq_map_) {
            freq_h = mix_u64(freq_h, v);
        }
        for (uint32_t v : enc.dist_freq_) {
            freq_h = mix_u64(freq_h, v);
        }
    }
    cp.freq_digest = freq_h;

    DPFlateTrace::instance().record_checkpoint(std::move(cp));
}

}  // namespace compressor::algorithm
