#pragma once

#include <cstddef>
#include <cstdint>

namespace compressor::algorithm {

/**
 * One slot in a byte-indexed LZ-style DP table (minimum token-count objective).
 *
 * Logical fields align with LZDP::DPState (token_count, predecessor, choice as
 * offset/length; literal when match_length==0 is read from input[predecessor]).
 *
 * DPFlate keeps a dense std::vector of these cells. LZDP may use heap nodes
 * (ROLListNode<Triple>) for the same recurrence; the *meaning* of the fields
 * is shared, only storage strategy differs.
 */
struct LzStyleDpCell {
    static constexpr size_t kUnreachable = SIZE_MAX;

    size_t token_count{kUnreachable};
    size_t predecessor{0};
    size_t match_offset{0};
    size_t match_length{0};
};

}  // namespace compressor::algorithm
