#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "LZencoding.hpp"

namespace compressor::algorithm::models {

enum class MatchEngine : int {
    KMP = 0,
    HashChain = 1,
};

class TopMatch {
    uint8_t size;
    std::vector<Triple> triples;

public:
    TopMatch(uint8_t dp_top) : size(dp_top) {}

    void insert(const Triple& triple) {
        if (size == 0 || triple.offset == 0) return;
        //二分查找
        uint8_t lo = 0, hi = static_cast<uint8_t>(triples.size());
        while (lo < hi) {
            uint8_t mid = lo + (hi - lo) / 2;
            if (triples[mid].length >= triple.length) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        triples.insert(triples.begin() + lo, triple);
        if (triples.size() > size) {
            triples.pop_back();
        }
    }

    bool isEmpty() const { return triples.empty(); }
    uint8_t getCount() const { return static_cast<uint8_t>(triples.size()); }
    const std::vector<Triple>& getTriples() const { return triples; }
};


//// 准备转移到lzdp文件中
/// lzdp_core DP 槽：代价统计 + 回溯前驱 + 该步 Triple
struct DPNode {
    size_t literal_count;
    size_t match_count;
    int pre_pos;
    Triple triple;
    
    DPNode(
        size_t literal_count=0, 
        size_t match_count=0, 
        int pre_pos=-2,
        const Triple& triple=Triple(0, 0, 0)
    ):literal_count(literal_count), match_count(match_count), pre_pos(pre_pos), triple(triple) {}
};

}  // namespace compressor::algorithm::models
