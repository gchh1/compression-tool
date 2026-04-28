
#include "PackWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "AlgorithmFactory.hpp"
#include "StreamProcessor.hpp"

namespace compressor::archiver {
/**
 * @brief Called when front end scan a new file
 *
 * @param filepath
 * @param algo
 */
auto PackWriter::beginFile(const std::string& filepath, AlgorithmID algo)
    -> void {
    if (finished_) return;
    if (file_open_) closeCurrentFile();

    /* 1. Initialize the header */
    entry_header_.filepath = filepath;
    entry_header_.algorithm_id = algo;
    entry_header_.original_size = 0;
    entry_header_.compressed_size = 0;
    current_comp_buffer_.clear();

    /* 2. Hold the head place */
    auto header = entry_header_.serialize();
    header_offset_ = output_bffer_.size() + header.size() - sizeof(uint64_t);
    output_bffer_.insert(output_bffer_.end(), header.begin(), header.end());

    /* 3. Create compressor */
    auto compressor = core::createAlgorithm(algo);
    processor_ =
        std::make_unique<processor::StreamProcessor>(std::move(compressor));
    file_open_ = true;
}

/**
 * @brief
 *
 * @param data
 */
auto PackWriter::pushFileData(std::span<const uint8_t> data) -> void {
    if (!file_open_) return;

    entry_header_.original_size += data.size();
    processor_->push(data, false);
    drainProcessor();
}

/**
 * @brief
 *
 */
auto PackWriter::endFile(void) -> void {
    if (!file_open_) return;

    processor_->finish();
    drainProcessor();
    processor_.reset();

    auto* dest = output_bffer_.data() + header_offset_;
    std::memcpy(dest, &current_compressed_size_, sizeof(uint64_t));

    file_open_ = false;
}

/**
 * @brief
 *
 * @return std::span<const uint8_t> const
 */
auto PackWriter::pullOutput(void) -> std::span<const uint8_t> const {
    return {output_bffer_.data() + output_pos_,
            output_bffer_.size() - output_pos_};
}

/**
 * @brief
 *
 * @param n
 */
auto PackWriter::consumeOutput(size_t n) -> void {
    n = std::min(n, output_bffer_.size() - output_pos_);
    output_pos_ += n;
    if (output_pos_ == output_bffer_.size()) {
        output_bffer_.clear();
        output_pos_ = 0;
    }
}

/**
 * @brief
 *
 */
auto PackWriter::drainProcessor(void) -> void {
    while (true) {
        auto out = processor_->pull();
        if (out.empty()) break;
        output_bffer_.insert(output_bffer_.end(), out.begin(), out.end());
        current_compressed_size_ += out.size();
        processor_->consume(out.size());
    }
}

/**
 * @brief
 *
 */
auto PackWriter::finish(void) -> void {
    if (finished_) return;
    if (file_open_) closeCurrentFile();
    finished_ = true;
}

/**
 * @brief
 *
 */
auto PackWriter::closeCurrentFile(void) -> void {
    if (processor_) {
        processor_->finish();
        processor_.reset();
    }

    file_open_ = false;
    current_comp_buffer_.clear();
}

}  // namespace compressor::archiver