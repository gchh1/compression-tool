#pragma once

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
        size_t range = 1);

    std::vector<uint8_t> literalrun(const std::vector<uint8_t>& input);

public:
    LZMine(size_t search_bytelength = 2, size_t look_bytelength = 2)
        : SEARCH_BYTELENGTH_(search_bytelength),
          LOOKAHEAD_BYTELENGTH_(look_bytelength) {
        size_t tmp = SEARCH_BYTELENGTH_;
        max_search_size_ = 0xFF;
        while (tmp > 1) {
            max_search_size_ <<= 8;
            max_search_size_ |= 0xFF;
            tmp--;
        }
        tmp = LOOKAHEAD_BYTELENGTH_;
        max_look_size_ = 0xFF;
        while (tmp > 1) {
            max_look_size_ <<= 8;
            max_look_size_ |= 0xFF;
            tmp--;
        }
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

    std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);
};

} // namespace algorithm
} // namespace compressor
