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

/** @brief Will be used if memory pool is nullptr */
constexpr size_t OUT_CHUNK_SIZE = 65536;

class StreamProcessor {
   public:
    /** @brief Construct with Algorithm pointer and mempory pool */
    explicit StreamProcessor(std::unique_ptr<IAlgorithm> algo,
                             std::shared_ptr<memory::MemoryPool> pool = nullptr)
        : algo_(std::move(algo)), pool_(std::move(pool)) {}

    /** @brief Process the chunk of data via `algo_` */
    auto push(memory::DataChunk chunk, bool is_last = false) -> void;

    /** @brief Process data via `algo_` */
    auto push(std::span<const uint8_t> data, bool is_last = false) -> void;

    /** @brief Pull processed data, as well as the ownership */
    auto pull(void) -> memory::DataChunk;

    /** @brief  */
    auto consume(size_t n) -> void;

    /** @brief  */
    auto finish(void) -> std::vector<uint8_t>;

    /** @brief Return if the processor is finished */
    auto isFinished(void) const -> bool { return finished_; }

    /** @brief  */
    auto getBlockProfile(void) -> std::optional<algorithm::BlockProfile> {
        return algo_ ? algo_->getBlockProfile() : std::nullopt;
    }

   private:
    /** @brief The algorithm that the processor object uses */
    std::unique_ptr<IAlgorithm> algo_;

    /** @brief Input chunks */
    std::deque<memory::DataChunk> in_chunks_;
    size_t in_pos_{0};  // read offset within front chunk

    // Output
    std::shared_ptr<memory::MemoryPool> pool_;
    std::shared_ptr<std::vector<uint8_t>> current_out_;
    std::deque<memory::DataChunk> ready_chunks_;
    size_t out_pos_{0};

    bool finished_{false};

    auto readSpan(void) const -> std::span<const uint8_t>;
    auto consumeInput(size_t n) -> void;
    auto processChunks(bool is_last) -> void;
    auto publishCurrent(void) -> void;
};

/** @brief Transfer processed data from `from` to `to` without copying. */
auto drain(StreamProcessor& from, StreamProcessor& to) -> void;

}  // namespace compressor::processor
