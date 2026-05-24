#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

struct KMPMatch {
    size_t offset;
    size_t length;
};

std::vector<KMPMatch> kmpSearch(
    const uint8_t* search_begin, size_t search_len,
    const uint8_t* look_begin, size_t look_len,
    size_t top_k, size_t min_match);

template <typename Iter>
std::vector<KMPMatch> kmpSearch(
    Iter search_begin, size_t search_len,
    Iter look_begin, size_t look_len,
    size_t top_k, size_t min_match) {
    return kmpSearch(&*search_begin, search_len, &*look_begin, look_len, top_k, min_match);
}

}  // namespace compressor::algorithm