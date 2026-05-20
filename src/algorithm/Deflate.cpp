// Include lib here

#include "Deflate.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "BitWriter.hpp"
#include "DebugLog.hpp"
#include "Deflate.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

/**
 * @brief Construct a new Deflate:: Deflate object
 *
 */
Deflate::Deflate(size_t slide_size, size_t min_match, size_t max_chain_length,
                 size_t lookahead_max, bool use_flag_encoding)
    : SLIDE_SIZE(slide_size),
      WINDOW_SIZE(2 * slide_size),
      MIN_MATCH(min_match),
      MAX_MATCH([&]() -> size_t {
          const size_t cap = lookahead_max == 0 ? size_t(258)
                                                : std::min(lookahead_max, size_t(258));
          return std::min(std::max(cap, min_match), size_t(258));
      }()),
      HASH_SIZE(slide_size),
      MAX_CHAIN_LENGTH(max_chain_length),
      use_flag_encoding_(use_flag_encoding) {
    offset_bits_ = static_cast<size_t>(std::bit_width(std::max(SLIDE_SIZE, size_t{1})));
    length_bits_ = static_cast<size_t>(std::bit_width(std::max(MAX_MATCH, size_t{1})));
    reset();
}

/**
 * @brief Reset the algorithm to original state
 *
 */
auto Deflate::reset(void) -> void {
    window_.resize(WINDOW_SIZE, 0);

    head_.assign(HASH_SIZE, NULL_PTR);
    prev_.assign(SLIDE_SIZE, NULL_PTR);

    cursor_ = 0;
    lookahead_ = 0;

    token_buffer_.clear();
    token_flush_idx_ = 0;

    nonflag_header_emitted_ = false;

    // ============================================================
    // [VIZ] Reset visualization tracking — restored from 8672f99
    // ============================================================
    input_pos_ = 0;
    block_index_ = 0;
    block_input_start_ = 0;
    block_literal_count_ = 0;
    block_match_count_ = 0;
    block_output_start_ = 0;
    // ============================================================
    // [VIZ END]
    // ============================================================

    deflate_state_ = DeflateState::FIND_MATCHES;
}

/**
 * @brief
 *
 * @param read
 * @param read_offset
 */
auto Deflate::fillWindow(void) -> void {
    if (cursor_ + lookahead_ >= WINDOW_SIZE) {
        slideWindow();
    }

    size_t space_left = WINDOW_SIZE - (cursor_ + lookahead_);
    size_t remain = reader_.getRemainSize();
    size_t to_copy = std::min(space_left, remain);

    if (to_copy > 0) {
        reader_.readBytes(window_.data() + cursor_ + lookahead_, to_copy);
        lookahead_ += to_copy;
    }
}

/**
 * @brief
 *
 */
auto Deflate::slideWindow(void) -> void {
    std::memcpy(window_.data(), window_.data() + SLIDE_SIZE, SLIDE_SIZE);

    cursor_ -= SLIDE_SIZE;

    for (size_t i = 0; i < HASH_SIZE; ++i) {
        if (head_[i] != NULL_PTR)
            head_[i] =
                (head_[i] >= SLIDE_SIZE) ? head_[i] - SLIDE_SIZE : NULL_PTR;
    }

    for (size_t i = 0; i < SLIDE_SIZE; ++i) {
        if (prev_[i] != NULL_PTR)
            prev_[i] =
                (prev_[i] >= SLIDE_SIZE) ? prev_[i] - SLIDE_SIZE : NULL_PTR;
    }
}

/**
 * @brief In this state,
 *
 * @param status
 * @param is_last_chunk
 */
