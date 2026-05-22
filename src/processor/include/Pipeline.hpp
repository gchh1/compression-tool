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
    /** @brief Construct with `algorithm chain` and `memory pool` */
    Pipeline(std::vector<std::unique_ptr<IAlgorithm>> algorithms,
             std::shared_ptr<memory::MemoryPool> pool = nullptr);

    /** @brief Push raw data to process. The data have been wrapper as a chunk
     */
    auto push(memory::DataChunk chunk, bool is_last = false) -> void;

    /** @brief Push raw data to process. The data is byte stream */
    auto push(std::span<const uint8_t> data, bool is_last = false) -> void;

    /** @brief Pull a chunk of handled data */
    auto pull(void) -> memory::DataChunk;

    /** @brief  */
    auto consume(size_t n) -> void;

    auto finish() -> void;

    auto isFinished() const -> bool { return finished_; };

   private:
    std::vector<std::unique_ptr<StreamProcessor>> stages_;
    bool finished_{false};

    auto drainAll() -> void;
};

}  // namespace compressor::processor
