#include "Brotli.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

// ============================================================================
// BrotliCompress
// ============================================================================

BrotliCompress::BrotliCompress(size_t window_size, size_t min_match,
                               size_t max_chain_length)
    : WINDOW_SIZE(window_size),
      SLIDE_SIZE(window_size / 2),
      MIN_MATCH(min_match),
      MAX_MATCH(258),
      HASH_SIZE(window_size),
      MAX_CHAIN_LENGTH(max_chain_length) {
    reset();
}

auto BrotliCompress::reset(void) -> void {
    window_.resize(WINDOW_SIZE, 0);
    head_.assign(HASH_SIZE, 0xFFFF);
    prev_.assign(SLIDE_SIZE, 0xFFFF);
    cursor_ = 0;
    lookahead_ = 0;
    token_buffer_.clear();
    token_flush_idx_ = 0;
    input_pos_ = 0;
    block_index_ = 0;
    block_input_start_ = 0;
    block_literal_count_ = 0;
    block_match_count_ = 0;
    block_output_start_ = 0;
    state_ = BrotliState::FIND_MATCHES;
}

auto BrotliCompress::getHash(size_t pos) -> uint16_t {
    return ((window_[pos] << 10) ^ (window_[pos + 1] << 5) ^
            window_[pos + 2]) &
           (HASH_SIZE - 1);
}

