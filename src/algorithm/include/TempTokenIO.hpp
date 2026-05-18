#pragma once

#include <cstdint>

#include "DPFlateBin64kDebug.hpp"
#include "TempFile.hpp"

namespace compressor::algorithm {

/// Fixed 4-byte temp token / DP link record (byte-aligned, indexable by position).
#pragma pack(push, 1)
struct TempTokenRecord {
    uint16_t length{0};
    uint16_t offset{0};
};
#pragma pack(pop)

inline constexpr size_t kTempTokenRecordBytes = sizeof(TempTokenRecord);

/// Random-access fixed records on a ``TempFile`` (COLLECT temp A, BACKTRACK temp B).
class TempTokenStore {
public:
    explicit TempTokenStore(TempFile* tf = nullptr) : tf_(tf) {}

    void rebind(TempFile* tf) {
        tf_ = tf;
        append_seq_ = 0;
    }

    void writeAt(uint32_t index, uint16_t length, uint16_t offset) {
        if (!tf_) {
            return;
        }
        const TempTokenRecord rec{length, offset};
        const uint64_t byte_off = static_cast<uint64_t>(index) * kTempTokenRecordBytes;
        if ((index & 0x3FFFu) == 0 || index == 65536u) {
            DPFLATE_BIN64K_LOG("TEMP_A_WRITE", "index=%u byte_off=%llu len=%u off=%u", index,
                                static_cast<unsigned long long>(byte_off), length, offset);
            const auto* raw = reinterpret_cast<const uint8_t*>(&rec);
            DPFlateBin64kDebug::log_hex("TEMP_A_BYTES", "record", raw, sizeof(rec));
        }
        tf_->writeAt(byte_off, &rec, sizeof(rec));
    }

    void readAt(uint32_t index, uint16_t& length, uint16_t& offset) {
        length = 0;
        offset = 0;
        if (!tf_) {
            return;
        }
        TempTokenRecord rec{};
        const uint64_t byte_off = static_cast<uint64_t>(index) * kTempTokenRecordBytes;
        if ((index & 0x3FFFu) == 0) {
            DPFLATE_BIN64K_LOG("TEMP_READ", "index=%u byte_off=%llu", index,
                                static_cast<unsigned long long>(byte_off));
        }
        tf_->readAt(byte_off, &rec, sizeof(rec));
        length = rec.length;
        offset = rec.offset;
    }

    void append(uint16_t length, uint16_t offset) {
        if (!tf_) {
            return;
        }
        const TempTokenRecord rec{length, offset};
        if (DPFlateBin64kDebug::active() && (append_seq_ & 0x3FFFu) == 0) {
            DPFLATE_BIN64K_LOG("TEMP_B_APPEND", "seq=%llu len=%u off=%u",
                                static_cast<unsigned long long>(append_seq_), length, offset);
        }
        ++append_seq_;
        tf_->write(&rec, sizeof(rec));
    }

    void readBySeqIndex(uint64_t seq_index, uint16_t& length, uint16_t& offset) {
        if (DPFlateBin64kDebug::active() && (seq_index & 0x3FFFu) == 0) {
            DPFLATE_BIN64K_LOG("TEMP_B_READ_SEQ", "seq_index=%llu",
                                static_cast<unsigned long long>(seq_index));
        }
        readAt(static_cast<uint32_t>(seq_index), length, offset);
    }

    static uint64_t recordCountFromBytes(uint64_t file_bytes) {
        return file_bytes / kTempTokenRecordBytes;
    }

private:
    TempFile* tf_{nullptr};
    uint64_t append_seq_{0};
};

}  // namespace compressor::algorithm
