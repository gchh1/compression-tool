#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "HuffmanTree3HM.hpp"
#include "IAlgorithm.hpp"

namespace compressor::algorithm {

/**
 * @brief Inflate3HM — Decompressor for 3HfMTree-encoded bitstream.
 *
 * Decode automaton (non-flag mode):
 *   READ_HEADER → DECODE_OFFSET
 *     → offset=0 → DECODE_RUN_LEN → DECODE_LITERALS × N → DECODE_OFFSET
 *     → offset>0 → DECODE_MATCH_LEN → COPY_MATCH → DECODE_OFFSET
 *
 * The bitstream format is produced by DPFlate with use_3hfmtree_=true:
 *   [4-byte header: offset_bits, length_bits, offset_chunk_bits, length_chunk_bits]
 *   [serialized literal tree]
 *   [serialized offset tree]
 *   [serialized length tree]
 *   [encoded tokens]
 */
class Inflate3HM : public AlgorithmBase {
   public:
    Inflate3HM();
    auto reset(void) -> void override;

   protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

   private:
    static constexpr size_t kWindowSize = 32768;

    std::vector<uint8_t> window_;
    uint64_t out_abs_{0};
    std::vector<uint8_t> output_buf_;
    size_t output_flush_pos_{0};

    std::unique_ptr<HuffmanTree3HM> huffman_tree_3hm_;

    enum class DecodeState {
        READ_HEADER,
        DECODE_OFFSET,
        DECODE_RUN_LEN,
        DECODE_LITERALS,
        DECODE_MATCH_LEN,
        COPY_MATCH,
        FLUSH_TO_WRITER
    };
    DecodeState decode_state_{DecodeState::READ_HEADER};

    uint16_t pending_offset_{0};
    uint16_t pending_length_{0};
    size_t pending_literal_count_{0};
    size_t literal_run_pos_{0};

    auto appendDecodedByte(uint8_t b) -> void;
};

}  // namespace compressor::algorithm