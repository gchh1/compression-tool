#include "MyFlate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

MyFlate::MyFlate(size_t search_size, size_t lookahead_size, size_t min_match,
                 size_t max_chain_length, size_t dp_sub_match_max)
    : SEARCH_SIZE(search_size),
      LOOKAHEAD_SIZE(lookahead_size),
      MIN_MATCH(min_match),
      MAX_CHAIN_LENGTH(max_chain_length),
      DP_SUB_MATCH_MAX(dp_sub_match_max) {
    reset();
}

auto MyFlate::reset(void) -> void {
    window_.clear();
    data_len_ = 0;
    cursor_ = 0;
    token_buffer_.clear();
    token_flush_idx_ = 0;
    state_ = MyFlateState::FIND_MATCHES;
    head_.clear();
    prev_.clear();
}

auto MyFlate::handleFindMatches(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    if (data_len_ == 0) {
        size_t remain = reader_.getRemainSize();
        if (remain > 0) {
            size_t old_len = window_.size();
            window_.resize(old_len + remain);
            size_t copied = reader_.readBytes(window_.data() + old_len, remain);
            data_len_ = window_.size();
        }
    }

    if (!is_last_chunk && data_len_ < MAX_BLOCK_TOKENS * 4) {
        status.need_input = true;
        return;
    }

    if (cursor_ >= data_len_) {
        state_ = MyFlateState::BUILD_TREE;
        return;
    }

    size_t chunk_start = cursor_;
    size_t chunk_len = data_len_ - cursor_;

    head_.assign(SEARCH_SIZE, NULL_PTR);
    prev_.assign(SEARCH_SIZE, NULL_PTR);

    struct MatchInfo {
        size_t length;
        size_t distance;
    };

    std::vector<MatchInfo> best_matches(chunk_len, {0, 0});

    for (size_t i = 0; i < chunk_len; i++) {
        size_t pos = chunk_start + i;

        if (i + 2 < chunk_len) {
            uint16_t hash_val = getHash(pos);

            uint32_t match_pos = head_[hash_val];
            size_t best_len = 0;
            size_t best_dist = 0;
            size_t chain_count = MAX_CHAIN_LENGTH;

            while (match_pos != NULL_PTR && chain_count-- > 0) {
                size_t distance = pos - match_pos;
                if (distance > SEARCH_SIZE || distance == 0) break;

                size_t max_possible = std::min(MAX_MATCH, chunk_len - i);
                size_t match_len = 0;
                while (match_len < max_possible &&
                       window_[pos + match_len] == window_[match_pos + match_len]) {
                    match_len++;
                }

                if (match_len > best_len) {
                    best_len = match_len;
                    best_dist = distance;
                    if (match_len == max_possible) break;
                }

                match_pos = prev_[match_pos & (SEARCH_SIZE - 1)];
            }

            if (best_len >= MIN_MATCH) {
                best_matches[i] = {best_len, best_dist};
            }

            prev_[pos & (SEARCH_SIZE - 1)] = head_[hash_val];
            head_[hash_val] = static_cast<uint32_t>(pos);
        }
    }

    struct DPNode {
        size_t cost;
        size_t match_len;
        size_t match_dist;
    };

    std::vector<DPNode> dp(chunk_len + 1);
    dp[0] = {0, 0, 0};
    for (size_t i = 1; i <= chunk_len; i++) {
        dp[i] = {static_cast<size_t>(-1), 0, 0};
    }

    for (size_t i = 0; i < chunk_len; i++) {
        if (dp[i].cost + 1 < dp[i + 1].cost) {
            dp[i + 1] = {dp[i].cost + 1, 0, 0};
        }

        if (best_matches[i].length >= MIN_MATCH) {
            size_t len = best_matches[i].length;
            if (dp[i].cost + 1 < dp[i + len].cost) {
                dp[i + len] = {dp[i].cost + 1, len, best_matches[i].distance};
            }

            for (size_t slen = MIN_MATCH; slen < len && slen <= DP_SUB_MATCH_MAX; slen++) {
                if (dp[i].cost + 1 < dp[i + slen].cost) {
                    dp[i + slen] = {dp[i].cost + 1, slen, best_matches[i].distance};
                }
            }
        }
    }

    struct TokenDecision {
        size_t match_len;
        size_t match_dist;
    };

    std::vector<TokenDecision> decisions;
    decisions.reserve(chunk_len);

    size_t ti = chunk_len;
    while (ti > 0) {
        decisions.push_back({dp[ti].match_len, dp[ti].match_dist});
        ti -= (dp[ti].match_len > 0) ? dp[ti].match_len : 1;
    }
    std::reverse(decisions.begin(), decisions.end());

    size_t pos = chunk_start;
    for (const auto& dec : decisions) {
        if (dec.match_len >= MIN_MATCH) {
            MyFlateToken tok;
            tok.is_literal = false;
            getLengthCode(dec.match_len, tok.code, tok.length_extra_bits, tok.length_extra_val);
            getDistCode(dec.match_dist, tok.dist_code, tok.dist_extra_bits, tok.dist_extra_val);
            token_buffer_.push_back(tok);
            pos += dec.match_len;
        } else {
            MyFlateToken tok;
            tok.is_literal = true;
            tok.code = window_[pos];
            token_buffer_.push_back(tok);
            pos++;
        }
    }
    cursor_ = pos;

    state_ = MyFlateState::BUILD_TREE;
}

