#include "DPFlate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "BitWriter.hpp"
#include "HuffmanTree.hpp"
#include "KMPMatcher.hpp"
#include "LzStyleDpCell.hpp"

namespace compressor::algorithm {

DPFlate::DPFlate(size_t search_size, size_t lookahead_size, size_t min_match,
                 size_t dp_top, size_t dp_sub_match_max)
    : SEARCH_SIZE(search_size),
      LOOKAHEAD_SIZE(lookahead_size),
      MIN_MATCH(min_match),
      DP_TOP(dp_top),
      DP_SUB_MATCH_MAX(dp_sub_match_max) {
    reset();
}

auto DPFlate::reset(void) -> void {
    input_buffer_.clear();
    token_buffer_.clear();
    huffman_tree_.reset();
    dist_tree_.reset();
    dictionary_.clear();
    dist_dictionary_.clear();
    token_flush_idx_ = 0;
    state_ = DPFlateState::COLLECT_INPUT;
}

auto DPFlate::handleCollectInput(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    size_t remain = reader_.getRemainSize();
    if (remain > 0) {
        size_t old_len = input_buffer_.size();
        input_buffer_.resize(old_len + remain);
        size_t copied =
            reader_.readBytes(input_buffer_.data() + old_len, remain);
        if (copied < remain) {
            input_buffer_.resize(old_len + copied);
        }
    }

    if (!is_last_chunk) {
        status.need_input = true;
        return;
    }

    state_ = DPFlateState::BUILD_TREE;
}

auto DPFlate::handleBuildTree(AlgorithmStatus& status, bool is_last_chunk)
    -> void {
    if (input_buffer_.empty()) {
        status.done = true;
        return;
    }

    size_t in_len = input_buffer_.size();

    std::vector<LzStyleDpCell> dp(in_len + 1);
    for (auto& n : dp) n.token_count = LzStyleDpCell::kUnreachable;
    dp[0].token_count = 0;

    std::vector<size_t> head(SEARCH_SIZE, SIZE_MAX);
    std::vector<size_t> prev(in_len, SIZE_MAX);

    for (size_t pos = 0; pos < in_len; pos++) {
        if (dp[pos].token_count == LzStyleDpCell::kUnreachable) continue;

        // Literal transition (cost 1 token)
        if (dp[pos].token_count + 1 < dp[pos + 1].token_count) {
            dp[pos + 1].token_count = dp[pos].token_count + 1;
            dp[pos + 1].match_offset = 0;
            dp[pos + 1].match_length = 0;
            dp[pos + 1].predecessor = pos;
        }

        const size_t remain = in_len - pos;
        const size_t look_len = (remain > LOOKAHEAD_SIZE) ? LOOKAHEAD_SIZE : remain;

        if (look_len >= MIN_MATCH) {
            struct MatchRes { size_t offset; size_t length; };
            std::vector<MatchRes> match_results;

            if (match_engine_ == 0) {
                size_t search_len = (pos > SEARCH_SIZE) ? SEARCH_SIZE : pos;
                size_t search_start = pos - search_len;
                auto kmp_results = kmpSearch(
                    input_buffer_.begin() + search_start, search_len,
                    input_buffer_.begin() + pos, look_len, DP_TOP, MIN_MATCH);
                for (auto& kr : kmp_results) {
                    match_results.push_back({kr.offset, kr.length});
                }
            } else {
                uint16_t hash_val = ((input_buffer_[pos] << 10) ^ (input_buffer_[pos + 1] << 5) ^ input_buffer_[pos + 2]) & (SEARCH_SIZE - 1);
                size_t match_pos = head[hash_val];
                prev[pos] = match_pos;
                head[hash_val] = pos;

                size_t chain_length = DP_TOP * 8; // Evaluate top N hash chain hits
                while (match_pos != SIZE_MAX && chain_length-- > 0) {
                    size_t dist = pos - match_pos;
                    if (dist > SEARCH_SIZE || dist == 0) break;

                    size_t max_len = std::min(static_cast<size_t>(258), remain);
                    size_t match_len = 0;
                    while (match_len < max_len &&
                           input_buffer_[pos + match_len] ==
                               input_buffer_[match_pos + match_len]) {
                        match_len++;
                    }

                    if (match_len >= MIN_MATCH) {
                        match_results.push_back({dist, match_len});
                    }
                    match_pos = prev[match_pos];
                }
            }

            for (auto& mr : match_results) {
                if (dp[pos].token_count + 1 < dp[pos + mr.length].token_count) {
                    dp[pos + mr.length].token_count = dp[pos].token_count + 1;
                    dp[pos + mr.length].match_offset = mr.offset;
                    dp[pos + mr.length].match_length = mr.length;
                    dp[pos + mr.length].predecessor = pos;
                }
            }
        }
    }

    std::vector<Token> tokens;
    size_t cur = in_len;
    while (cur > 0) {
        size_t prev_pos = dp[cur].predecessor;
        size_t len = dp[cur].match_length;
        size_t dist = dp[cur].match_offset;

        Token t;
        if (len == 0) {
            t.is_literal = true;
            t.code = input_buffer_[prev_pos];
        } else {
            t.is_literal = false;
            getLengthCode(len, t.code, t.length_extra_bits, t.length_extra_val);
            getDistCode(dist, t.dist_code, t.dist_extra_bits, t.dist_extra_val);
        }
        tokens.push_back(t);
        cur = prev_pos;
    }
    std::reverse(tokens.begin(), tokens.end());
    token_buffer_ = std::move(tokens);

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
        state_ = DPFlateState::FLUSH_TOKENS;
    } else {
        status.need_output = true;
    }
}

auto DPFlate::handleFlushTokens(AlgorithmStatus& status, bool is_last_chunk)
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

    const auto& eof_code = dictionary_[256];
    for (int i = eof_code.length - 1; i >= 0; i--) {
        writer_.writeBit((eof_code.code >> i) & 1);
    }

    writer_.flush();
    status.done = true;
}

auto DPFlate::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        (this->*kStateHandlers[static_cast<size_t>(state_)])(status,
                                                             is_last_chunk);

        if (status.need_input || status.need_output || status.done) {
            return;
        }
    }
}

void DPFlate::getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits,
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

void DPFlate::getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
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