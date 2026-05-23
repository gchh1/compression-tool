#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "BitProcessor.hpp"
#include "HuffmanTree.hpp"
#include "LZencoding.hpp"

namespace compressor::algorithm {

struct DeflateLengthExtra {
    uint32_t base;
    uint32_t extra_bits;
};
struct DeflateDistExtra {
    uint32_t base;
    uint32_t extra_bits;
};

inline const DeflateLengthExtra kDeflateLengthTable[] = {
    {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0},
    {11, 1}, {13, 1}, {15, 1}, {17, 1},
    {19, 2}, {23, 2}, {27, 2}, {31, 2},
    {35, 3}, {43, 3}, {51, 3}, {59, 3},
    {67, 4}, {83, 4}, {99, 4}, {115, 4},
    {131, 5}, {163, 5}, {195, 5}, {227, 5},
    {258, 0}
};
inline constexpr size_t kDeflateLengthCodeCount = 29;

inline const DeflateDistExtra kDeflateDistTable[] = {
    {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 1}, {7, 1},
    {9, 2}, {13, 2}, {17, 3}, {25, 3}, {33, 4}, {49, 4},
    {65, 5}, {97, 5}, {129, 6}, {193, 6}, {257, 7}, {385, 7},
    {513, 8}, {769, 8}, {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10},
    {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13}
};
inline constexpr size_t kDeflateDistCodeCount = 30;

inline size_t findLengthCode(uint32_t length) {
    for (size_t i = 0; i < kDeflateLengthCodeCount; ++i) {
        if (length <= kDeflateLengthTable[i].base) return i;
        if (i + 1 < kDeflateLengthCodeCount &&
            length < kDeflateLengthTable[i + 1].base) return i;
    }
    return 0;
}

inline size_t findDistCode(uint32_t dist) {
    for (size_t i = 0; i < kDeflateDistCodeCount; ++i) {
        if (dist <= kDeflateDistTable[i].base) return i;
        if (i + 1 < kDeflateDistCodeCount &&
            dist < kDeflateDistTable[i + 1].base) return i;
    }
    return 0;
}

struct InflateEncodeResult {
    std::vector<uint8_t> data;
    uint32_t literal_tree_bits;
    uint32_t dist_tree_bits;
};

inline InflateEncodeResult inflate_encode(
    const std::vector<Triple>& triples,
    compressor::utils::_buffer& pending) {
    InflateEncodeResult result;
    if (triples.empty()) return result;

    std::vector<uint32_t> lit_freq(288, 0);
    std::vector<uint32_t> dist_freq(kDeflateDistCodeCount, 0);

    for (const auto& t : triples) {
        if (t.offset == 0) {
            lit_freq[t.literal]++;
        } else {
            size_t len_code = findLengthCode(t.length);
            lit_freq[257 + len_code]++;
            size_t dist_code = findDistCode(t.offset);
            dist_freq[dist_code]++;
        }
    }
    lit_freq[256] = 1;

    HuffmanTree lit_tree(lit_freq, 288, 9);
    HuffmanTree dist_tree(dist_freq, kDeflateDistCodeCount, 5);
    auto lit_dict = lit_tree.buildDictionary();
    auto dist_dict = dist_tree.buildDictionary();

    result.literal_tree_bits = static_cast<uint32_t>(lit_tree.getTreeSize());
    result.dist_tree_bits = static_cast<uint32_t>(dist_tree.getTreeSize());

    compressor::utils::BitWriter writer(result.data, pending);

    writer.writeBits(static_cast<uint64_t>(result.literal_tree_bits), 32);
    writer.writeBits(static_cast<uint64_t>(result.dist_tree_bits), 32);
    lit_tree.serialize(writer);
    dist_tree.serialize(writer);

    writer.writeBits(static_cast<uint64_t>(triples.size()), 32);

    for (const auto& t : triples) {
        if (t.offset == 0) {
            writeHuffmanCode(writer, lit_dict[t.literal]);
        } else {
            size_t len_code = findLengthCode(t.length);
            writeHuffmanCode(writer, lit_dict[257 + len_code]);
            if (kDeflateLengthTable[len_code].extra_bits > 0) {
                uint32_t extra = t.length - kDeflateLengthTable[len_code].base;
                writer.writeBits(extra, kDeflateLengthTable[len_code].extra_bits);
            }

            size_t dist_code = findDistCode(t.offset);
            writeHuffmanCode(writer, dist_dict[dist_code]);
            if (kDeflateDistTable[dist_code].extra_bits > 0) {
                uint32_t extra = t.offset - kDeflateDistTable[dist_code].base;
                writer.writeBits(extra, kDeflateDistTable[dist_code].extra_bits);
            }
        }
    }

    writeHuffmanCode(writer, lit_dict[256]);

    pending = writer.getBuf();
    return result;
}

struct InflateDecodeResult {
    std::vector<uint8_t> data;
};

inline InflateDecodeResult inflate_decode(
    const std::vector<uint8_t>& compressed,
    compressor::utils::_buffer& pending) {
    InflateDecodeResult result;

    compressor::utils::BitReader reader(compressed, pending);

    uint64_t val;
    reader.readBits(val, 32);
    uint32_t lit_tree_bits = static_cast<uint32_t>(val);

    reader.readBits(val, 32);
    uint32_t dist_tree_bits = static_cast<uint32_t>(val);

    HuffmanTree lit_tree;
    {
        std::vector<uint32_t> dummy_freq(288, 1);
        new (&lit_tree) HuffmanTree(dummy_freq, 288, 9);
        lit_tree.deserialize(reader);
    }

    HuffmanTree dist_tree;
    {
        std::vector<uint32_t> dummy_freq(kDeflateDistCodeCount, 1);
        new (&dist_tree) HuffmanTree(dummy_freq, kDeflateDistCodeCount, 5);
        dist_tree.deserialize(reader);
    }

    reader.readBits(val, 32);
    uint32_t token_count = static_cast<uint32_t>(val);

    uint32_t decoded = 0;
    while (decoded < token_count) {
        uint16_t sym = readHuffmanSymbol(reader, lit_tree.getRoot());

        if (sym == 256) break;

        if (sym < 256) {
            result.data.push_back(static_cast<uint8_t>(sym));
            decoded++;
        } else {
            size_t len_code = sym - 257;
            uint32_t length = kDeflateLengthTable[len_code].base;
            if (kDeflateLengthTable[len_code].extra_bits > 0) {
                uint64_t extra;
                reader.readBits(extra, kDeflateLengthTable[len_code].extra_bits);
                length += static_cast<uint32_t>(extra);
            }

            uint16_t dist_sym = readHuffmanSymbol(reader, dist_tree.getRoot());
            uint32_t dist = kDeflateDistTable[dist_sym].base;
            if (kDeflateDistTable[dist_sym].extra_bits > 0) {
                uint64_t extra;
                reader.readBits(extra, kDeflateDistTable[dist_sym].extra_bits);
                dist += static_cast<uint32_t>(extra);
            }

            size_t copy_start = result.data.size() > dist
                ? result.data.size() - dist : 0;
            for (uint32_t i = 0; i < length; ++i) {
                if (copy_start + i < result.data.size()) {
                    result.data.push_back(result.data[copy_start + i]);
                }
            }
            decoded++;
        }
    }

    pending = reader.getBuf();
    return result;
}

}  // namespace compressor::algorithm