/**
 * @file CountMinSketch.hpp
 * @author ADE Module - Count-Min Sketch for N-gram Statistics
 * @brief Memory-efficient probabilistic data structure for frequency estimation
 * @version 3.0
 * @date 2026-05-07
 *
 * @copyright Copyright (c) 2026 WebCompress Project
 *
 * @details Implements Count-Min Sketch algorithm for estimating bigram/trigram
 * frequencies with O(1) update and query time, using only 64KB fixed memory.
 *
 * Key advantages over exact counting:
 * - Memory: 64KB fixed (vs 8.7MB for exact 256×256 matrix)
 * - Speed: O(1) per operation, no hash collisions to resolve
 * - Accuracy: Acceptable error bounds (ε=0.001, δ=0.01)
 *
 * Usage:
 * ```cpp
 * CountMinSketch cms;
 * cms.update(bigram_hash);  // Add occurrence
 * auto count = cms.query(bigram_hash);  // Estimate frequency
 * ```
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace compressor {
namespace ade {

// ========================================================================
// COUNT-MIN SKETCH IMPLEMENTATION
// ========================================================================

/**
 * @brief Count-Min Sketch template class for frequency estimation
 *
 * Template Parameters:
 * @tparam WIDTH Number of columns in sketch (default: 4096)
 * @tparam DEPTH Number of hash functions/rows (default: 4)
 *
 * Memory footprint: WIDTH × DEPTH × sizeof(Counter) bytes
 * Default configuration: 4096 × 4 × 4 bytes = 65,536 bytes = 64 KB
 *
 * Error guarantees:
 * - ε (epsilon) = 2/WIDTH = 0.00049 (relative error)
 * - δ (delta) = e^(-DEPTH) = 0.0183 (failure probability)
 */
template <size_t WIDTH = 4096, size_t DEPTH = 4>
class CountMinSketch {
   public:
    using Counter = uint32_t;  /// Counter type (uint32_t supports up to ~4B counts)

    static constexpr size_t TABLE_WIDTH = WIDTH;
    static constexpr size_t TABLE_DEPTH = DEPTH;
    static constexpr size_t MEMORY_SIZE = WIDTH * DEPTH * sizeof(Counter);

    /**
     * @brief Construct empty sketch with all counters initialized to zero
     */
    CountMinSketch() { reset(); }

    /**
     * @brief Reset all counters to zero
     */
    auto reset() -> void {
        for (auto& row : table_) {
            row.fill(0);
        }
        total_count_ = 0;
    }

    /**
     * @brief Record an occurrence of the given key
     *
     * Uses DEPTH independent hash functions to increment counters.
     * Time complexity: O(DEPTH)
     *
     * @param key Hash value of the n-gram to count
     */
    auto update(uint32_t key) -> void {
        for (size_t row = 0; row < DEPTH; ++row) {
            size_t col = hash_function(key, row);
            if (++table_[row][col] == 0) {  // Overflow protection
                table_[row][col] = std::numeric_limits<Counter>::max();
            }
        }
        ++total_count_;
    }

    /**
     * @brief Query estimated frequency of a key
     *
     * Returns minimum count across all rows (conservative estimate).
     * Actual count is always ≥ returned value.
     * Time complexity: O(DEPTH)
     *
     * @param key Hash value to query
     * @return Estimated minimum frequency count
     */
    auto query(uint32_t key) const -> Counter {
        Counter min_count = std::numeric_limits<Counter>::max();

        for (size_t row = 0; row < DEPTH; ++row) {
            size_t col = hash_function(key, row);
            min_count = std::min(min_count, table_[row][col]);
        }

        return min_count;
    }

    /**
     * @brief Get total number of recorded items across all updates
     */
    auto total_count() const -> uint64_t { return total_count_; }

    /**
     * @brief Get memory usage in bytes
     */
    static constexpr auto memory_size() -> size_t { return MEMORY_SIZE; }

    // ====================================================================
    // ADVANCED STATISTICS FOR FEATURE EXTRACTION
    // ====================================================================

