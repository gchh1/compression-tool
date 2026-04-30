#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "EntryHeader.hpp"
#include "Pipeline.hpp"

namespace compressor::archiver {

using core::AlgorithmID;

class PackWriter {
   public:
    PackWriter() = default;

    auto beginFile(const std::string& filepath, AlgorithmID comp_algo,
                   AlgorithmID preproc_algo = AlgorithmID::None) -> void;

    auto pushFileData(std::span<const uint8_t> data) -> void;

    auto pullOutput(void) -> std::span<const uint8_t> const;

    auto consumeOutput(size_t n) -> void;

    auto endFile(void) -> void;

    auto finish(void) -> void;

   private:
    EntryHeader entry_header_;

    std::vector<uint8_t> output_buffer_;

    std::unique_ptr<processor::Pipeline> pipeline_;

    size_t output_pos_{0};

    size_t header_offset_{0};

    size_t current_compressed_size_{0};

    bool finished_{false};
    bool file_open_{false};

    auto drainOutput(void) -> void;

    auto closeCurrentFile(void) -> void;
};

}  // namespace compressor::archiver
