#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "BitReader.hpp"
#include "TempFile.hpp"

namespace compressor::algorithm {

/// 支持按位随机访问的文件读取器
///
/// 封装 TempFile，提供：
///   - size_in_bits()    → 文件总位宽
///   - read_bits_at()    → 从任意位偏移读取 N 位
///   - read_chunk()      → 读取整块原始字节（供 VirtualBuffer 使用）
///
/// Phase 2 回溯阶段：从 Temp A 尾部逆向分块读取 token 时使用。
class FileBitReader {
public:
    FileBitReader(TempFile& file, size_t file_size_bits)
        : file_(file), file_size_bits_(file_size_bits) {}

    /// 文件总大小（位）
    size_t size_in_bits() const { return file_size_bits_; }

    /// 从指定位偏移读取 N 位
    uint64_t read_bits_at(size_t bit_offset, size_t num_bits) {
        size_t byte_start = bit_offset / 8;
        uint8_t bit_skip = static_cast<uint8_t>(bit_offset % 8);
        size_t byte_count = (static_cast<size_t>(bit_skip) + num_bits + 7) / 8;

        buf_.resize(byte_count);
        file_.readAt(byte_start, buf_.data(), byte_count);

        utils::BitReader br(std::span<const uint8_t>(buf_));
        if (bit_skip > 0) br.readBits(bit_skip);
        return br.readBits(static_cast<uint8_t>(num_bits));
    }

    /// 读取整块原始字节（用于 VirtualBuffer 拼接）
    std::vector<uint8_t> read_chunk(size_t bit_offset, size_t num_bits) {
        size_t byte_start = bit_offset / 8;
        uint8_t bit_skip = static_cast<uint8_t>(bit_offset % 8);
        size_t byte_count = (static_cast<size_t>(bit_skip) + num_bits + 7) / 8;

        std::vector<uint8_t> raw(byte_count);
        file_.readAt(byte_start, raw.data(), byte_count);
        return raw;
    }

private:
    TempFile& file_;
    size_t file_size_bits_;
    std::vector<uint8_t> buf_;
};

} // namespace compressor::algorithm