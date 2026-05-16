#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "TempFile.hpp"

namespace compressor::algorithm {

/// Fixed-width packed DP link record: LSB-first bit order, matching ``BitWriter`` / ``BitReader``.
/// Literal: flag 0 + 8-bit byte + padding. Match: flag 1 + offset_bits + length_bits + padding.
struct PackedDpLinkSpec {
    uint8_t offset_bits{1};
    uint8_t length_bits{1};
    uint8_t record_bits{10};

    /// non-flag 匹配数据位宽 = length_bits + offset_bits
    uint8_t match_data_bits() const { return static_cast<uint8_t>(length_bits + offset_bits); }

    static auto calcBitWidth(size_t v) -> uint8_t {
        int bits = 0;
        if (v == 0) return 1;
        while (v > 0) {
            ++bits;
            v >>= 1;
        }
        return static_cast<uint8_t>(bits == 0 ? 1 : bits);
    }

    static auto fromWindow(size_t search_size, size_t lookahead_size) -> PackedDpLinkSpec {
        uint8_t ob = calcBitWidth(search_size);
        uint8_t lb = calcBitWidth(lookahead_size);
        uint8_t w = static_cast<uint8_t>(1 + std::max(8, static_cast<int>(ob) + static_cast<int>(lb)));
        return {ob, lb, w};
    }

    static auto fromBitWidths(uint8_t ob, uint8_t lb) -> PackedDpLinkSpec {
        uint8_t w = static_cast<uint8_t>(1 + std::max(8, static_cast<int>(ob) + static_cast<int>(lb)));
        return {ob, lb, w};
    }

    void writeRecord(compressor::utils::BitWriter& bw, uint16_t length, uint16_t raw_offset) const {
        if (length == 0) {
            bw.writeBits(0, 1);
            bw.writeBits(raw_offset & 0xFF, 8);
            uint8_t pad = static_cast<uint8_t>(record_bits - 1 - 8);
            if (pad > 0) bw.writeBits(0, pad);
        } else {
            bw.writeBits(1, 1);
            bw.writeBits(length, length_bits);
            bw.writeBits(raw_offset, offset_bits);
            uint8_t used = static_cast<uint8_t>(1 + match_data_bits());
            uint8_t pad = static_cast<uint8_t>(record_bits - used);
            if (pad > 0) bw.writeBits(0, pad);
        }
    }

    void readRecord(compressor::utils::BitReader& br, uint16_t& length, uint16_t& raw_offset) const {
        uint64_t tag = br.readBits(1);
        if (tag == 0) {
            raw_offset = static_cast<uint16_t>(br.readBits(8));
            length = 0;
            uint8_t pad = static_cast<uint8_t>(record_bits - 1 - 8);
            if (pad > 0) br.readBits(pad);
        } else {
            length = static_cast<uint16_t>(br.readBits(length_bits));
            raw_offset = static_cast<uint16_t>(br.readBits(offset_bits));
            uint8_t used = static_cast<uint8_t>(1 + match_data_bits());
            uint8_t pad = static_cast<uint8_t>(record_bits - used);
            if (pad > 0) br.readBits(pad);
        }
    }
};

/// Append packed bits to a ``TempFile`` using the same layout as ``BitWriter`` (chunked).
class TempFileBitAppender {
    static constexpr size_t kBitBufferBytes = 2 * 1024 * 1024;

public:
    explicit TempFileBitAppender(TempFile* tf = nullptr) : tf_(tf) {
        buf_.assign(kBitBufferBytes, 0);
        bw_.emplace(std::span<uint8_t>(buf_.data(), buf_.size()));
    }

    void rebind(TempFile* tf) {
        if (bw_.has_value()) flush();
        tf_ = tf;
        buf_.assign(kBitBufferBytes, 0);
        bw_.emplace(std::span<uint8_t>(buf_.data(), buf_.size()));
    }

    void writeBits(uint64_t value, uint8_t count) {
        if (count == 0) return;
        if (!bw_->ensureSpace(count)) flushChunk();
        bw_->writeBits(value, count);
    }

