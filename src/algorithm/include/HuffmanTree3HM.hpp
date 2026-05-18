#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

/**
 * @brief 3HfMTree — Three Huffman Trees encoding with fixed-chunk multi-level queries.
 *
 * Splits match tokens into offset + length, building three independent trees:
 *   - Literal tree (256 symbols, byte values 0-255)
 *   - Offset tree (2^b_o symbols), b_o = offset_chunk_bits — LSB-first chunks for offset_bits_
 *   - Length tree (2^b_l symbols), b_l = length_chunk_bits — same for length_bits_
 *
 * Multi-level query: offset uses ceil(offset_bits_/b_o) chunks of b_o bits (from LSB);
 * length uses ceil(length_bits_/b_l) chunks of b_l bits. The last chunk may have fewer
 * valid bits; the decoder truncates using offset_bits_/length_bits_ from header.
 *
 * Decoding automaton (non-flag mode):
 *   DECODE_OFFSET → offset=0 → DECODE_RUN_LEN → DECODE_LITERALS × N → DECODE_OFFSET
 *                 → offset>0 → DECODE_MATCH_LEN → DECODE_OFFSET
 */
class HuffmanTree3HM {
   public:
    HuffmanTree3HM() = default;
    ~HuffmanTree3HM() = default;

    HuffmanTree3HM(const HuffmanTree3HM&) = delete;
    HuffmanTree3HM& operator=(const HuffmanTree3HM&) = delete;

    /** @brief Build three trees from frequency maps */
    void buildTrees(const std::vector<uint32_t>& literal_freq,
                    const std::vector<uint32_t>& offset_freq,
                    const std::vector<uint32_t>& length_freq,
                    size_t offset_count, size_t length_count,
                    size_t offset_bits, size_t length_bits,
                    size_t offset_chunk_bits, size_t length_chunk_bits);

    /**
     * @brief Serialize header + three trees to BitWriter.
     *
     * Header (4 bytes, LSB first):
     *   offset_bits  : 8 bits
     *   length_bits  : 8 bits
     *   offset_chunk_bits : 8 bits
     *   length_chunk_bits : 8 bits
     *
     * Followed by literal_tree_, offset_tree_, length_tree_ in order.
     */
    void serialize(utils::BitWriter& writer) const;

    /** @brief Deserialize header + three trees from BitReader (self-describing) */
    void deserialize(utils::BitReader& reader);

    /** @brief Deserialize three trees from BitReader with explicit parameters */
    void deserialize(utils::BitReader& reader,
                     size_t offset_count, size_t length_count,
                     size_t offset_bits, size_t length_bits,
                     size_t offset_chunk_bits, size_t length_chunk_bits);

    /** @brief Encode a literal byte using literal tree */
    void encodeLiteral(uint8_t byte, utils::BitWriter& writer) const;

    /**
     * @brief Encode a match (offset + length) using multi-level queries.
     * Splits offset into ceil(offset_bits_/chunk_bits_) chunks,
     * length into ceil(length_bits_/chunk_bits_) chunks.
     */
    void encodeMatch(uint16_t offset, uint16_t length,
                     utils::BitWriter& writer) const;

    /**
     * @brief Encode a literal run header (offset=0 + run_length).
     * offset=0 encoded as multi-level, run_length similarly.
     */
    void encodeRunHeader(uint16_t run_length,
                         utils::BitWriter& writer) const;

    /**
     * @brief Decode one offset value from BitReader using multi-level query.
     * @return decoded offset value (0 indicates literal run)
     */
    uint16_t decodeOffset(utils::BitReader& reader) const;

    /** @brief After decodeOffset returns 0, decode the run length */
    uint16_t decodeRunLength(utils::BitReader& reader) const;

    /** @brief Decode one literal byte */
    uint8_t decodeLiteral(utils::BitReader& reader) const;

    /** @brief After decodeOffset returns >0, decode the match length */
    uint16_t decodeMatchLength(utils::BitReader& reader) const;

    /** @brief Get serialized tree size in bits */
    size_t getTreeSize() const;

    size_t getOffsetCount() const { return offset_count_; }
    size_t getLengthCount() const { return length_count_; }
    size_t getOffsetBits() const { return offset_bits_; }
    size_t getLengthBits() const { return length_bits_; }
    size_t getOffsetChunkBits() const { return offset_chunk_bits_; }
    size_t getLengthChunkBits() const { return length_chunk_bits_; }

   private:
    /** @brief Encode a value using multi-level query with the given tree/dict */
    void encodeMultiLevel(uint16_t value, size_t value_bits,
                          const HuffmanTree& tree,
                          const std::vector<HuffmanCode>& dict,
                          size_t chunk_bits,
                          utils::BitWriter& writer) const;

    /** @brief Decode a value using multi-level query with the given tree */
    uint16_t decodeMultiLevel(size_t value_bits,
                              const HuffmanTree& tree,
                              size_t chunk_bits,
                              utils::BitReader& reader) const;

    std::unique_ptr<HuffmanTree> literal_tree_;
    std::unique_ptr<HuffmanTree> offset_tree_;
    std::unique_ptr<HuffmanTree> length_tree_;

    std::vector<HuffmanCode> literal_dict_;
    std::vector<HuffmanCode> offset_dict_;
    std::vector<HuffmanCode> length_dict_;

    size_t offset_count_{0};
    size_t length_count_{0};
    size_t offset_bits_{0};
    size_t length_bits_{0};
    size_t offset_chunk_bits_{8};
    size_t length_chunk_bits_{8};
};

}  // namespace compressor::algorithm