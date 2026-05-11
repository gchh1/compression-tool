#include "PackWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "DataChunk.hpp"
#include "Pipeline.hpp"
#include "PipelineBuilder.hpp"

namespace compressor::archiver {

auto PackWriter::beginFile(const std::string& filepath,
                           std::span<const AlgorithmID> chain) -> void {
    if (finished_) return;
    if (file_open_) closeCurrentFile();

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

    pipeline_ = processor::buildCompressionPipeline(entry_header_.algo_chain, pool_);

    file_open_ = true;
}

auto PackWriter::pushFileData(memory::DataChunk chunk) -> void {
    if (!file_open_) return;

    entry_header_.original_size += chunk.size();
    pipeline_->push(std::move(chunk));
    drainOutput();
}

auto PackWriter::pushFileData(std::span<const uint8_t> data) -> void {
    if (!file_open_ || data.empty()) return;

    auto v = std::make_shared<std::vector<uint8_t>>(data.begin(), data.end());
    auto size = data.size();
    pushFileData(memory::DataChunk::adopt(std::move(v), size));
}

auto PackWriter::pullOutput() -> std::span<const uint8_t> {
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

auto PackWriter::endFile() -> void {
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

auto PackWriter::finish() -> void {
    if (finished_) return;
    if (file_open_) closeCurrentFile();
    finished_ = true;
}

auto PackWriter::drainOutput() -> void {
    while (true) {
        auto chunk = pipeline_->pull();
        if (chunk.empty()) break;
        current_compressed_size_ += chunk.size();
        output_chunks_.push_back(std::move(chunk));
    }
}

auto PackWriter::closeCurrentFile() -> void {
    if (pipeline_) {
        pipeline_->finish();
        pipeline_.reset();
    }
    file_open_ = false;
}

}  // namespace compressor::archiver
