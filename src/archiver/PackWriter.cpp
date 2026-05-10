#include "PackWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "DataChunk.hpp"
#include "Pipeline.hpp"

namespace compressor::archiver {

/**
 * @brief Initialize the members of the `PackWriter` object. Sepecifically the
 *        `entry_header_`, `pipeline_`.
 *
 * @param filepath
 * @param chain
 */
auto PackWriter::beginFile(const std::string& filepath,
                           std::span<const AlgorithmID> chain) -> void {
    if (finished_) return;
    if (file_open_) closeCurrentFile();

    // Initialize the `entry_header_`
    entry_header_.filepath = filepath;
    entry_header_.algo_chain.assign(chain.begin(), chain.end());
    entry_header_.original_size = 0;
    entry_header_.compressed_size = 0;

    auto header = entry_header_.serialize();
    header_offset_ = header.size() - sizeof(uint64_t);
    header_buffer_ = std::move(header);
    header_pos_ = 0;

    output_chunks_.clear();
    chunk_idx_ = 0;
    current_compressed_size_ = 0;

    // Map the algorithmID to algorithm pointer and construct `pipeline_`
    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        if (auto a = core::createAlgorithm(id)) {
            algos.push_back(std::move(a));
        }
    }
    pipeline_ = std::make_unique<processor::Pipeline>(std::move(algos), pool_);

    file_open_ = true;
}

/**
 * @brief Given a chunk of data, call `pipeline_->push` to handle the file data
 *        and place the handled chunks to `output_chunks_`
 *
 * @param chunk
 */
auto PackWriter::pushFileData(memory::DataChunk chunk) -> void {
    if (!file_open_) return;

    entry_header_.original_size += chunk.size();
    pipeline_->push(std::move(chunk));
    drainOutput();
}

/**
 * @brief Wrapper the byte stream data to `DataChunk` and handle
 *
 * @param data
 */
auto PackWriter::pushFileData(std::span<const uint8_t> data) -> void {
    if (!file_open_ || data.empty()) return;

    auto v = std::make_shared<std::vector<uint8_t>>(data.begin(), data.end());
    auto size = data.size();
    pushFileData(memory::DataChunk::adopt(std::move(v), size));
}

/**
 * @brief Return a `chunk` of data from `output_chunks`
 *
 * @return std::span<const uint8_t>
 */
auto PackWriter::pullOutput(void) -> std::span<const uint8_t> {
    // Serve header first
    if (header_pos_ < header_buffer_.size())
        return {header_buffer_.data() + header_pos_,
                header_buffer_.size() - header_pos_};

    // Then serve compressed-data chunks
    while (chunk_idx_ < output_chunks_.size()) {
        auto v = output_chunks_[chunk_idx_].view();
        if (!v.empty()) return v;
        chunk_idx_++;
    }
    return {};
}

/**
 * @brief
 *
 * @param n
 */
auto PackWriter::consumeOutput(size_t n) -> void {
    // Consume from header
    if (header_pos_ < header_buffer_.size()) {
        size_t hdr_n = std::min(n, header_buffer_.size() - header_pos_);
        header_pos_ += hdr_n;
        n -= hdr_n;
        if (n == 0) return;
    }

    // Consume from chunks
    while (n > 0 && chunk_idx_ < output_chunks_.size()) {
        auto& chunk = output_chunks_[chunk_idx_];
        if (n >= chunk.size()) {
            n -= chunk.size();
            chunk_idx_++;
        } else {
            output_chunks_[chunk_idx_] = chunk.slice(n, chunk.size() - n);
            n = 0;
        }
    }

    // Drop fully-consumed chunks
    if (chunk_idx_ > 0) {
        output_chunks_.erase(output_chunks_.begin(),
                             output_chunks_.begin() + chunk_idx_);
        chunk_idx_ = 0;
    }
}

/**
 * @brief Clean the `pipeline_` and write back the `entry_header_` for
 *        [original_size] and [compressed_sizse]
 *
 */
auto PackWriter::endFile(void) -> void {
    if (!file_open_) return;

    pipeline_->finish();
    drainOutput();
    pipeline_.reset();

    // Patch original_size and compressed_size into header buffer
    std::memcpy(header_buffer_.data() + header_offset_ - sizeof(uint64_t),
                &entry_header_.original_size, sizeof(uint64_t));
    std::memcpy(header_buffer_.data() + header_offset_,
                &current_compressed_size_, sizeof(uint64_t));

    file_open_ = false;
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
 * @brief Pull data from `pipeline_` to `output_chunks_`
 *
 */
auto PackWriter::drainOutput(void) -> void {
    while (true) {
        auto chunk = pipeline_->pull();
        if (chunk.empty()) break;
        current_compressed_size_ += chunk.size();
        output_chunks_.push_back(std::move(chunk));
    }
}

/**
 * @brief Reset the `pipeline_` and close file
 *
 */
auto PackWriter::closeCurrentFile(void) -> void {
    if (pipeline_) {
        pipeline_->finish();
        pipeline_.reset();
    }
    file_open_ = false;
}

}  // namespace compressor::archiver
