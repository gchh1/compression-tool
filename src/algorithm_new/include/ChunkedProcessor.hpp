#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "LZencoding.hpp"
#include "MemoryPool.hpp"

namespace compressor::algorithm {

struct ChunkedProcessConfig {
    size_t chunk_size{1 << 20};
    size_t search_window{4096};
};

class ChunkedProcessor {
public:
    explicit ChunkedProcessor(MemoryPool& pool, ChunkedProcessConfig cfg = {})
        : pool_(pool), cfg_(std::move(cfg)) {}

    struct ChunkResult {
        std::vector<Triple> triples;
        bool is_last;
        size_t absolute_start;
    };

    using ProcessFn = std::function<ChunkResult(
        const uint8_t* data, size_t size,
        size_t abs_offset, bool is_last)>;

    using CancelCheck = std::function<bool()>;

    std::vector<uint8_t> process_to_bytes(
        const std::vector<uint8_t>& input,
        ProcessFn process_fn,
        const EncodingConfig& enc_cfg,
        CancelCheck on_cancel = nullptr);

private:
    MemoryPool& pool_;
    ChunkedProcessConfig cfg_;
};

inline std::vector<uint8_t> ChunkedProcessor::process_to_bytes(
    const std::vector<uint8_t>& input,
    ProcessFn process_fn,
    const EncodingConfig& enc_cfg,
    CancelCheck on_cancel) {

    if (input.empty()) return {};

    const size_t n = input.size();
    const size_t sw = cfg_.search_window;
    const size_t effective_chunk = std::max(cfg_.chunk_size, sw);

    std::vector<uint8_t> output;
    output.reserve(n / 4);
    compressor::utils::_buffer emit_pending;
    size_t pos = 0;

    while (pos < n) {
        if (on_cancel && on_cancel()) {
            throw std::runtime_error("cancelled");
        }

        auto slot = pool_.acquire();
        if (!slot) throw std::runtime_error("ChunkedProcessor: pool exhausted");

        bool is_last = (pos + effective_chunk >= n);
        size_t chunk_end = is_last ? n : (pos + effective_chunk);
        size_t prefix = (pos > sw) ? sw : pos;

        auto& buf = slot.data();
        buf.reserve(prefix + (chunk_end - pos));
        buf.assign(input.begin() + static_cast<std::ptrdiff_t>(pos - prefix),
                   input.begin() + static_cast<std::ptrdiff_t>(chunk_end));

        auto result = process_fn(buf.data(), buf.size(), pos - prefix, is_last);
        slot.release();

        if (!result.triples.empty()) {
            auto encoded = encoding_triple_lz(result.triples, enc_cfg, emit_pending, false);
            if (!encoded.empty()) {
                output.insert(output.end(), encoded.begin(), encoded.end());
            }
        }

        if (is_last) break;
        size_t advance = (chunk_end > pos + sw) ? (chunk_end - sw) : chunk_end;
        if (advance <= pos) advance = pos + 1;
        pos = advance;
    }

    auto tail = encoding_triple_lz(std::vector<Triple>{}, enc_cfg, emit_pending, true);
    if (!tail.empty()) {
        output.insert(output.end(), tail.begin(), tail.end());
    }

    return output;
}

}  // namespace compressor::algorithm
