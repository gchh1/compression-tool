#include "KMPMatcher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

std::vector<KMPMatch> kmpSearch(
    const uint8_t* search_begin, size_t search_len,
    const uint8_t* look_begin, size_t look_len,
    size_t top_k, size_t min_match) {

    std::vector<KMPMatch> results;

    if (search_len == 0 || look_len == 0) return results;

    for (size_t start = 0; start + min_match <= search_len; ++start) {
        size_t match_len = 0;
        while (match_len < look_len && start + match_len < search_len &&
               search_begin[start + match_len] == look_begin[match_len]) {
            ++match_len;
        }
        if (match_len >= min_match) {
            KMPMatch m;
            m.offset = search_len - start;
            m.length = match_len;
            results.push_back(m);
            if (results.size() >= top_k) break;
        }
    }

    if (results.empty() && search_len > 0) {
        KMPMatch m;
        m.offset = 0;
        m.length = 0;
        results.push_back(m);
    }
    return results;
}

}  // namespace compressor::algorithm