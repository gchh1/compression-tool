/**
 * @file Pipeline.hpp
 * @author yhc
 * @brief
 * @version 0.1
 * @date 2026-04-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "IAlgorithm.hpp"
#include "StreamProcessor.hpp"

namespace compressor::processor {

using algorithm::IAlgorithm;

class Pipeline {
   public:
    Pipeline(std::unique_ptr<IAlgorithm> first,
             std::unique_ptr<IAlgorithm> second = nullptr);

    auto push(std::span<const uint8_t> data, bool is_last = false) -> void;

    auto pull(void) -> std::span<const uint8_t>;

    auto consume(size_t n) -> void;

    auto finish(void) -> void;

    auto isFinished(void) const -> bool;

   private:
    std::unique_ptr<StreamProcessor> first_;
    std::unique_ptr<StreamProcessor> second_;
    bool finished_{false};

    auto drainInternal(void) -> void;
};

}  // namespace compressor::processor
