/**
 * @file IDataReader.hpp
 * @author yhc
 * @brief
 * @version 0.1
 * @date 2026-04-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace compressor::archiver {
class IDataReader {
   public:
    virtual ~IDataReader() = default;

    /** @brief Suppose we have source file, cut part from `source* + offset` to
     * buffer and return the actual size we read. */
    virtual auto read(uint64_t offset, std::span<uint8_t> buffer) -> size_t = 0;

    /** @brief Return the size of the `source data` */
    virtual auto size(void) const -> uint64_t = 0;
};
}  // namespace compressor::archiver