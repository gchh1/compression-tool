#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "EntryHeader.hpp"
#include "IDataReader.hpp"

namespace compressor::processor {
class Pipeline;
}

namespace compressor::archiver {
class PackReader {
   public:
    explicit PackReader(std::unique_ptr<IDataReader> reader);

    auto getEntries(void) const -> const std::vector<EntryHeader>&;

    auto extractStream(size_t index) const
        -> std::unique_ptr<processor::Pipeline>;

   private:
    const size_t ENTRY_CHUNK_SIZE = 4096;
    const size_t INPUT_BUFFER_SIZE = 65536;

    std::unique_ptr<IDataReader> reader_;

    std::vector<EntryHeader> entries_;

    auto buildIndex(void) -> void;
};
}  // namespace compressor::archiver