auto Deflate::handleFindMatches(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    if (lookahead_ < MAX_MATCH && reader_.getRemainSize() > 0) {
        fillWindow();
    }

    if (lookahead_ == 0) {
        if (!is_last_chunk) {
            status.need_input = true;
            return;
        } else {
            deflate_state_ = DeflateState::BUILD_TREE;
            return;
        }
    }

    size_t match_length = 0;
    size_t match_distance = 0;

    if (lookahead_ >= MIN_MATCH) {
        uint16_t hash_val = getHash(cursor_);
        uint16_t match_pos = head_[hash_val];

        prev_[cursor_ % SLIDE_SIZE] = match_pos;
        head_[hash_val] = static_cast<uint16_t>(cursor_);

        size_t chain_length = MAX_CHAIN_LENGTH;
        while (match_pos != NULL_PTR && chain_length-- > 0) {
            size_t distance = cursor_ - match_pos;
            if (distance > SLIDE_SIZE || distance == 0) break;

            size_t current_len = 0;
            size_t max_possible = std::min(MAX_MATCH, lookahead_);

            while (current_len < max_possible &&
                   window_[cursor_ + current_len] ==
                       window_[match_pos + current_len]) {
                current_len++;
            }

            if (current_len > match_length) {
                match_length = current_len;
                match_distance = distance;
                if (match_length == max_possible) break;
            }
            match_pos = prev_[match_pos % SLIDE_SIZE];
        }
    }

    if (match_length >= MIN_MATCH) {
        Token t;
        t.is_literal = false;
        getLengthCode(match_length, t.code, t.length_extra_bits,
                      t.length_extra_val);
        getDistCode(match_distance, t.dist_code, t.dist_extra_bits,
                    t.dist_extra_val);
        t.match_len = static_cast<uint16_t>(match_length);
        t.match_dist = static_cast<uint16_t>(match_distance);
        token_buffer_.push_back(t);

        // ============================================================
        // [VIZ] Emit match event — restored from 8672f99
        // ============================================================
        notifyObservers(MatchEvent{input_pos_,
                                   static_cast<uint16_t>(match_distance),
                                   static_cast<uint16_t>(match_length), 0});
        // ============================================================
        // [VIZ END]
        // ============================================================

        for (size_t i = 1; i < match_length; ++i) {
            // [VIZ] Track input position — restored from 8672f99
            input_pos_++;
            cursor_++;
            lookahead_--;
            if (lookahead_ >= MIN_MATCH) {
                uint16_t hash_val = getHash(cursor_);
                prev_[cursor_ % SLIDE_SIZE] = head_[hash_val];
                head_[hash_val] = static_cast<uint16_t>(cursor_);
            }
        }
        // [VIZ] Track input position & match count — restored from 8672f99
        input_pos_++;
        cursor_++;
        lookahead_--;
        ++block_match_count_;
    } else {
        Token t;
        t.is_literal = true;
        t.code = window_[cursor_];
        token_buffer_.push_back(t);

        // ============================================================
        // [VIZ] Emit literal event — restored from 8672f99
        // ============================================================
        notifyObservers(
            MatchEvent{input_pos_, 0, 0, window_[cursor_]});
        // ============================================================
        // [VIZ END]
        // ============================================================

        // [VIZ] Track input position & literal count — restored from 8672f99
        input_pos_++;
        cursor_++;
        lookahead_--;
        ++block_literal_count_;
    }

    if (token_buffer_.size() >= MAX_BLOCK_TOKENS) {
        deflate_state_ = DeflateState::BUILD_TREE;
    }
}

auto Deflate::handleBuildTree(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    if (token_buffer_.empty() && !is_last_chunk) {
        deflate_state_ = DeflateState::FIND_MATCHES;
        return;
    }

    if (!use_flag_encoding_) {
        token_flush_idx_ = 0;
        deflate_state_ = DeflateState::FLUSH_TOKENS;
        return;
    }

    std::vector<uint32_t> freq_map(DEFLATE_ALPHABET_SIZE, 0);
    std::vector<uint32_t> dist_freq(DISTANCE_DICTIONARY_SIZE, 0);
    for (const auto& token : token_buffer_) {
        freq_map[token.code]++;
        if (!token.is_literal) {
            dist_freq[token.dist_code]++;
        }
    }
    // EOF
    freq_map[256] = 1;

    huffman_tree_ = std::make_unique<HuffmanTree>(
        freq_map, DEFLATE_ALPHABET_SIZE, DEFLATE_SYMBOL_BITS);
    dist_tree_ = std::make_unique<HuffmanTree>(
        dist_freq, DISTANCE_DICTIONARY_SIZE, DISTANCE_SYMBOL_BITS);

    dictionary_ = huffman_tree_->buildDictionary();
    dist_dictionary_ = dist_tree_->buildDictionary();

    // ============================================================
    // [VIZ] Emit Huffman tree built events — restored from 8672f99
    // ============================================================
    {
        HuffmanTreeBuilt lit_tree_ev;
        lit_tree_ev.block_index = block_index_;
        lit_tree_ev.tree_type = 0;
        lit_tree_ev.alphabet_size = DEFLATE_ALPHABET_SIZE;
        for (size_t i = 0; i < DEFLATE_ALPHABET_SIZE && i < 286; ++i)
            lit_tree_ev.code_lengths[i] = dictionary_[i].length;
        notifyObservers(lit_tree_ev);
    }
    {
        HuffmanTreeBuilt dist_tree_ev;
        dist_tree_ev.block_index = block_index_;
        dist_tree_ev.tree_type = 1;
        dist_tree_ev.alphabet_size = DISTANCE_DICTIONARY_SIZE;
        for (size_t i = 0; i < DISTANCE_DICTIONARY_SIZE && i < 286; ++i)
            dist_tree_ev.code_lengths[i] = dist_dictionary_[i].length;
        notifyObservers(dist_tree_ev);
    }
    // ============================================================
    // [VIZ END]
    // ============================================================

    if (writer_.ensureSpace(huffman_tree_->getTreeSize() +
                            dist_tree_->getTreeSize())) {
        // [VIZ] Track block output start — restored from 8672f99
        block_output_start_ = writer_.getBytesWritten();
        huffman_tree_->serializeTree(writer_);
        dist_tree_->serializeTree(writer_);

        token_flush_idx_ = 0;
        deflate_state_ = DeflateState::FLUSH_TOKENS;
    } else {
        status.need_output = true;
    }
}

