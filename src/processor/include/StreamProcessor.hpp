#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <vector>

#include "DataChunk.hpp"
#include "IAlgorithm.hpp"
#include "MemoryPool.hpp"

namespace compressor::processor {

using algorithm::IAlgorithm;

constexpr size_t OUT_CHUNK_SIZE = 65536;

class StreamProcessor {
   public:
    explicit StreamProcessor(std::unique_ptr<IAlgorithm> algo,
                             std::shared_ptr<memory::MemoryPool> pool = nullptr)
        : algo_(std::move(algo)), pool_(std::move(pool)) {}

    /// Zero-copy push: takes ownership of a DataChunk.
    auto push(memory::DataChunk chunk, bool is_last = false) -> void;

    /// Convenience: copies span into an owned chunk, then delegates.
    auto push(std::span<const uint8_t> data, bool is_last = false) -> void;

    /// Pull processed data.  Ownership transfers to the caller.
    auto pull() -> memory::DataChunk;

    /// Consume n bytes from the front of the ready queue.
    auto consume(size_t n) -> void;

    auto finish() -> std::vector<uint8_t>;

    auto isFinished() const -> bool { return finished_; }

   private:
    std::unique_ptr<IAlgorithm> algo_;

    // ---- input ----
    std::deque<memory::DataChunk> in_chunks_;
    size_t in_pos_{0};  // read offset within front chunk

    // ---- pool-backed output ----
    std::shared_ptr<memory::MemoryPool> pool_;
    std::shared_ptr<std::vector<uint8_t>> current_out_;
    std::deque<memory::DataChunk> ready_chunks_;
    size_t out_pos_{0};
    bool finished_{false};

    // ---- helpers ----
    auto readSpan() const -> std::span<const uint8_t>;
    auto consumeInput(size_t n) -> void;
    auto processChunks(bool is_last) -> void;
    auto publishCurrent() -> void;
};

/// Transfer processed data from `from` to `to` without copying.
auto drain(StreamProcessor& from, StreamProcessor& to) -> void;

}  // namespace compressor::processor
