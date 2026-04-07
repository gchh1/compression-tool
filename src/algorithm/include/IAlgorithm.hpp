/**
 * @file IAlgorithm.hpp
 * @author yhc
 * @brief Common structure, class, constant are defined here to be included by
 *        every algorithm
 * @version 0.1
 * @date 2026-04-05
 *
 * @copyright Copyright (c) 2026
 *
 */

// Include lib here
#include <cstdint>

namespace compressor {
namespace algorithm {
struct Token {
    bool is_literal;    // true: character; false: (length, position)
    uint16_t value;     // char or length
    uint16_t position;  // position
};
}  // namespace algorithm

}  // namespace compressor