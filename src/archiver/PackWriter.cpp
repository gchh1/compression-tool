#include "PackWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "AlgorithmFactory.hpp"
#include "Pipeline.hpp"

namespace compressor::archiver {

auto PackWriter::beginFile(const std::string& filepath, AlgorithmID comp_algo,
                           AlgorithmID preproc_algo) -> void {
    if (finished_) return;
    if (file_open_) closeCurrentFile();

    entry_header_.filepath = filepath;
    entry_header_.preproc_algo_id = preproc_algo;
    entry_header_.comp_algo_id = comp_algo;
    entry_header_.original_size = 0;
    entry_header_.compressed_size = 0;

    auto header = entry_header_.serialize();
    header_offset_ =
        output_buffer_.size() + header.size() - sizeof(uint64_t);
    output_buffer_.insert(output_buffer_.end(), header.begin(), header.end());

    auto preproc = core::createAlgorithm(preproc_algo);
    auto comp = core::createAlgorithm(comp_algo);
    pipeline_ = std::make_unique<processor::Pipeline>(std::move(preproc),
                                                      std::move(comp));

    file_open_ = true;
}

auto PackWriter::pushFileData(std::span<const uint8_t> data) -> void {
    if (!file_open_) return;

    entry_header_.original_size += data.size();
    pipeline_->push(data);
    drainOutput();
}

auto PackWriter::pullOutput(void) -> std::span<const uint8_t> const {
    return {output_buffer_.data() + output_pos_,
            output_buffer_.size() - output_pos_};
}

auto PackWriter::consumeOutput(size_t n) -> void {
    n = std::min(n, output_buffer_.size() - output_pos_);
    output_pos_ += n;
    if (output_pos_ == output_buffer_.size()) {
        output_buffer_.clear();
        output_pos_ = 0;
    }
}

auto PackWriter::endFile(void) -> void {
    if (!file_open_) return;

    pipeline_->finish();
    drainOutput();
    pipeline_.reset();

    auto* dest = output_buffer_.data() + header_offset_;
    std::memcpy(dest, &current_compressed_size_, sizeof(uint64_t));

    file_open_ = false;
}

auto PackWriter::finish(void) -> void {
    if (finished_) return;
    if (file_open_) closeCurrentFile();
    finished_ = true;
}

auto PackWriter::drainOutput(void) -> void {
    while (true) {
        auto out = pipeline_->pull();
        if (out.empty()) break;
        output_buffer_.insert(output_buffer_.end(), out.begin(), out.end());
        current_compressed_size_ += out.size();
        pipeline_->consume(out.size());
    }
}

auto PackWriter::closeCurrentFile(void) -> void {
    if (pipeline_) {
        pipeline_->finish();
        pipeline_.reset();
    }
    file_open_ = false;
}

}  // namespace compressor::archiver