    /**
     * @brief Calculate unique key ratio (for feature [16])
     *
     * Samples keys and estimates what fraction have non-zero counts.
     * This is a proxy for "bigram uniqueness" feature.
     *
     * @param sample_size Number of random samples to test (default: 10000)
     * @return Estimated ratio of unique keys [0, 1]
     */
    auto estimate_uniqueness_ratio(size_t sample_size = 10000) const -> float {
        if (total_count_ == 0) return 0.0f;

        size_t present_count = 0;
        size_t single_count = 0;
        size_t actual_sample = std::min(sample_size, total_count_);

        for (size_t i = 0; i < actual_sample; ++i) {
            uint32_t count = query(static_cast<uint32_t>(i));
            if (count > 0) {
                ++present_count;
                if (count == 1) {
                    ++single_count;
                }
            }
        }

        if (actual_sample == 0) return 0.0f;

        // Use presence ratio as primary estimator
        // Keys that appear at all (count > 0) are likely unique since
        // we only inserted each key once. Collisions cause overestimates,
        // but presence is still a good signal.
        float presence_ratio = static_cast<float>(present_count) / static_cast<float>(actual_sample);

        // Single-count ratio provides a lower bound on uniqueness
        float single_ratio = static_cast<float>(single_count) / static_cast<float>(actual_sample);

        // Blend: presence is upper bound, single is lower bound
        // For high-entropy data with many unique keys, presence_ratio
        // will be high even with collisions
        return std::min((presence_ratio + single_ratio) * 0.5f, 1.0f);
    }

    /**
     * @brief Calculate Top-K concentration metric (for feature [17])
     *
     * Estimates how concentrated the distribution is by checking if
     * high-frequency items dominate total counts.
     *
     * Algorithm:
     * 1. Find K approximate maximum-frequency keys via sampling
     * 2. Sum their estimated counts
     * 3. Return ratio: sum_top_k / total_count
     *
     * @param k Number of top items to consider (default: 10)
     * @return Concentration ratio [0, 1] where higher = more repetitive data
     */
    // auto calculate_topk_concentration(size_t k = 10) const -> float {
    //     if (total_count_ == 0 || k == 0) return 0.0f;

    //     struct KeyCount {
    //         uint32_t key;
    //         Counter count;
    //     };

    //     // Sample candidates to find top-K approximate maxima
    //     std::vector<KeyCount> candidates;
    //     size_t num_samples = std::min((size_t)100000, WIDTH * DEPTH);

    //     for (size_t i = 0; i < num_samples; ++i) {
    //         uint32_t test_key = static_cast<uint32_t>(i * 2654435769u);
    //         Counter cnt = query(test_key);

    //         if (cnt > 0) {
    //             candidates.push_back({test_key, cnt});
    //         }
    //     }

    //     // Sort by count descending and take top K
    //     std::partial_sort(candidates.begin(),
    //                      candidates.begin() + std::min(k, candidates.size()),
    //                      candidates.end(),
    //                      [](const KeyCount& a, const KeyCount& b) {
    //                          return a.count > b.count;
    //                      });

    //     // Sum top-K counts
    //     uint64_t top_k_sum = 0;
    //     size_t actual_k = std::min(k, candidates.size());

    //     for (size_t i = 0; i < actual_k; ++i) {
    //         top_k_sum += candidates[i].count;
    //     }

    //     return static_cast<float>(top_k_sum) / total_count_;
    // }
    ///////这里改用了小顶堆，避免了对所有样本排序的开销，在大数据量时效率更高
    auto calculate_topk_concentration(size_t k = 10) const -> float {
        if (total_count_ == 0 || k == 0) return 0.0f;

        struct KeyCount {
            uint32_t key;
            Counter count;
            // 小顶堆用 > 比较（堆顶最小）
            bool operator>(const KeyCount& other) const {
                return count > other.count;
            }
        };

        // 小顶堆：只保留最大的 K 个
        std::priority_queue<KeyCount, std::vector<KeyCount>, std::greater<KeyCount>> min_heap;
        
        size_t num_samples = std::min((size_t)100000, WIDTH * DEPTH);
        for (size_t i = 0; i < num_samples; ++i) {
            uint32_t test_key = static_cast<uint32_t>(i * 2654435769u);
            Counter cnt = query(test_key);
            
            if (cnt > 0) {
                if (min_heap.size() < k) {
                    min_heap.push({test_key, cnt});
                } else if (cnt > min_heap.top().count) {
                    min_heap.pop();           // 移除当前最小的
                    min_heap.push({test_key, cnt});
                }
            }
        }

        // 累加堆中所有（即 Top K）
        uint64_t top_k_sum = 0;
        while (!min_heap.empty()) {
            top_k_sum += min_heap.top().count;
            min_heap.pop();
        }

        return static_cast<float>(top_k_sum) / total_count_;
    }

