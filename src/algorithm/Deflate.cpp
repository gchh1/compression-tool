// Include lib here

#include "Deflate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "BitWriter.hpp"
#include "Deflate.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

/**
 * @brief Construct a new Deflate:: Deflate object
 *
 */
Deflate::Deflate() { reset(); }

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
    bfinal_ = false;

    block_profile_.clear();

    deflate_state_ = DeflateState::FIND_MATCHES;
}

auto Deflate::getBlockProfile() -> std::optional<BlockProfile> {
    if (block_profile_.empty()) return std::nullopt;
    BlockProfile p;
    p.blocks = std::move(block_profile_);
    block_profile_.clear();
    return p;
}

auto Deflate::fillWindow(void) -> void {
    while (reader_.getRemainSize() > 0) {
        if (cursor_ >= SLIDE_SIZE && cursor_ + lookahead_ >= WINDOW_SIZE) {
            slideWindow();
        }

        size_t space_left = WINDOW_SIZE - (cursor_ + lookahead_);
        size_t remain = reader_.getRemainSize();
        size_t to_copy = std::min(space_left, remain);

        if (to_copy == 0) break;

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

        prev_[cursor_ & (SLIDE_SIZE - 1)] = match_pos;
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
            match_pos = prev_[match_pos & (SLIDE_SIZE - 1)];
        }
    }

    if (match_length >= MIN_MATCH) {
        Token t;
        t.is_literal = false;
        getLengthCode(match_length, t.code, t.length_extra_bits,
                      t.length_extra_val);
        getDistCode(match_distance, t.dist_code, t.dist_extra_bits,
                    t.dist_extra_val);
        token_buffer_.push_back(t);

        for (size_t i = 1; i < match_length; ++i) {
            cursor_++;
            lookahead_--;
            if (lookahead_ >= MIN_MATCH) {
                uint16_t hash_val = getHash(cursor_);
                prev_[cursor_ & (SLIDE_SIZE - 1)] = head_[hash_val];
                head_[hash_val] = static_cast<uint16_t>(cursor_);
            }
        }
        cursor_++;
        lookahead_--;
    } else {
        Token t;
        t.is_literal = true;
        t.code = window_[cursor_];
        token_buffer_.push_back(t);
        cursor_++;
        lookahead_--;
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

    // ---- record block profile ----
    {
        BlockInfo info;
        info.block_index = block_profile_.size();
        info.ll_tree_bits = huffman_tree_->getTreeSize();
        info.dist_tree_bits = dist_tree_->getTreeSize();

        // Length base table (matching Inflate::kLengthBase)
        static constexpr uint16_t kLenBase[] = {
            3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        for (const auto& t : token_buffer_) {
            if (t.is_literal) {
                info.literal_count++;
                info.output_bytes += 1;
            } else {
                info.match_count++;
                info.output_bytes +=
                    kLenBase[t.code - 257] + t.length_extra_val;
            }
        }
        info.ll_code_lengths = huffman_tree_->getCodeLengths();
        info.dist_code_lengths = dist_tree_->getCodeLengths();
        block_profile_.push_back(std::move(info));
    }

    bfinal_ = is_last_chunk && (lookahead_ == 0);

    if (writer_.ensureSpace(huffman_tree_->getTreeSize() +
                            dist_tree_->getTreeSize() + 3)) {
        // Block header: BFINAL (1 bit) + BTYPE = 2 (2 bits)
        writer_.writeBit(bfinal_ ? 1 : 0);
        writer_.writeBits(2, 2);

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
    while (token_flush_idx_ < token_buffer_.size()) {
        if (!writer_.ensureSpace(48)) {
            status.need_output = true;
            return;
        }

        const auto& token = token_buffer_[token_flush_idx_];

        const auto& main_code = dictionary_[token.code];
        writer_.writeBits(main_code.code, main_code.length);

        if (!token.is_literal) {
            if (token.length_extra_bits > 0) {
                writer_.writeBits(token.length_extra_val,
                                  token.length_extra_bits);
            }

            const auto& dist_code = dist_dictionary_[token.dist_code];
            writer_.writeBits(dist_code.code, dist_code.length);

            if (token.dist_extra_bits > 0) {
                writer_.writeBits(token.dist_extra_val, token.dist_extra_bits);
            }
        }

        token_flush_idx_++;
    }

    // Write EOF to terminate the block
    {
        const auto& eof_code = dictionary_[256];
        if (!writer_.ensureSpace(eof_code.length)) {
            status.need_output = true;
            return;
        }
        writer_.writeBits(eof_code.code, eof_code.length);
    }
    token_buffer_.clear();

    if (bfinal_) {
        writer_.flush();
        status.done = true;
        return;
    }

    deflate_state_ = DeflateState::FIND_MATCHES;
}

/**
 * @brief LZSS with hash table optimization and Huffman encode
 *
 * @param input
 * @return std::vector<Token>
 */
auto Deflate::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
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
