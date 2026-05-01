#include "PackReader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "Pipeline.hpp"

namespace compressor::archiver {

PackReader::PackReader(std::unique_ptr<IDataReader> reader)
    : reader_(std::move(reader)) {
    buildIndex();
}

auto PackReader::getEntries(void) const -> const std::vector<EntryHeader>& {
    return entries_;
}

auto PackReader::buildIndex(void) -> void {
    uint64_t total_size = reader_->size();
    uint64_t pos = 0;
    std::vector<uint8_t> buffer(ENTRY_CHUNK_SIZE);

    while (pos < total_size) {
        size_t actual = reader_->read(pos, buffer);
        if (actual == 0) {
            break;
        }

        auto result = EntryHeader::deserialize(buffer);
        if (!result) {
            return;
        }

        auto& [meta, consumed] = *result;
        uint64_t data_offset = pos + consumed;
        if (data_offset + meta.compressed_size > total_size) {
            return;
        }

        meta.data_offset = data_offset;
        entries_.push_back(std::move(meta));

        pos = data_offset + entries_.back().compressed_size;
    }
}

auto PackReader::extractStream(size_t index) const
    -> std::unique_ptr<processor::Pipeline> {
    if (index >= entries_.size()) {
        return nullptr;
    }

    const auto& meta = entries_[index];
    auto decomp_algo =
        core::createAlgorithm(core::getDecompressorID(meta.comp_algo_id));
    auto postproc_algo =
        core::createAlgorithm(core::getPostpressorID(meta.preproc_algo_id));

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    if (auto a = std::move(decomp_algo)) algos.push_back(std::move(a));
    if (auto a = std::move(postproc_algo)) algos.push_back(std::move(a));
    auto pipeline = std::make_unique<processor::Pipeline>(std::move(algos));

    std::vector<uint8_t> buffer(INPUT_BUFFER_SIZE);
    uint64_t comp_remain = meta.compressed_size;
    uint64_t read_offset = meta.data_offset;

    while (comp_remain > 0) {
        size_t to_read = static_cast<size_t>(
            std::min(static_cast<uint64_t>(INPUT_BUFFER_SIZE), comp_remain));
        size_t actual = reader_->read(
            read_offset, std::span<uint8_t>(buffer.data(), to_read));
        if (actual == 0) {
            return nullptr;
        }

        bool is_last = (comp_remain == actual);
        pipeline->push(buffer, is_last);

        read_offset += actual;
        comp_remain -= actual;
    }

    pipeline->finish();
    return pipeline;
}

}  // namespace compressor::archiver