auto MyFlate::handleBuildTree(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    if (token_buffer_.empty()) {
        if (cursor_ >= data_len_ && is_last_chunk) {
            writer_.flush();
            status.done = true;
            return;
        }
        state_ = MyFlateState::FIND_MATCHES;
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
    freq_map[256] = 1;

    huffman_tree_ = std::make_unique<HuffmanTree>(
        freq_map, DEFLATE_ALPHABET_SIZE, DEFLATE_SYMBOL_BITS);
    dist_tree_ = std::make_unique<HuffmanTree>(
        dist_freq, DISTANCE_DICTIONARY_SIZE, DISTANCE_SYMBOL_BITS);

    dictionary_ = huffman_tree_->buildDictionary();
    dist_dictionary_ = dist_tree_->buildDictionary();

    if (writer_.ensureSpace(huffman_tree_->getTreeSize() +
                            dist_tree_->getTreeSize())) {
        huffman_tree_->serializeTree(writer_);
        dist_tree_->serializeTree(writer_);

        token_flush_idx_ = 0;
        state_ = MyFlateState::FLUSH_TOKENS;
    } else {
        status.need_output = true;
    }
}

auto MyFlate::handleFlushTokens(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    while (token_flush_idx_ < token_buffer_.size()) {
        if (!writer_.ensureSpace(6)) {
            status.need_output = true;
            return;
        }

        const auto& token = token_buffer_[token_flush_idx_];

        const auto& main_code = dictionary_[token.code];
        for (int i = main_code.length - 1; i >= 0; i--) {
            writer_.writeBit((main_code.code >> i) & 1);
        }

        if (!token.is_literal) {
            if (token.length_extra_bits > 0) {
                writer_.writeBits(token.length_extra_val,
                                  token.length_extra_bits);
            }

            const auto& dist_code = dist_dictionary_[token.dist_code];
            for (int i = dist_code.length - 1; i >= 0; i--) {
                writer_.writeBit((dist_code.code >> i) & 1);
            }

            if (token.dist_extra_bits > 0) {
                writer_.writeBits(token.dist_extra_val, token.dist_extra_bits);
            }
        }

        token_flush_idx_++;
    }

    token_buffer_.clear();

    if (is_last_chunk && cursor_ >= data_len_) {
        const auto& eof_code = dictionary_[256];
        for (int i = eof_code.length - 1; i >= 0; i--) {
            writer_.writeBit((eof_code.code >> i) & 1);
        }
        writer_.flush();
        status.done = true;
        return;
    }

    state_ = MyFlateState::FIND_MATCHES;
}

auto MyFlate::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        (this->*kStateHandlers[static_cast<size_t>(state_)])(status,
                                                             is_last_chunk);

        if (status.need_input || status.need_output || status.done) {
            return;
        }
    }
}

void MyFlate::getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits,
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

void MyFlate::getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
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
