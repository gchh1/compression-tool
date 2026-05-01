/**
 * @file Delta.hpp
 * @author yhc
 * @brief Delta algorithm that inherit `AlgorithmBase`
 * @version 0.3
 * @date 2026-04-26
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor::algorithm {

/**
 * @brief
 *
 */
class DeltaEncode : public AlgorithmBase {
   public:
    explicit DeltaEncode(int quality = 100);

    ~DeltaEncode() = default;

    auto reset(void) -> void override;

   protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

   private:
    /** @brief Storing shift_ so that we don't need to calculate it every time
     * we call encode */
    int shift_{0};

    /** @brief Prev_ */
    uint8_t prev_{0};

    static constexpr size_t BUFFER_SIZE_ = 4096;
    std::vector<uint8_t> buffer_;
};

/**
 * @brief
 *
 */
class DeltaDecode : public AlgorithmBase {
   public:
    DeltaDecode();

    ~DeltaDecode() = default;

    auto reset(void) -> void override;

   protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

   private:
    static constexpr size_t BUFFER_SIZE_ = 4096;
    std::vector<uint8_t> buffer_;
    uint8_t prev_{0};
};

class Delta {
public:
    static std::vector<uint8_t> encode(std::vector<uint8_t> data, int quality = 100);
    static std::vector<uint8_t> decode(std::vector<uint8_t> data);
};

}  // namespace compressor::algorithm