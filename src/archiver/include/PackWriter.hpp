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

    /** @brief Construct by MemoryPool */
    explicit PackWriter(std::shared_ptr<memory::MemoryPool> pool)
        : pool_(std::move(pool)) {}

    /** @brief Start a file entry with an algorithm chain (last = compressor).
     */
    auto beginFile(const std::string& filepath,
                   std::span<const AlgorithmID> chain) -> void;

    /** @brief Pass a chunk of raw file data and handle to compressed data */
    auto pushFileData(memory::DataChunk chunk) -> void;

    auto pushFileData(std::span<const uint8_t> data) -> void;

    /** @brief Pull a chunk of compressed data */
    auto pullOutput(void) -> std::span<const uint8_t>;

    /** @brief  */
    auto consumeOutput(size_t n) -> void;

    /** @brief File-level finish. Called when finish compressing a file */
    auto endFile(void) -> void;

    /** @brief Archive-level finish. Called when finish the archive */
    auto finish(void) -> void;

   private:
    /** @brief Header for the file being compressed */
    EntryHeader entry_header_;

    /** @brief Helper for header */
    std::vector<uint8_t> header_buffer_;
    size_t header_pos_{0};
    size_t header_offset_{0};

    /** @brief From where to `acquire` `chunk` to store data */
    std::shared_ptr<memory::MemoryPool> pool_;

    /** @brief Compressed chunk to be push back */
    std::deque<memory::DataChunk> output_chunks_;
    size_t chunk_idx_{0};

    /** @brief Pipeline that compress data via algorithm chain we specify */
    std::unique_ptr<processor::Pipeline> pipeline_;

    /** @brief  */
    uint64_t current_compressed_size_{0};

    /** @brief State flags */
    bool finished_{false};
    bool file_open_{false};

    /** @brief pipeline_ -> self.output_chunks */
    auto drainOutput(void) -> void;

    /** @brief Reset to origianl state */
    auto closeCurrentFile(void) -> void;
};

}  // namespace compressor::archiver