auto Deflate::handleFlushTokens(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    if (!use_flag_encoding_) {
        if (!nonflag_header_emitted_) {
            if (!writer_.ensureSpace(24)) {
                status.need_output = true;
                return;
            }
            writer_.writeBits(0x4E, 8);
            writer_.writeBits(static_cast<uint32_t>(offset_bits_), 8);
            writer_.writeBits(static_cast<uint32_t>(length_bits_), 8);
            nonflag_header_emitted_ = true;
        }

        while (token_flush_idx_ < token_buffer_.size()) {
            size_t lit_run = 0;
            size_t run_start = token_flush_idx_;

            while (token_flush_idx_ < token_buffer_.size() &&
                   token_buffer_[token_flush_idx_].is_literal) {
                lit_run++;
                token_flush_idx_++;
            }

            if (lit_run > 0) {
                size_t max_lit_per_chunk = (size_t{1} << length_bits_) - 1;
                size_t lit_pos = 0;
                while (lit_pos < lit_run) {
                    size_t chunk = std::min(lit_run - lit_pos, max_lit_per_chunk);
                    if (!writer_.ensureSpace(static_cast<size_t>(offset_bits_ + length_bits_ + chunk * 8))) {
                        token_flush_idx_ = run_start + lit_pos;
                        status.need_output = true;
                        return;
                    }
                    writer_.writeBits(0, static_cast<uint8_t>(offset_bits_));
                    writer_.writeBits(static_cast<uint32_t>(chunk), static_cast<uint8_t>(length_bits_));
                    for (size_t i = 0; i < chunk; i++) {
                        writer_.writeBits(token_buffer_[run_start + lit_pos + i].code, 8);
                    }
                    lit_pos += chunk;
                }
            }

            if (token_flush_idx_ < token_buffer_.size()) {
                const auto& token = token_buffer_[token_flush_idx_];
                if (!writer_.ensureSpace(static_cast<size_t>(offset_bits_ + length_bits_))) {
                    status.need_output = true;
                    return;
                }
                writer_.writeBits(token.match_dist, static_cast<uint8_t>(offset_bits_));
                writer_.writeBits(token.match_len, static_cast<uint8_t>(length_bits_));
                token_flush_idx_++;
            }
        }

        // ============================================================
        // [VIZ] Emit block boundary — restored from 8672f99
        // ============================================================
        {
            BlockBoundary bb;
            bb.block_index = block_index_;
            bb.input_start = block_input_start_;
            bb.input_bytes = input_pos_ - block_input_start_;
            bb.literal_count = block_literal_count_;
            bb.match_count = block_match_count_;
            bb.output_bytes = writer_.getBytesWritten() - block_output_start_;
            notifyObservers(bb);
            notifyBlockFinish();
        }
        // ============================================================
        // [VIZ END]
        // ============================================================

        token_buffer_.clear();

        if (is_last_chunk && lookahead_ == 0) {
            if (!writer_.ensureSpace(static_cast<size_t>(offset_bits_ + length_bits_))) {
                status.need_output = true;
                return;
            }
            writer_.writeBits(0, static_cast<uint8_t>(offset_bits_));
            writer_.writeBits(0, static_cast<uint8_t>(length_bits_));
            // [VIZ] Notify compression finish — restored from 8672f99
            notifyCompressionFinish();
            writer_.flush();
            status.done = true;
            return;
        }

        // ============================================================
        // [VIZ] Advance block index & reset tracking — restored from 8672f99
        // ============================================================
        ++block_index_;
        block_input_start_ = input_pos_;
        block_literal_count_ = 0;
        block_match_count_ = 0;
        // ============================================================
        // [VIZ END]
        // ============================================================

        deflate_state_ = DeflateState::FIND_MATCHES;
        return;
    }

    while (token_flush_idx_ < token_buffer_.size()) {
        if (!writer_.ensureSpace(48)) {
            status.need_output = true;
            return;
        }

        const auto& token = token_buffer_[token_flush_idx_];

        writeHuffmanCode(writer_, dictionary_[token.code]);

        if (!token.is_literal) {
            if (token.length_extra_bits > 0) {
                writer_.writeBits(token.length_extra_val,
                                  token.length_extra_bits);
            }

            writeHuffmanCode(writer_, dist_dictionary_[token.dist_code]);

            if (token.dist_extra_bits > 0) {
                writer_.writeBits(token.dist_extra_val, token.dist_extra_bits);
            }
        }

        token_flush_idx_++;
    }

    DEBUG_LOG("[Deflate] handleFlushTokens: writing block EOF, tokens_flushed=%zu", token_flush_idx_);
    if (!writer_.ensureSpace(16)) {
        status.need_output = true;
        return;
    }
    writeHuffmanCode(writer_, dictionary_[256]);

    // ============================================================
    // [VIZ] Emit block boundary — restored from 8672f99
    // ============================================================
    {
        BlockBoundary bb;
        bb.block_index = block_index_;
        bb.input_start = block_input_start_;
        bb.input_bytes = input_pos_ - block_input_start_;
        bb.literal_count = block_literal_count_;
        bb.match_count = block_match_count_;
        bb.output_bytes = writer_.getBytesWritten() - block_output_start_;
        notifyObservers(bb);
        notifyBlockFinish();
    }
    // ============================================================
    // [VIZ END]
    // ============================================================

    token_buffer_.clear();
    token_flush_idx_ = 0;

    if (is_last_chunk && lookahead_ == 0) {
        // [VIZ] Notify compression finish — restored from 8672f99
        notifyCompressionFinish();
        writer_.flush();
        DEBUG_LOG("[Deflate] handleFlushTokens: EOF written, done");
        status.done = true;
        return;
    }

    // ============================================================
    // [VIZ] Advance block index & reset tracking — restored from 8672f99
    // ============================================================
    ++block_index_;
    block_input_start_ = input_pos_;
    block_literal_count_ = 0;
    block_match_count_ = 0;
    // ============================================================
    // [VIZ END]
    // ============================================================

    deflate_state_ = DeflateState::FIND_MATCHES;
}

