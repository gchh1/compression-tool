#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

class MemoryPool {
public:
    explicit MemoryPool(size_t num_slots = 16, size_t slot_size = 65536)
        : slot_size_(slot_size) {
        slots_.reserve(num_slots);
        for (size_t i = 0; i < num_slots; ++i) {
            slots_.emplace_back(slot_size_);
            free_list_.push_back(static_cast<int>(i));
        }
    }

    size_t slot_size() const { return slot_size_; }
    size_t num_slots() const { return slots_.size(); }
    size_t free_count() const { return free_list_.size(); }

    class SlotGuard {
    public:
        SlotGuard() : pool_(nullptr), idx_(-1) {}
        SlotGuard(MemoryPool* pool, int idx)
            : pool_(pool), idx_(idx) {}
        ~SlotGuard() { release(); }

        SlotGuard(SlotGuard&& other) noexcept
            : pool_(other.pool_), idx_(other.idx_) {
            other.pool_ = nullptr;
            other.idx_ = -1;
        }
        SlotGuard& operator=(SlotGuard&& other) noexcept {
            if (this != &other) {
                release();
                pool_ = other.pool_;
                idx_ = other.idx_;
                other.pool_ = nullptr;
                other.idx_ = -1;
            }
            return *this;
        }
        SlotGuard(const SlotGuard&) = delete;
        SlotGuard& operator=(const SlotGuard&) = delete;

        explicit operator bool() const { return pool_ != nullptr && idx_ >= 0; }

        std::vector<uint8_t>& data() { return pool_->slots_[idx_]; }
        const std::vector<uint8_t>& data() const { return pool_->slots_[idx_]; }

        void release() {
            if (pool_ && idx_ >= 0) {
                pool_->free_list_.push_back(idx_);
                pool_ = nullptr;
                idx_ = -1;
            }
        }

    private:
        MemoryPool* pool_;
        int idx_;
    };

    SlotGuard acquire() {
        if (free_list_.empty()) return {};
        int idx = free_list_.back();
        free_list_.pop_back();
        auto& slot = slots_[idx];
        slot.clear();
        return SlotGuard(this, idx);
    }

private:
    size_t slot_size_;
    std::vector<std::vector<uint8_t>> slots_;
    std::vector<int> free_list_;
};

}  // namespace compressor::algorithm
