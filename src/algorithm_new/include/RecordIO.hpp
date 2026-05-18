#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "BitProcessor.hpp"
#include "LZencoding.hpp"
#include "Models.hpp"

namespace compressor::algorithm::record_io {

// Temp file record sizes (§1.19); bit-packed field widths sum to these byte counts.
constexpr size_t kDPNodeRecordBytes = 29u;
constexpr size_t kTripleRecordBytes = 9u;

inline std::vector<uint8_t> dp_nodes_to_u8(
    const std::vector<models::DPNode>& nodes,
    compressor::utils::_buffer& pending) {
    std::vector<uint8_t> out;
    compressor::utils::BitWriter writer(out, pending);
    for (const auto& d : nodes) {
        writer.writeBits(static_cast<uint64_t>(d.literal_count), 64);
        writer.writeBits(static_cast<uint64_t>(d.match_count), 64);
        writer.writeBits(static_cast<uint64_t>(static_cast<uint32_t>(d.pre_pos)), 32);
        writer.writeBits(static_cast<uint64_t>(d.triple.offset), 32);
        writer.writeBits(static_cast<uint64_t>(d.triple.length), 32);
        writer.writeBits(static_cast<uint64_t>(d.triple.literal), 8);
    }
    pending = writer.getBuf();
    return out;
}

inline std::vector<models::DPNode> parse_dp_nodes_bytes(
    const std::vector<uint8_t>& bytes,
    compressor::utils::_buffer& pending) {
    std::vector<models::DPNode> nodes;
    if (bytes.empty()) {
        return nodes;
    }
    compressor::utils::BitReader reader(bytes, pending);
    uint64_t val = 0;
    const size_t n_records = bytes.size() / kDPNodeRecordBytes;
    nodes.reserve(n_records);
    for (size_t r = 0; r < n_records; ++r) {
        reader.readBits(val, 64);
        const size_t literal_count = static_cast<size_t>(val);
        reader.readBits(val, 64);
        const size_t match_count = static_cast<size_t>(val);
        reader.readBits(val, 32);
        const int pre_pos = static_cast<int>(val);
        reader.readBits(val, 32);
        const uint32_t offset = static_cast<uint32_t>(val);
        reader.readBits(val, 32);
        const uint32_t length = static_cast<uint32_t>(val);
        reader.readBits(val, 8);
        const uint8_t literal = static_cast<uint8_t>(val);
        nodes.emplace_back(literal_count, match_count, pre_pos,
                           Triple(offset, length, literal));
    }
    pending = reader.getBuf();
    return nodes;
}

inline std::pair<std::vector<models::DPNode>, std::vector<uint8_t>> u8_to_dp_nodes(
    const std::vector<uint8_t>& chunk,
    std::vector<uint8_t>& byte_carry,
    compressor::utils::_buffer& pending) {
    std::vector<uint8_t> stream;
    stream.reserve(byte_carry.size() + chunk.size());
    stream.insert(stream.end(), byte_carry.begin(), byte_carry.end());
    stream.insert(stream.end(), chunk.begin(), chunk.end());
    byte_carry.clear();

    const size_t usable = (stream.size() / kDPNodeRecordBytes) * kDPNodeRecordBytes;
    if (usable == 0) {
        byte_carry = std::move(stream);
        return {{}, byte_carry};
    }

    std::vector<uint8_t> prefix(stream.begin(),
                                stream.begin() + static_cast<std::ptrdiff_t>(usable));
    auto nodes = parse_dp_nodes_bytes(prefix, pending);

    if (usable < stream.size()) {
        byte_carry.assign(stream.begin() + static_cast<std::ptrdiff_t>(usable),
                          stream.end());
    }
    return {std::move(nodes), byte_carry};
}

inline std::vector<uint8_t> triples_to_u8(
    const std::vector<Triple>& triples,
    compressor::utils::_buffer& pending) {
    return triple2u8(triples, pending);
}

/// 单条 Triple 定长记录（`kTripleRecordBytes`），与 `u8_to_triples` 对齐。
inline std::vector<uint8_t> triple_to_record_bytes(
    const Triple& triple,
    compressor::utils::_buffer& pending) {
    std::vector<uint8_t> out;
    compressor::utils::BitWriter writer(out, pending);
    writer.writeBits(static_cast<uint64_t>(triple.offset), 32);
    writer.writeBits(static_cast<uint64_t>(triple.length), 32);
    writer.writeBits(static_cast<uint64_t>(triple.literal), 8);
    pending = writer.getBuf();
    return out;
}

inline std::pair<std::vector<Triple>, std::vector<uint8_t>> u8_to_triples(
    const std::vector<uint8_t>& chunk,
    std::vector<uint8_t>& byte_carry,
    compressor::utils::_buffer& pending) {
    std::vector<uint8_t> stream;
    stream.reserve(byte_carry.size() + chunk.size());
    stream.insert(stream.end(), byte_carry.begin(), byte_carry.end());
    stream.insert(stream.end(), chunk.begin(), chunk.end());
    byte_carry.clear();

    const size_t usable = (stream.size() / kTripleRecordBytes) * kTripleRecordBytes;
    if (usable == 0) {
        byte_carry = std::move(stream);
        return {{}, byte_carry};
    }

    std::vector<uint8_t> prefix(stream.begin(),
                                stream.begin() + static_cast<std::ptrdiff_t>(usable));
    auto triples = u82triple(prefix, pending);

    if (usable < stream.size()) {
        byte_carry.assign(stream.begin() + static_cast<std::ptrdiff_t>(usable),
                          stream.end());
    }
    return {std::move(triples), byte_carry};
}

}  // namespace compressor::algorithm::record_io