/**
 * @brief LZSS with hash table optimization and Huffman encode
 *
 * @param input
 * @return std::vector<Token>
 */
auto Deflate::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    static int handle_call = 0;
    ++handle_call;
    int inner_iter = 0;
    while (true) {
        ++inner_iter;
        if (inner_iter > 100000000) {
            DEBUG_LOG("[Deflate] SAFETY BREAK: inner_iter=%d state=%d",
                      inner_iter, (int)deflate_state_);
            // [VIZ] Notify compression finish on safety break — restored from 8672f99
            notifyCompressionFinish();
            status.done = true;
            return;
        }

        (this->*kStateHandlers[static_cast<size_t>(deflate_state_)])(
            status, is_last_chunk);

        if (status.need_input || status.need_output || status.done) {
            return;
        }
    }
}

void Deflate::getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits,
                            uint16_t& extra_val) {
    if (length <= 10) {
        code = 257 + length - 3;
        extra_bits = 0;
        extra_val = 0;
    } else if (length <= 18) {
        extra_bits = 1;
        code = 265 + (length - 11) / 2;
        extra_val = (length - 11) % 2;
    } else if (length <= 34) {
        extra_bits = 2;
        code = 269 + (length - 19) / 4;
        extra_val = (length - 19) % 4;
    } else if (length <= 66) {
        extra_bits = 3;
        code = 273 + (length - 35) / 8;
        extra_val = (length - 35) % 8;
    } else if (length <= 130) {
        extra_bits = 4;
        code = 277 + (length - 67) / 16;
        extra_val = (length - 67) % 16;
    } else if (length <= 257) {
        extra_bits = 5;
        code = 281 + (length - 131) / 32;
        extra_val = (length - 131) % 32;
    } else if (length == 258) {
        code = 285;
        extra_bits = 0;
        extra_val = 0;
    }
}

void Deflate::getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
                          uint16_t& extra_val) {
    dist -= 1;
    if (dist < 4) {
        code = dist;
        extra_bits = 0;
        extra_val = 0;
        return;
    }

    uint8_t msb = 0;
    size_t temp = dist >> 2;
    while (temp) {
        temp >>= 1;
        msb++;
    }

    extra_bits = msb;
    code = (msb << 1) + 2 + ((dist >> msb) & 1);
    extra_val = dist & ((1 << msb) - 1);
}

}  // namespace compressor::algorithm
