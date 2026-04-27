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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "IAlgorithm.hpp"

namespace compressor::core {

using algorithm::IAlgorithm;

constexpr size_t IN_CHUNK_SIZE = 65536;
constexpr size_t OUT_CHUNK_SIZE = 65536;

class StreamProcessor {
   public:
    /** @brief Instantial the `processor` with unique algorithm, like `Deflate`
     */
    explicit StreamProcessor(std::unique_ptr<IAlgorithm> algo) :;

    /** @brief Receive a chunk of data to process */
    auto push(std::span<const uint8_t>, bool is_last = false) -> void;

    /** @brief Return the processed data */
    auto pull() -> std::span<const uint8_t>;

    /** @brief  */
    auto consume(size_t n) -> void;

   private:
    std::unique_ptr<IAlgorithm> algo_;
};

}  // namespace compressor::core