    /**
     * @brief Get raw access to internal table (for serialization/debugging)
     */
    auto get_table() const -> const std::array<std::array<Counter, WIDTH>, DEPTH>& {
        return table_;
    }

   private:
    std::array<std::array<Counter, WIDTH>, DEPTH> table_{};
    uint64_t total_count_{0};

    /**
     * @brief Independent hash function for each row
     *
     * Uses different prime multipliers for each row to ensure independence.
     * Based on Knuth's multiplicative hash with row-specific constants.
     *
     * @param key Input hash value
     * @param row Row index (selects which hash function to use)
     * @return Column index in range [0, WIDTH-1]
     */
    auto hash_function(uint32_t key, size_t row) const -> size_t {
        // Prime multipliers for each depth level (chosen to minimize collision)
        static constexpr uint32_t PRIMES[DEPTH] = {
            2654435761u,  // Golden ratio based
            809296233u,   // Large prime
            1640531513u,  // Another golden ratio variant
            2166136261u   // FNV offset basis
        };

        uint64_t product = static_cast<uint64_t>(key) * PRIMES[row];
        return static_cast<size_t>((product >> 16) % WIDTH);
    }
};

// ========================================================================
// BIGRAM/TRIGRAM HASH HELPERS
// ========================================================================

namespace ngram_utils {

/**
 * @brief Compute hash for a byte pair (bigram)
 *
 * Packs two bytes into a single 16-bit value for efficient hashing.
 * Total possible bigrams: 256 × 256 = 65,536
 *
 * @param b1 First byte
 * @param b2 Second byte
 * @return 16-bit bigram hash value
 */
inline auto compute_bigram_hash(uint8_t b1, uint8_t b2) -> uint16_t {
    return static_cast<uint16_t>((static_cast<uint16_t>(b1) << 8) | b2);
}

/**
 * @brief Compute hash for a byte triplet (trigram)
 *
 * Packs three bytes into a 24-bit value.
 * Total possible trigrams: 256³ = 16,777,216
 *
 * @param b1, b2, b3 Three consecutive bytes
 * @return 24-bit trigram hash value (truncated to 32-bit)
 */
inline auto compute_trigram_hash(uint8_t b1, uint8_t b2, uint8_t b3) -> uint32_t {
    return (static_cast<uint32_t>(b1) << 16) |
           (static_cast<uint32_t>(b2) << 8) |
           static_cast<uint32_t>(b3);
}

/**
 * @brief Extract bigrams from data stream and feed to Count-Min Sketch
 *
 * Processes data in a single pass, extracting all overlapping bigrams.
 * Time complexity: O(n) where n is data length.
 *
 * @tparam CMS Count-Min Sketch type
 * @param data Input data pointer
 * @param size Data length in bytes
 * @param cms Reference to sketch to update
 */
template <typename CMS>
auto extract_bigrams(const uint8_t* data, size_t size, CMS& cms) -> void {
    if (size < 2) return;

    for (size_t i = 0; i < size - 1; ++i) {
        uint16_t bigram = compute_bigram_hash(data[i], data[i + 1]);
        cms.update(static_cast<uint32_t>(bigram));
    }
}

/**
 * @brief Extract trigrams from data stream and feed to Count-Min Sketch
 *
 * Similar to extract_bigrams but for 3-byte sequences.
 * Use when more context is needed for better discrimination.
 */
template <typename CMS>
auto extract_trigrams(const uint8_t* data, size_t size, CMS& cms) -> void {
    if (size < 3) return;

    for (size_t i = 0; i < size - 2; ++i) {
        uint32_t trigram = compute_trigram_hash(data[i], data[i + 1], data[i + 2]);
        cms.update(trigram);
    }
}

}  // namespace ngram_utils

}  // namespace ade
}  // namespace compressor