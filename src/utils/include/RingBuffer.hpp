#pragma once

#include <cstddef>
#include <cstdint>

namespace compressor::utils {

class RingBuffer {
public:
    RingBuffer() = default;

    explicit RingBuffer(size_t capacity)
        : data_(capacity), head_(0), size_(0) {}

    void push(uint8_t byte) {
        if (size_ < data_.size()) {
            data_[(head_ + size_) % data_.size()] = byte;
            size_++;
        } else {
            data_[head_] = byte;
            head_ = (head_ + 1) % data_.size();
        }
    }

    uint8_t operator[](size_t index) const {
        return data_[(head_ + index) % data_.size()];
    }

    size_t size() const { return size_; }
    size_t capacity() const { return data_.size(); }
    bool empty() const { return size_ == 0; }

    void clear() {
        head_ = 0;
        size_ = 0;
    }

private:
    std::vector<uint8_t> data_;
    size_t head_{0};
    size_t size_{0};
};

}  // namespace compressor::utils