#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"

namespace compressor::archiver {
using core::AlgorithmID;

/**
 * @brief [`filepath`] [algorithmID_chain] [original_size] [compressed_size]
 *
 */
struct EntryHeader {
    std::string filepath;
    std::vector<AlgorithmID> algo_chain;
    uint64_t original_size;
    uint64_t compressed_size;
    uint64_t data_offset;  // computed at parse time, not serialized

    /** @brief Serialze the EntryHeader to byte stream */
    auto serialize(void) -> std::vector<uint8_t> const {
        std::vector<uint8_t> buffer;

        auto push_u8 = [&](uint8_t v) { buffer.push_back(v & 0xFF); };
        auto push_u16 = [&](uint16_t v) {
            buffer.push_back(v & 0xFF);
            buffer.push_back((v >> 8) & 0xFF);
        };
        auto push_u64 = [&](uint64_t v) {
            for (int i = 0; i < 8; ++i) buffer.push_back((v >> (i * 8)) & 0xFF);
        };

        push_u16(static_cast<uint16_t>(filepath.size()));
        for (char c : filepath) push_u8(static_cast<uint8_t>(c));

        push_u8(static_cast<uint8_t>(algo_chain.size()));
        for (auto id : algo_chain) push_u8(static_cast<uint8_t>(id));

        push_u64(original_size);
        push_u64(compressed_size);
        return buffer;
    }

    /** @brief Deserialize byte stream to EntryHeader */
    static auto deserialize(std::span<const uint8_t> data)
        -> std::optional<std::pair<EntryHeader, size_t>> {
        EntryHeader meta;
        size_t pos = 0;

        if (pos + 2 > data.size()) return std::nullopt;
        uint16_t name_len = data[pos] | (data[pos + 1] << 8);
        pos += 2;

        if (pos + name_len > data.size()) return std::nullopt;
        meta.filepath.assign(reinterpret_cast<const char*>(data.data() + pos),
                             name_len);
        pos += name_len;

        if (pos + 1 > data.size()) return std::nullopt;
        uint8_t chain_len = data[pos++];

        if (pos + chain_len > data.size()) return std::nullopt;
        for (uint8_t i = 0; i < chain_len; ++i)
            meta.algo_chain.push_back(static_cast<AlgorithmID>(data[pos++]));

        if (pos + 16 > data.size()) return std::nullopt;
        meta.original_size =
            *reinterpret_cast<const uint64_t*>(data.data() + pos);
        pos += 8;
        meta.compressed_size =
            *reinterpret_cast<const uint64_t*>(data.data() + pos);
        pos += 8;
        return std::make_pair(std::move(meta), pos);
    }
};

}  // namespace compressor::archiver
