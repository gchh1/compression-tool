#pragma once

// Include lib here

#include <cstdint>
#include <vector>
namespace compressor {
namespace algorithm {

struct Token {
    bool is_literal;
    uint16_t value;
    uint16_t position;
};

// TODO replace the slide window with hash table
class Deflate {
   public:
    static std::vector<Token> compress(const std::vector<uint8_t>& input);
};

}  // namespace algorithm

}  // namespace compressor