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
#include <memory>
#include <vector>

#include "EntryHeader.hpp"
#include "IDataReader.hpp"
#include "StreamProcessor.hpp"

namespace compressor::archiver {
class PackReader {
   public:
    explicit PackReader(std::unique_ptr<IDataReader> reader);

    /** @brief Extract all the `EntryHeader` */
    auto getEntries(void) const -> const std::vector<EntryHeader>&;

    /** @brief  */
    auto extractStream(size_t index) const
        -> std::unique_ptr<processor::StreamProcessor>;

   private:
    const size_t ENTRY_CHUNK_SIZE = 4096;
    const size_t INPUT_BUFFER_SIZE = 65536;

    std::unique_ptr<IDataReader> reader_;

    std::vector<EntryHeader> entries_;

    /** @brief  */
    auto buildIndex(void) -> void;
};
}  // namespace compressor::archiver