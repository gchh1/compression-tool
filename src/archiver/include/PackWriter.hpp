#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "DataChunk.hpp"
#include "EntryHeader.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"

namespace compressor::archiver {

using core::AlgorithmID;

class PackWriter {
   public:
    PackWriter() = default;

    explicit PackWriter(std::shared_ptr<memory::MemoryPool> pool)
        : pool_(std::move(pool)) {}

    /** @brief Start a file entry with an algorithm chain (last = compressor). */
    auto beginFile(const std::string& filepath,
                   std::span<const AlgorithmID> chain) -> void;

    /** @brief  */
    auto pushFileData(memory::DataChunk chunk) -> void;
    auto pushFileData(std::span<const uint8_t> data) -> void;

    auto pullOutput() -> std::span<const uint8_t>;

    auto consumeOutput(size_t n) -> void;

    auto endFile() -> void;

    auto finish() -> void;

   private:
    EntryHeader entry_header_;

    std::shared_ptr<memory::MemoryPool> pool_;

    std::vector<uint8_t> header_buffer_;
    size_t header_pos_{0};
    size_t header_offset_{0};

    std::deque<memory::DataChunk> output_chunks_;
    size_t chunk_idx_{0};

    std::unique_ptr<processor::Pipeline> pipeline_;

    uint64_t current_compressed_size_{0};

    bool finished_{false};
    bool file_open_{false};

    auto drainOutput() -> void;

    auto closeCurrentFile() -> void;
};

}  // namespace compressor::archiver
