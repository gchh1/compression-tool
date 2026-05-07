#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

enum class LZMineMatchType { KMPNEXT };

class LZMine {
public:
    struct Triple {
        size_t offset;
        size_t length;
        uint8_t next_byte;
        Triple(size_t a, size_t b, uint8_t c)
            : offset(a), length(b), next_byte(c) {}
        Triple() : offset(0), length(0), next_byte('\0') {}
    };

    struct DPCandidate {
        size_t offset;
        size_t length;
        uint8_t next_byte;
        bool is_chosen;
    };

    struct DPState {
        size_t position;
        bool reachable;
        size_t token_count;
        size_t predecessor;
        Triple choice;
    };

    struct DPStep {
        size_t position;
        std::vector<DPCandidate> candidates;
        size_t best_token_count;
    };

    struct DPVisualization {
        std::vector<DPStep> steps;
        std::vector<DPState> dp_array;
        std::vector<Triple> optimal_path;
        size_t input_length;
        size_t search_size;
        size_t lookahead_size;
    };

private:
    size_t SEARCH_BYTELENGTH_;
    size_t LOOKAHEAD_BYTELENGTH_;
    size_t max_search_size_;
    size_t max_look_size_;

    std::vector<size_t> kmp_next(std::vector<uint8_t>::const_iterator pattern,
                                  size_t len);

    std::vector<Triple> kmpMatch(
        std::vector<uint8_t>::const_iterator search_window,
        size_t search_window_size,
        std::vector<uint8_t>::const_iterator lookahead_window,
        size_t lookahead_window_size,
        size_t range = 1,
        size_t min_match_length = 3);

    std::vector<uint8_t> literalrun(const std::vector<uint8_t>& input);

public:
    LZMine(size_t search_bytelength = 0, size_t look_bytelength = 0)
        : SEARCH_BYTELENGTH_(search_bytelength),
          LOOKAHEAD_BYTELENGTH_(look_bytelength) {
        if (SEARCH_BYTELENGTH_ == 0) SEARCH_BYTELENGTH_ = 2;
        if (LOOKAHEAD_BYTELENGTH_ == 0) LOOKAHEAD_BYTELENGTH_ = 2;
        max_search_size_ = calcMaxSize(SEARCH_BYTELENGTH_);
        max_look_size_ = calcMaxSize(LOOKAHEAD_BYTELENGTH_);
    }

    static size_t calcByteLength(size_t max_val) {
        if (max_val <= 0xFF) return 1;
        if (max_val <= 0xFFFF) return 2;
        if (max_val <= 0xFFFFFF) return 3;
        return 4;
    }

    static size_t calcMaxSize(size_t bytelength) {
        size_t v = 0xFF;
        for (size_t i = 1; i < bytelength; i++) {
            v <<= 8;
            v |= 0xFF;
        }
        return v;
    }

    void autoByteLength(size_t search_size, size_t lookahead_size) {
        SEARCH_BYTELENGTH_ = calcByteLength(search_size);
        LOOKAHEAD_BYTELENGTH_ = calcByteLength(lookahead_size);
        max_search_size_ = calcMaxSize(SEARCH_BYTELENGTH_);
        max_look_size_ = calcMaxSize(LOOKAHEAD_BYTELENGTH_);
    }

    std::vector<uint8_t> compress(const std::vector<uint8_t>& input,
                                   size_t search_size,
                                   size_t lookahead_size,
                                   LZMineMatchType match_type =
                                       LZMineMatchType::KMPNEXT);

    std::vector<uint8_t> compress_ultra(const std::vector<uint8_t>& input,
                                         size_t search_size,
                                         size_t lookahead_size,
                                         size_t range = 3,
                                         LZMineMatchType match_type =
                                             LZMineMatchType::KMPNEXT);

    std::vector<Triple> compress_ultra_triples(
        const std::vector<uint8_t>& input,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3);

    DPVisualization get_dp_visualization(
        const std::vector<uint8_t>& input,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3);

    std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);
};

} // namespace algorithm
} // namespace compressor
