/**
 * @file PackReader.hpp
 * @author yhc
 * @brief
 * @version 0.1
 * @date 2026-04-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "PackWriter.hpp"
#include "StreamProcessor.hpp"

namespace compressor::archiver {
class PackReader {
   public:
    explicit PackReader(std::vector<uint8_t> data);

    auto getEntries(void) const -> const std::vector<EntryHeader>&;

    auto extractStream(size_t index) const
        -> std::unique_ptr<processor::StreamProcessor>;
};
}  // namespace compressor::archiver