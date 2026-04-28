/**
 * @file StreamProcessor.hpp
 * @author
 * @brief
 * @version 0.1
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "IAlgorithm.hpp"
#include "RingBuffer.hpp"

namespace compressor::processor {

using algorithm::IAlgorithm;

constexpr size_t IN_CHUNK_SIZE = 65536;
constexpr size_t OUT_CHUNK_SIZE = 65536;

class StreamProcessor {
   public:
    /** @brief Instantial the `processor` with unique algorithm, like `Deflate`
     */
    explicit StreamProcessor(std::unique_ptr<IAlgorithm> algo)
        : algo_(std::move(algo)),
          in_buffer_(IN_CHUNK_SIZE),
          out_buffer_(OUT_CHUNK_SIZE) {}

    /** @brief Receive a chunk of data to process */
    auto push(std::span<const uint8_t> data, bool is_last = false) -> void;

    /** @brief Return the processed data */
    auto pull() -> std::span<const uint8_t>;

    /** @brief  */
    auto consume(size_t n) -> void;

    auto finish(void) -> std::vector<uint8_t>;

    auto isFinished(void) -> bool const { return finished_; }

   private:
    std::unique_ptr<IAlgorithm> algo_;

    RingBuffer in_buffer_;

    std::vector<uint8_t> out_buffer_;

    size_t out_pos_{0};
    bool finished_{false};

    // ===================================
    // Private helpful method
    // ===================================
    auto processChunks(bool is_last) -> void;
};

}  // namespace compressor::processor