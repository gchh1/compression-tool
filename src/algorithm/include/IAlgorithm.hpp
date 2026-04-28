/**
 * @file IAlgorithm.hpp
 * @author Algorithm interface
 * @brief
 * @version 0.1
 * @date 2026-04-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <span>

#include "BitReader.hpp"
#include "BitWriter.hpp"
namespace compressor::algorithm {

/**
 * @brief
 *
 */
struct AlgorithmStatus {
    size_t bytes_consumed{0};
    size_t bytes_produced{0};

    bool need_input{false};
    bool need_output{false};

    bool done{false};
};

class IAlgorithm {
   public:
    virtual ~IAlgorithm() = default;

    virtual auto process(std::span<const uint8_t> read,
                         std::span<uint8_t> write, bool is_last_chunk)
        -> AlgorithmStatus = 0;

    virtual auto reset(void) -> void = 0;
};

class AlgorithmBase : public IAlgorithm {
   public:
    virtual ~AlgorithmBase() = default;

    auto process(std::span<const uint8_t> read, std::span<uint8_t> write,
                 bool is_last_chunk) -> AlgorithmStatus final;

    auto reset(void) -> void {}

   protected:
    utils::BitReader reader_;
    utils::BitWriter writer_;

    virtual auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk)
        -> void = 0;
};

}  // namespace compressor::algorithm