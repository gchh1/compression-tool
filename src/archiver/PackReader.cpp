
#include "PackReader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "StreamProcessor.hpp"

namespace compressor::archiver {
/**
 * @brief Construct a new Pack Reader:: Pack Reader object
 *
 * @param reader
 */
PackReader::PackReader(std::unique_ptr<IDataReader> reader)
    : reader_(std::move(reader)) {
    buildIndex();
}

/**
 * @brief In this function, we only read the `EntryHeader` of each file, and
 *        puch_back to `entries`. Here `EntryHeader.compressed_size` is used as
 *        `offset` to locate the next `EntryHeader`
 */
auto PackReader::buildIndex(void) -> void {
    uint64_t total_size = reader_->size();
    uint64_t pos = 0;
    std::vector<uint8_t> buffer(ENTRY_CHUNK_SIZE);

    while (pos < total_size) {
        /* 1. Seice data to `buffer` from source */
        size_t actual = reader_->read(pos, buffer);
        if (actual == 0) {
            break;
        }

        /* 2. Deserialize to pair<`EntryHeader`, size of it> */
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

/**
 * @brief In this
 *
 * @param index
 * @return std::unique_ptr<processor::StreamProcessor>
 */
auto PackReader::extractStream(size_t index) const
    -> std::unique_ptr<processor::StreamProcessor> {
    if (index >= entries_.size()) {
        return nullptr;
    }

    const auto& meta = entries_[index];
    auto postproc_id =
        core::createAlgorithm(core::getPostpressorID(meta.preproc_algo_id));
    auto decomp_id =
        core::createAlgorithm(core::getDecompressorID(meta.comp_algo_id));

    auto postproc =
        std::make_unique<processor::StreamProcessor>(std::move(postproc_id));
    auto decomp =
        std::make_unique<processor::StreamProcessor>(std::move(decomp_id));

    std::vector<uint8_t> buffer(INPUT_BUFFER_SIZE);
    uint64_t comp_remain = meta.compressed_size;
    uint64_t read_offset = meta.data_offset;

    /* 1. Decompress */
    while (comp_remain > 0) {
        size_t to_read = static_cast<size_t>(
            std::min(static_cast<uint64_t>(INPUT_BUFFER_SIZE), comp_remain));
        size_t actual = reader_->read(
            read_offset, std::span<uint8_t>(buffer.data(), to_read));
        if (actual == 0) {
            return nullptr;
        }

        bool is_last = (comp_remain == actual);
        decomp->push(buffer, is_last);

        read_offset += actual;
        comp_remain -= actual;
    }

    /* 2. Postprocess */

    return decomp;
}

}  // namespace compressor::archiver