auto BrotliCompress::fillWindow(void) -> void {
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

auto BrotliCompress::slideWindow(void) -> void {
    std::memcpy(window_.data(), window_.data() + SLIDE_SIZE, SLIDE_SIZE);
    cursor_ -= SLIDE_SIZE;

    for (size_t i = 0; i < HASH_SIZE; ++i) {
        if (head_[i] != 0xFFFF)
            head_[i] = (head_[i] >= SLIDE_SIZE) ? head_[i] - SLIDE_SIZE : 0xFFFF;
    }

    for (size_t i = 0; i < SLIDE_SIZE; ++i) {
        if (prev_[i] != 0xFFFF)
            prev_[i] = (prev_[i] >= SLIDE_SIZE) ? prev_[i] - SLIDE_SIZE : 0xFFFF;
    }
}

auto BrotliCompress::handleFindMatches(AlgorithmStatus& status,
                                       bool is_last_chunk) -> void {
    if (lookahead_ < MAX_MATCH && reader_.getRemainSize() > 0) {
        fillWindow();
    }

    if (lookahead_ == 0) {
        if (!is_last_chunk) {
            status.need_input = true;
            return;
        } else {
            state_ = BrotliState::BUILD_TREE;
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
        while (match_pos != 0xFFFF && chain_length-- > 0) {
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
        BrotliToken t;
        t.is_literal = false;
        uint8_t len_code;
        uint8_t len_extra;
        uint16_t len_val;
        getLengthCode(match_length, len_code, len_extra, len_val);
        t.length_code = len_code;
        t.length_extra_bits = len_extra;
        t.length_extra_val = len_val;

        uint8_t d_code;
        uint8_t d_extra;
        uint16_t d_val;
        getDistCode(match_distance, d_code, d_extra, d_val);
        t.dist_code = d_code;
        t.dist_extra_bits = d_extra;
        t.dist_extra_val = d_val;

        token_buffer_.push_back(t);

        notifyObservers(MatchEvent{input_pos_,
                                   static_cast<uint16_t>(match_distance),
                                   static_cast<uint16_t>(match_length), 0});

        for (size_t i = 1; i < match_length; ++i) {
            input_pos_++;
            cursor_++;
            lookahead_--;
            if (lookahead_ >= MIN_MATCH) {
                uint16_t hash_val = getHash(cursor_);
                prev_[cursor_ & (SLIDE_SIZE - 1)] = head_[hash_val];
                head_[hash_val] = static_cast<uint16_t>(cursor_);
            }
        }
        input_pos_++;
        cursor_++;
        lookahead_--;
        ++block_match_count_;
    } else {
        BrotliToken t;
        t.is_literal = true;
        t.literal = window_[cursor_];
        token_buffer_.push_back(t);

        notifyObservers(
            MatchEvent{input_pos_, 0, 0, window_[cursor_]});

        input_pos_++;
        cursor_++;
        lookahead_--;
        ++block_literal_count_;
    }

    if (token_buffer_.size() >= MAX_BLOCK_TOKENS) {
        state_ = BrotliState::BUILD_TREE;
    }
}

auto BrotliCompress::handleBuildTree(AlgorithmStatus& status,
                                     bool is_last_chunk) -> void {
    if (token_buffer_.empty() && !is_last_chunk) {
        state_ = BrotliState::FIND_MATCHES;
        return;
    }

    std::vector<uint32_t> lit_freq0(BROTLI_LIT_ALPHABET, 0);
    std::vector<uint32_t> lit_freq1(BROTLI_LIT_ALPHABET, 0);
    std::vector<uint32_t> len_freq(BROTLI_LEN_ALPHABET, 0);
    std::vector<uint32_t> dist_freq(BROTLI_DIST_ALPHABET, 0);

    bool prev_was_match = false;
    for (const auto& token : token_buffer_) {
        if (!token.is_literal) {
            len_freq[token.length_code]++;
            dist_freq[token.dist_code]++;
            prev_was_match = true;
        } else {
            if (prev_was_match) {
                lit_freq1[token.literal]++;
            } else {
                lit_freq0[token.literal]++;
            }
            prev_was_match = false;
        }
    }

    lit_freq0[256] = 1;
    lit_freq1[256] = 1;

    lit_tree0_ = std::make_unique<HuffmanTree>(lit_freq0, BROTLI_LIT_ALPHABET,
                                               BROTLI_LIT_SYMBOL_BITS);
    lit_tree1_ = std::make_unique<HuffmanTree>(lit_freq1, BROTLI_LIT_ALPHABET,
                                               BROTLI_LIT_SYMBOL_BITS);
    len_tree_ = std::make_unique<HuffmanTree>(len_freq, BROTLI_LEN_ALPHABET,
                                              BROTLI_LEN_SYMBOL_BITS);
    dist_tree_ = std::make_unique<HuffmanTree>(dist_freq, BROTLI_DIST_ALPHABET,
                                              BROTLI_DIST_SYMBOL_BITS);

    lit_dict0_ = lit_tree0_->buildDictionary();
    lit_dict1_ = lit_tree1_->buildDictionary();
    len_dict_ = len_tree_->buildDictionary();
    dist_dict_ = dist_tree_->buildDictionary();

    {
        HuffmanTreeBuilt ev;
        ev.block_index = block_index_;
        ev.tree_type = 0;
        ev.alphabet_size = BROTLI_LIT_ALPHABET;
        for (size_t i = 0; i < BROTLI_LIT_ALPHABET && i < 286; ++i)
            ev.code_lengths[i] = lit_dict0_[i].length;
        notifyObservers(ev);
    }
    {
        HuffmanTreeBuilt ev;
        ev.block_index = block_index_;
        ev.tree_type = 1;
        ev.alphabet_size = BROTLI_LIT_ALPHABET;
        for (size_t i = 0; i < BROTLI_LIT_ALPHABET && i < 286; ++i)
            ev.code_lengths[i] = lit_dict1_[i].length;
        notifyObservers(ev);
    }
    {
        HuffmanTreeBuilt ev;
        ev.block_index = block_index_;
        ev.tree_type = 2;
        ev.alphabet_size = BROTLI_LEN_ALPHABET;
        for (size_t i = 0; i < BROTLI_LEN_ALPHABET && i < 286; ++i)
            ev.code_lengths[i] = len_dict_[i].length;
        notifyObservers(ev);
    }
    {
        HuffmanTreeBuilt ev;
        ev.block_index = block_index_;
        ev.tree_type = 3;
        ev.alphabet_size = BROTLI_DIST_ALPHABET;
        for (size_t i = 0; i < BROTLI_DIST_ALPHABET && i < 286; ++i)
            ev.code_lengths[i] = dist_dict_[i].length;
        notifyObservers(ev);
    }

    size_t total_tree_bits =
        lit_tree0_->getTreeSize() + lit_tree1_->getTreeSize() +
        len_tree_->getTreeSize() + dist_tree_->getTreeSize();

    if (writer_.ensureSpace(total_tree_bits + 1)) {
        block_output_start_ = writer_.getBytesWritten();
        bool block_is_last = is_last_chunk && lookahead_ == 0;
        writer_.writeBit(block_is_last ? 1 : 0);

        lit_tree0_->serializeTree(writer_);
        lit_tree1_->serializeTree(writer_);
        len_tree_->serializeTree(writer_);
        dist_tree_->serializeTree(writer_);

        token_flush_idx_ = 0;
        state_ = BrotliState::FLUSH_TOKENS;
    } else {
        status.need_output = true;
    }
}

auto BrotliCompress::handleFlushTokens(AlgorithmStatus& status,
                                       bool is_last_chunk) -> void {
    bool prev_was_match = false;
    while (token_flush_idx_ < token_buffer_.size()) {
        if (!writer_.ensureSpace(16)) {
            status.need_output = true;
            return;
        }

        const auto& token = token_buffer_[token_flush_idx_];

        if (token.is_literal) {
            writer_.writeBit(0);  // type: literal
            size_t ctx = prev_was_match ? 1 : 0;
            writeHuffmanCode(
                writer_, (ctx == 0) ? lit_dict0_[token.literal]
                                    : lit_dict1_[token.literal]);
            prev_was_match = false;
        } else {
            writer_.writeBit(1);  // type: match
            writeHuffmanCode(writer_, len_dict_[token.length_code]);

            if (token.length_extra_bits > 0) {
                writer_.writeBits(token.length_extra_val,
                                  token.length_extra_bits);
            }

            writeHuffmanCode(writer_, dist_dict_[token.dist_code]);

            if (token.dist_extra_bits > 0) {
                writer_.writeBits(token.dist_extra_val, token.dist_extra_bits);
            }
            prev_was_match = true;
        }

        token_flush_idx_++;
    }

    // Write EOF: literal type bit + EOF symbol from appropriate context
    writer_.writeBit(0);
    if (prev_was_match) {
        writeHuffmanCode(writer_, lit_dict1_[256]);
    } else {
        writeHuffmanCode(writer_, lit_dict0_[256]);
    }

    token_buffer_.clear();

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

    if (is_last_chunk && lookahead_ == 0) {
        writer_.flush();
        status.done = true;
        return;
    }

    ++block_index_;
    block_input_start_ = input_pos_;
    block_literal_count_ = 0;
    block_match_count_ = 0;

    state_ = BrotliState::FIND_MATCHES;
}

auto BrotliCompress::handle(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    static constexpr void (BrotliCompress::*handlers[])(AlgorithmStatus&, bool) = {
        &BrotliCompress::handleFindMatches,
        &BrotliCompress::handleBuildTree,
        &BrotliCompress::handleFlushTokens};

    while (true) {
        (this->*handlers[static_cast<size_t>(state_)])(status, is_last_chunk);

        if (status.need_input || status.need_output) {
            return;
        }
        if (status.done) {
            notifyCompressionFinish();
            return;
        }
    }
}

void BrotliCompress::getLengthCode(size_t length, uint8_t& code,
                                   uint8_t& extra_bits, uint16_t& extra_val) {
    if (length <= 10) {
        code = static_cast<uint8_t>(length - 3);
        extra_bits = 0;
        extra_val = 0;
    } else if (length <= 18) {
        extra_bits = 1;
        code = 8 + static_cast<uint8_t>((length - 11) / 2);
        extra_val = (length - 11) % 2;
    } else if (length <= 34) {
        extra_bits = 2;
        code = 12 + static_cast<uint8_t>((length - 19) / 4);
        extra_val = (length - 19) % 4;
    } else if (length <= 66) {
        extra_bits = 3;
        code = 16 + static_cast<uint8_t>((length - 35) / 8);
        extra_val = (length - 35) % 8;
    } else if (length <= 130) {
        extra_bits = 4;
        code = 20 + static_cast<uint8_t>((length - 67) / 16);
        extra_val = (length - 67) % 16;
    } else if (length <= 257) {
        extra_bits = 5;
        code = 24 + static_cast<uint8_t>((length - 131) / 32);
        extra_val = (length - 131) % 32;
    } else {
        code = 28;
        extra_bits = 0;
        extra_val = 0;
    }
}

void BrotliCompress::getDistCode(size_t dist, uint8_t& code,
                                 uint8_t& extra_bits, uint16_t& extra_val) {
    dist -= 1;
    if (dist < 4) {
        code = static_cast<uint8_t>(dist);
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
    extra_val = static_cast<uint16_t>(dist & ((1 << msb) - 1));
}

// ============================================================================
// BrotliDecompress
// ============================================================================

BrotliDecompress::BrotliDecompress() { reset(); }

auto BrotliDecompress::reset(void) -> void {
    output_buf_.clear();
    destroyTree(lit_root0_);
    destroyTree(lit_root1_);
    destroyTree(len_root_);
    destroyTree(dist_root_);
    lit_root0_ = nullptr;
    lit_root1_ = nullptr;
    len_root_ = nullptr;
    dist_root_ = nullptr;
    lit_cursor_ = nullptr;
    prev_was_match_ = false;
    decode_state_ = DecodeState::READ_LIT_TREE0;
    pending_length_ = 0;
    pending_dist_ = 0;
}

void BrotliDecompress::destroyTree(node* n) {
    if (!n) return;
    destroyTree(n->left);
    destroyTree(n->right);
    delete n;
}

auto BrotliDecompress::readHuffmanTree(node*& root, size_t symbol_bits)
    -> bool {
    if (!reader_.ensureBits(1)) return false;
    bool is_leaf = (reader_.readBit() == 1);

    if (is_leaf) {
        if (!reader_.ensureBits(static_cast<uint8_t>(symbol_bits))) return false;
        uint16_t sym = static_cast<uint16_t>(
            reader_.readBits(static_cast<uint8_t>(symbol_bits)));
        root = new node(sym, 0);
    } else {
        root = new node(static_cast<uint16_t>(0), static_cast<uint32_t>(0));
        if (!readHuffmanTree(root->left, symbol_bits)) return false;
        if (!readHuffmanTree(root->right, symbol_bits)) return false;
    }
    return true;
}

void BrotliDecompress::decodeLengthCode(uint16_t symbol, uint16_t& length,
                                        uint8_t& extra_bits) {
    if (symbol >= BROTLI_LEN_ALPHABET) {
        length = 0;
        extra_bits = 0;
        return;
    }
    length = static_cast<uint16_t>(LENGTH_BASES[symbol]);
    extra_bits = LENGTH_EXTRA[symbol];
}

void BrotliDecompress::decodeDistCode(uint16_t symbol, uint16_t& dist,
                                      uint8_t& extra_bits) {
    if (symbol >= BROTLI_DIST_ALPHABET) {
        dist = 0;
        extra_bits = 0;
        return;
    }
    dist = static_cast<uint16_t>(DIST_BASES[symbol]);
    extra_bits = DIST_EXTRA[symbol];
}

auto BrotliDecompress::handle(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    while (true) {
        if (decode_state_ == DecodeState::READ_LIT_TREE0) {
            destroyTree(lit_root0_);
            lit_root0_ = nullptr;

            if (!reader_.ensureBits(1)) {
                status.need_input = true;
                return;
            }
            reader_.readBit();  // consume is_last flag, not used in this simple impl

            if (!readHuffmanTree(lit_root0_, BROTLI_LIT_SYMBOL_BITS)) {
                status.need_input = true;
                return;
            }
            decode_state_ = DecodeState::READ_LIT_TREE1;
            continue;
        }

        if (decode_state_ == DecodeState::READ_LIT_TREE1) {
            if (!readHuffmanTree(lit_root1_, BROTLI_LIT_SYMBOL_BITS)) {
                status.need_input = true;
                return;
            }
            decode_state_ = DecodeState::READ_LEN_TREE;
            continue;
        }

        if (decode_state_ == DecodeState::READ_LEN_TREE) {
            if (!readHuffmanTree(len_root_, BROTLI_LEN_SYMBOL_BITS)) {
                status.need_input = true;
                return;
            }
            decode_state_ = DecodeState::READ_DIST_TREE;
            continue;
        }

        if (decode_state_ == DecodeState::READ_DIST_TREE) {
            if (!readHuffmanTree(dist_root_, BROTLI_DIST_SYMBOL_BITS)) {
                status.need_input = true;
                return;
            }
            decode_state_ = DecodeState::DECODE_TOKENS;
            continue;
        }

        if (decode_state_ == DecodeState::DECODE_TOKENS) {
            if (!reader_.ensureBits(1)) {
                status.need_input = true;
                return;
            }
            bool is_match = (reader_.readBit() == 1);

            if (!is_match) {
                // Literal: read from context-aware literal tree
                node* active_tree = prev_was_match_ ? lit_root1_ : lit_root0_;
                lit_cursor_ = active_tree;

                while (!lit_cursor_->isLeaf()) {
                    if (!reader_.ensureBits(1)) {
                        status.need_input = true;
                        return;
                    }
                    bool bit = (reader_.readBit() == 1);
                    lit_cursor_ = bit ? lit_cursor_->right : lit_cursor_->left;
                }

                uint16_t symbol = lit_cursor_->symbol;

                if (symbol == 256) {
                    // EOF — flush and done
                    for (size_t i = 0; i < output_buf_.size(); i++) {
                        if (!writer_.ensureSpace(1)) {
                            status.need_output = true;
                            return;
                        }
                        writer_.writeBits(output_buf_[i], 8);
                    }
                    output_buf_.clear();
                    status.done = true;
                    return;
                }

                output_buf_.push_back(static_cast<uint8_t>(symbol));
                prev_was_match_ = false;
                continue;
            }

            // Match: read length code from length tree
            {
                len_cursor_ = len_root_;
                while (!len_cursor_->isLeaf()) {
                    if (!reader_.ensureBits(1)) {
                        status.need_input = true;
                        return;
                    }
                    bool bit = (reader_.readBit() == 1);
                    len_cursor_ = bit ? len_cursor_->right : len_cursor_->left;
                }

                uint16_t len_sym = len_cursor_->symbol;
                uint16_t base_len = 0;
                uint8_t len_extra = 0;
                decodeLengthCode(len_sym, base_len, len_extra);
                pending_length_ = base_len;

                if (len_extra > 0) {
                    if (!reader_.ensureBits(len_extra)) {
                        status.need_input = true;
                        return;
                    }
                    pending_length_ += static_cast<uint16_t>(reader_.readBits(len_extra));
                }
            }

            // Read distance code from distance tree
            {
                node* d_cursor = dist_root_;
                while (!d_cursor->isLeaf()) {
                    if (!reader_.ensureBits(1)) {
                        status.need_input = true;
                        return;
                    }
                    bool bit = (reader_.readBit() == 1);
                    d_cursor = bit ? d_cursor->right : d_cursor->left;
                }

                uint16_t dist_sym = d_cursor->symbol;
                uint16_t base_dist = 0;
                uint8_t dist_extra = 0;
                decodeDistCode(dist_sym, base_dist, dist_extra);
                pending_dist_ = base_dist;

                if (dist_extra > 0) {
                    if (!reader_.ensureBits(dist_extra)) {
                        status.need_input = true;
                        return;
                    }
                    pending_dist_ += static_cast<uint16_t>(reader_.readBits(dist_extra));
                }
            }

            prev_was_match_ = true;
            decode_state_ = DecodeState::COPY_MATCH;
            continue;
        }

        if (decode_state_ == DecodeState::COPY_MATCH) {
            if (pending_dist_ > output_buf_.size()) {
                status.done = false;
                return;
            }
            size_t src_start = output_buf_.size() - pending_dist_;
            for (size_t i = 0; i < pending_length_; i++) {
                output_buf_.push_back(output_buf_[src_start + i]);
            }
            decode_state_ = DecodeState::DECODE_TOKENS;
            continue;
        }
    }
}

}  // namespace compressor::algorithm
