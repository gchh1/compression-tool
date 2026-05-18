#pragma once


#include <algorithm>
#include <cstdint>
#include <vector>

#include "LZencoding.hpp"
#include "Models.hpp"

namespace compressor::algorithm::LZMatcher {

template <typename Iter>
std::vector<size_t> Next(Iter pattern, size_t len){
    
    std::vector<size_t> next(len, 0);
    size_t j = 0;
    for (size_t i = 1; i < len; i++) {
        while (pattern[i] != pattern[j] && j != 0) {
            j = next[j - 1];
        }
        if (pattern[i] == pattern[j]) j++;
        next[i] = j;
    }
    return next;
}

template <typename Iter>
std::vector<Triple> kmpSearch(
    Iter search_begin,
    size_t search_len,
    Iter lookahead_begin,
    size_t lookahead_len,
    size_t dp_top,
    size_t min_match_len)
{   
    models::TopMatch matches(dp_top);
    std::vector<size_t> next = Next(lookahead_begin, lookahead_len);//支持重叠匹配
    size_t j = 0;


    for (size_t i = 0; i < search_len+lookahead_len-1; i++) {
        auto current_char = (i < search_len) ? search_begin[i] : lookahead_begin[i - search_len];
        while (current_char != lookahead_begin[j] && j != 0) {
            j = next[j - 1];
        }
        if (current_char == lookahead_begin[j]) {j++;}
        
        size_t match_len = std::min(j, lookahead_len);
        if (match_len < min_match_len) continue;
        size_t offset = search_len - i + match_len - 1;
        matches.insert(Triple(offset, match_len));
        if (j >= lookahead_len) {
            j = (lookahead_len > 1) ? next[lookahead_len - 1] : 0;
        }
    }
    if (matches.isEmpty()) {
        return {Triple(0, 1, lookahead_begin[0])};
    }
    return matches.getTriples();

}




}


