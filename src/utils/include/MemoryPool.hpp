#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace compressor::memory {

/**
 * @brief Fixed-size pool of pre-allocated byte buffers.
 *
 *        Must be created via std::make_shared<MemoryPool>() - the custom
 *        deleter on acquired chunks captures a weak_ptr for safe retrn.
 *
 */
class MemoryPool : public std::enable_shared_from_this<MemoryPool> {
   public:
    /** @brief Initialize the memory pool via `number` of chunks and `size` of
     *         each chunk */
    MemoryPool(size_t num_chunks, size_t chunk_size) : chunk_size_(chunk_size) {
        chunks_.reserve(num_chunks);
        for (size_t i = 0; i < num_chunks; ++i) {
            chunks_.emplace_back(chunk_size);
            free_list_.push_back(i);
        }
    }

    /**
     * @brief Thread-safe RAII method to acquire a free chunk in the memory
     *        pool. Once acquiring, the owner doesn't have to give the chunk
     *        back, since the `deleter` will recycle the chunk
     *
     * @return std::shared_ptr<std::vector<uint8_t>>
     */
    auto acquire(void) -> std::shared_ptr<std::vector<uint8_t>> {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !free_list_.empty(); });

        size_t idx = free_list_.back();
        free_list_.pop_back();

        auto& chunk_data = chunks_[idx];
        chunk_data.clear();

        std::weak_ptr<MemoryPool> weak_self = shared_from_this();
        return {&chunk_data, [weak_self, idx](std::vector<uint8_t>*) {
                    if (auto self = weak_self.lock()) {
                        std::lock_guard lk(self->mutex_);
                        self->free_list_.push_back(idx);
                        self->cv_.notify_one();
                    }
                }};
    }

    /**
     * @brief
     *
     * @return size_t
     */
    auto chunkSize() const -> size_t { return chunk_size_; }

   private:
    /** @brief Size of each chunk */
    size_t chunk_size_;

    /** @brief Core resources, `chunks`, that memory pool maintain */
    std::vector<std::vector<uint8_t>> chunks_;

    /** @brief Store the index of available chunk in the `chunks_` */
    std::vector<size_t> free_list_;

    // Members for thread security
    /** @brief Mutual exclusion for memory pool */
    std::mutex mutex_;

    /** @brief Condition variable for memory pool */
    std::condition_variable cv_;
};

}  // namespace compressor::memory
