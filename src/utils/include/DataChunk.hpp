#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace compressor::memory {

/// Shared-ownership, read-only byte buffer view.
/// Zero-copy slice: slice() shares the same control block.
class DataChunk {
   public:
    DataChunk() = default;

    /// Adopt a shared_ptr (e.g. from MemoryPool).
    /// @param size  logical size, may be ≤ owner->size()
    static auto adopt(std::shared_ptr<const std::vector<uint8_t>> owner,
                      size_t size) -> DataChunk {
        DataChunk c;
        c.owner_ = std::move(owner);
        c.data_ = c.owner_->data();
        c.size_ = size < c.owner_->size() ? size : c.owner_->size();
        return c;
    }

    /// Sub-range sharing the same ownership.  Zero copy.
    auto slice(size_t offset, size_t count) const -> DataChunk {
        if (offset >= size_) return {};
        DataChunk c;
        c.owner_ = owner_;
        c.data_ = data_ + offset;
        c.size_ = (count < size_ - offset) ? count : (size_ - offset);
        return c;
    }

    auto view() const -> std::span<const uint8_t> { return {data_, size_}; }
    auto size() const -> size_t { return size_; }
    auto empty() const -> bool { return size_ == 0; }

    /// Shared ownership handle for zero-copy async write (e.g. BackgroundWriter).
    auto owner() const -> std::shared_ptr<const std::vector<uint8_t>> { return owner_; }

   private:
    std::shared_ptr<const std::vector<uint8_t>> owner_;
    const uint8_t* data_{nullptr};
    size_t size_{0};
};

}  // namespace compressor::memory
