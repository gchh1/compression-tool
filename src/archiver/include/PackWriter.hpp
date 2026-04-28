/**
 * @file PackWriter.hpp
 * @author yhc
 * @brief For raw files or folder, we need to `compress` and `package` them into
 *        a `package`. As for the procedure, each file is compressed to [header]
 *        + [compressed context] seperately.
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
#include <span>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "EntryHeader.hpp"
#include "StreamProcessor.hpp"

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

    std::vector<uint8_t> current_comp_buffer_;

    std::unique_ptr<processor::StreamProcessor> compressor_;

    std::unique_ptr<processor::StreamProcessor> preprocessor_;

    std::vector<uint8_t> output_bffer_;

    size_t output_pos_{0};

    size_t header_offset_{0};

    size_t current_compressed_size_{0};

    bool finished_{false};
    bool file_open_{false};

    auto drainProcessor(void) -> void;

    auto closeCurrentFile(void) -> void;
};
}  // namespace compressor::archiver
