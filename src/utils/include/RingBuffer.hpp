/**
 * @file RingBuffer.hpp
 * @author yhc
 * @brief
 * @version 0.1
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
class RingBuffer {
   public:
    /** @brief Construct the `ring buffer` with given capacity */
    explicit RingBuffer(size_t capacity)
        : ring_(capacity), capacity_(capacity) {}

    /** @brief Append data to the `ring buffer` */
    auto append(std::span<const uint8_t> data) -> void {
        if (data.size() == 0) return;

        ensureCapacity(size_ + data.size());

        size_t tail = (head_ + size_) % capacity_;
        size_t first_part = std::min(data.size(), capacity_ - tail);

        std::memcpy(ring_.data() + tail, data.data(), first_part);
        if (first_part < data.size()) {
            std::memcpy(ring_.data(), data.data() + first_part,
                        data.size() - first_part);
        }
        size_ += data.size();
    }

    /** @brief Consume `n` byte from head */
    auto consume(size_t n) -> void {
        if (n > size_) n = size_;
        head_ = (head_ + n) % capacity_;
        size_ -= n;
    }

    /** @brief Return the available span of the data in the `ring buffer` */
    auto readSpan(void) -> std::span<const uint8_t> {
        if (size_ == 0) return {};

        if (head_ + size_ > capacity_) {
            compact();
        }
        return {ring_.data() + head_, size_};
    }

    /** @brief Return the size */
    auto size(void) const -> size_t { return size_; }

    auto reset(void) -> void;

   private:
    std::vector<uint8_t> ring_;

    size_t head_{0};

    size_t size_{0};

    size_t capacity_;

    /** @brief When append new data to the `ring buffer`, ensure we have enough
     *        capacity, otherwise, enlarge the `ring buffer`*/
    auto ensureCapacity(size_t total_size) -> void {
        if (total_size <= capacity_) return;

        size_t new_cap = capacity_ * 2;
        while (new_cap < total_size) new_cap *= 2;

        std::vector<uint8_t> new_buf(new_cap);
        size_t first_part = std::min(size_, capacity_ - head_);
        std::memcpy(new_buf.data(), ring_.data() + head_, first_part);
        if (first_part < size_) {
            std::memcpy(new_buf.data() + first_part, ring_.data(),
                        size_ - first_part);
        }

        ring_ = std::move(new_buf);
        capacity_ = new_cap;
        head_ = 0;
    }

    auto compact(void) -> void;
};