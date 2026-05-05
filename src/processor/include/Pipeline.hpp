#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "DataChunk.hpp"
#include "IAlgorithm.hpp"
#include "MemoryPool.hpp"
#include "StreamProcessor.hpp"

namespace compressor::processor {

using algorithm::IAlgorithm;

class Pipeline {
   public:
    Pipeline(std::vector<std::unique_ptr<IAlgorithm>> algorithms,
             std::shared_ptr<memory::MemoryPool> pool = nullptr);

    /// Zero-copy push into the first stage.
    auto push(memory::DataChunk chunk, bool is_last = false) -> void;

    /// Convenience: wraps span in an owned DataChunk.
    auto push(std::span<const uint8_t> data, bool is_last = false) -> void;

    /// Pull processed data from the last stage.
    auto pull() -> memory::DataChunk;

    auto consume(size_t n) -> void;

    auto finish() -> void;

    auto isFinished() const -> bool;

    // Collect block-level profiling data from the compression algorithm.
    auto getBlockProfile() -> std::optional<algorithm::BlockProfile>;

   private:
    std::vector<std::unique_ptr<StreamProcessor>> stages_;
    bool finished_{false};

    auto drainAll() -> void;
};

}  // namespace compressor::processor
