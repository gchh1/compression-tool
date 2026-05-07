#include "LZCrazy.hpp"

#include <cmath>
#include <stdexcept>

#include "BitReader.hpp"
#include "BitWriter.hpp"

namespace compressor {
namespace algorithm {

size_t LZCrazy::calcBits(size_t max_val) {
    if (max_val <= 1) return 1;
    size_t bits = 0;
    size_t v = max_val;
    while (v > 0) {
        bits++;
        v >>= 1;
    }
    return bits;
}

std::vector<uint8_t> LZCrazy::compress(
    const std::vector<uint8_t>& input,
    size_t search_size,
    size_t lookahead_size,
    size_t min_match) {
    if (input.empty()) return {};

    if (search_size > MAX_SEARCH_SIZE_) search_size = MAX_SEARCH_SIZE_;
    if (lookahead_size > MAX_LOOKAHEAD_SIZE_) lookahead_size = MAX_LOOKAHEAD_SIZE_;
    if (min_match < 1) min_match = 1;

    size_t offset_bits = calcBits(search_size);
    size_t length_bits = calcBits(lookahead_size - min_match);

    size_t in_len = input.size();

    std::vector<uint8_t> output(in_len + in_len / 4 + 64, 0);
    utils::BitWriter writer{std::span<uint8_t>(output)};

    writer.writeBits(in_len, 32);
    writer.writeBits(offset_bits, 5);
    writer.writeBits(length_bits, 5);
    writer.writeBits(min_match, 5);

    size_t pos = 0;
    while (pos < in_len) {
        size_t search_start = (pos > search_size) ? pos - search_size : 0;
        size_t best_offset = 0;
        size_t best_length = 0;

        for (size_t i = search_start; i < pos; ++i) {
            size_t cur_len = 0;
            while (cur_len < lookahead_size &&
                   pos + cur_len < in_len &&
                   input[i + cur_len] == input[pos + cur_len]) {
                cur_len++;
            }
            if (cur_len > best_length) {
                best_length = cur_len;
                best_offset = pos - i;
            }
        }

        if (best_length >= min_match) {
            writer.writeBit(0);
            writer.writeBits(best_offset, static_cast<uint8_t>(offset_bits));
            writer.writeBits(best_length - min_match, static_cast<uint8_t>(length_bits));
            pos += best_length;
        } else {
            writer.writeBit(1);
            writer.writeBits(input[pos], 8);
            pos++;
        }
    }

    size_t total_bytes = writer.flush();
    output.resize(total_bytes);
    return output;
}

std::vector<uint8_t> LZCrazy::decompress(
    const std::vector<uint8_t>& input) {
    if (input.size() < 6) return {};

    utils::BitReader reader{std::span<const uint8_t>(input)};

    uint64_t original_size = reader.readBits(32);
    uint64_t offset_bits = reader.readBits(5);
    uint64_t length_bits = reader.readBits(5);
    uint64_t min_match = reader.readBits(5);

    if (offset_bits == 0 || original_size == 0) return {};

    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(original_size));

    while (output.size() < original_size) {
        if (!reader.ensureBits(1)) break;
        uint64_t flag = reader.readBit();

        if (flag == 1) {
            if (!reader.ensureBits(8)) break;
            uint64_t ch = reader.readBits(8);
            output.push_back(static_cast<uint8_t>(ch));
        } else {
            if (!reader.ensureBits(static_cast<uint8_t>(offset_bits + length_bits))) break;
            uint64_t offset = reader.readBits(static_cast<uint8_t>(offset_bits));
            uint64_t length_val = reader.readBits(static_cast<uint8_t>(length_bits));
            size_t length = static_cast<size_t>(length_val) + min_match;

            if (offset == 0 || offset > output.size()) {
                throw std::runtime_error("LZCrazy decompress: offset out of range");
            }

            size_t copy_start = output.size() - offset;
            for (size_t k = 0; k < length; k++) {
                output.push_back(output[copy_start + k]);
            }
        }
    }

    output.resize(static_cast<size_t>(original_size));
    return output;
}

}  // namespace algorithm
}  // namespace compressor