    void writePackedLink(const PackedDpLinkSpec& spec, uint16_t length, uint16_t raw_offset) {
        if (!bw_->ensureSpace(spec.record_bits)) flushChunk();
        spec.writeRecord(*bw_, length, raw_offset);
    }

    void flush() {
        if (!bw_.has_value()) return;
        size_t n = bw_->flush();
        if (n > 0 && tf_) tf_->write(buf_.data(), n);
        bw_->changeSource(std::span<uint8_t>(buf_.data(), buf_.size()));
        if (tf_) tf_->flush();
    }

private:
    void flushChunk() {
        if (!bw_.has_value()) return;
        const size_t n = bw_->drainFullBytes();
        if (n > 0 && tf_) tf_->write(buf_.data(), n);
        bw_->changeSource(std::span<uint8_t>(buf_.data(), buf_.size()));
    }

    TempFile* tf_{nullptr};
    std::vector<uint8_t> buf_;
    std::optional<compressor::utils::BitWriter> bw_;
};

/// Random read of packed links by record index (backward walk uses indices in descending order).
class PackedDpLinkBackwardWindow {
public:
    PackedDpLinkBackwardWindow(TempFile& tf, PackedDpLinkSpec spec)
        : tf_(tf), spec_(spec) {}

    void readPair(uint64_t idx, uint16_t& length, uint16_t& raw_offset) {
        const size_t record_bytes =
            static_cast<size_t>((spec_.record_bits + 7) / 8) + 1;

        auto load_window = [&](uint64_t center_idx) {
            const uint64_t load_end = center_idx + 1;
            const uint64_t load_start =
                (load_end > kMaxRecords) ? (load_end - kMaxRecords) : 0;
            const uint64_t byte0 = (load_start * spec_.record_bits) / 8;
            const uint64_t bit1 = load_end * spec_.record_bits;
            const uint64_t byte1 = (bit1 + 7) / 8 + 1;
            buf_.resize(static_cast<size_t>(byte1 - byte0));
            tf_.readAt(byte0, buf_.data(), buf_.size());
            buf_byte0_ = byte0;
            buf_first_idx_ = load_start;
            buf_num_records_ = static_cast<size_t>(load_end - load_start);
        };

        if (idx < buf_first_idx_ || idx >= buf_first_idx_ + buf_num_records_ ||
            buf_num_records_ == 0) {
            load_window(idx);
        }

        const uint64_t global_bit = idx * spec_.record_bits;
        const uint64_t local_bit = global_bit - buf_byte0_ * 8;
        const size_t byte_in_buf = static_cast<size_t>(local_bit / 8);
        const auto skip = static_cast<uint8_t>(local_bit % 8);

        if (byte_in_buf + record_bytes > buf_.size()) {
            load_window(idx);
            const uint64_t reloaded_global_bit = idx * spec_.record_bits;
            const uint64_t reloaded_local_bit = reloaded_global_bit - buf_byte0_ * 8;
            const size_t reloaded_byte_in_buf =
                static_cast<size_t>(reloaded_local_bit / 8);
            const auto reloaded_skip =
                static_cast<uint8_t>(reloaded_local_bit % 8);

            if (reloaded_byte_in_buf + record_bytes > buf_.size()) {
                length = 0;
                raw_offset = 0;
                return;
            }

            compressor::utils::BitReader br(std::span<const uint8_t>(
                buf_.data() + reloaded_byte_in_buf, buf_.size() - reloaded_byte_in_buf));
            if (reloaded_skip > 0) br.readBits(reloaded_skip);
            spec_.readRecord(br, length, raw_offset);
            return;
        }

        compressor::utils::BitReader br(
            std::span<const uint8_t>(buf_.data() + byte_in_buf, buf_.size() - byte_in_buf));
        if (skip > 0) br.readBits(skip);
        spec_.readRecord(br, length, raw_offset);
    }

private:
    static constexpr uint64_t kMaxRecords = 1ULL << 20;

    TempFile& tf_;
    PackedDpLinkSpec spec_;
    std::vector<uint8_t> buf_;
    uint64_t buf_byte0_{0};
    uint64_t buf_first_idx_{0};
    size_t buf_num_records_{0};
};

}  // namespace compressor::algorithm
