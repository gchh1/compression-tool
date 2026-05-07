#include "CrazyFlate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor {
namespace algorithm {

size_t CrazyFlate::calcBits(size_t max_val) {
    if (max_val <= 1) return 1;
    size_t bits = 0;
    size_t v = max_val;
    while (v > 0) {
        bits++;
        v >>= 1;
    }
    return bits;
}

uint16_t CrazyFlate::getHash(const std::vector<uint8_t>& data, size_t pos) {
    if (pos + 2 >= data.size()) return 0;
    return static_cast<uint16_t>(
        ((static_cast<uint16_t>(data[pos]) << 8) ^
         (static_cast<uint16_t>(data[pos + 1]) << 4) ^
         static_cast<uint16_t>(data[pos + 2])) & (HASH_SIZE_ - 1));
}

static constexpr size_t LIT_LEN_ALPHABET_SIZE = 286;
static constexpr uint8_t LIT_LEN_SYMBOL_BITS = 9;
static constexpr size_t DIST_ALPHABET_SIZE = 30;
static constexpr uint8_t DIST_SYMBOL_BITS = 5;

static const size_t LENGTH_BASES[] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t LENGTH_EXTRA[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const size_t DIST_BASES[] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t DIST_EXTRA[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits, uint16_t& extra_val) {
    for (int i = 28; i >= 0; i--) {
        if (length >= LENGTH_BASES[i]) {
            code = 257 + i;
            extra_bits = LENGTH_EXTRA[i];
            extra_val = static_cast<uint16_t>(length - LENGTH_BASES[i]);
            return;
        }
    }
    code = 257;
    extra_bits = 0;
    extra_val = 0;
}

static void getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits, uint16_t& extra_val) {
    for (int i = 29; i >= 0; i--) {
        if (dist >= DIST_BASES[i]) {
            code = static_cast<uint8_t>(i);
            extra_bits = DIST_EXTRA[i];
            extra_val = static_cast<uint16_t>(dist - DIST_BASES[i]);
            return;
        }
    }
    code = 0;
    extra_bits = 0;
    extra_val = 0;
}

static void destroyTree(node* n) {
    if (!n) return;
    destroyTree(n->left);
    destroyTree(n->right);
    delete n;
}

static node* readTree(utils::BitReader& reader, size_t symbol_bits, int depth = 0) {
    if (depth > 512 || !reader.ensureBits(1)) return nullptr;
    uint64_t bit = reader.readBit();
    if (bit == 1) {
        if (!reader.ensureBits(symbol_bits)) return nullptr;
        uint64_t sym = reader.readBits(symbol_bits);
        return new node(static_cast<uint16_t>(sym), 0);
    }
    node* left = readTree(reader, symbol_bits, depth + 1);
    node* right = readTree(reader, symbol_bits, depth + 1);
    if (!left || !right) {
        destroyTree(left);
        destroyTree(right);
        return nullptr;
    }
    return new node(left, right);
}

struct CrazyFlateToken {
    bool is_literal;
    uint16_t code;
    uint8_t length_extra_bits;
    uint16_t length_extra_val;
    uint8_t dist_code;
    uint8_t dist_extra_bits;
    uint16_t dist_extra_val;
};

struct MatchInfo {
    size_t length;
    size_t distance;
};

struct DPNode {
    size_t cost;
    size_t match_len;
    size_t match_dist;
};

std::vector<uint8_t> CrazyFlate::compress(
    const std::vector<uint8_t>& input,
    size_t search_size,
    size_t lookahead_size,
    size_t min_match,
    size_t dp_depth,
    size_t max_chain_length) {
    if (input.empty()) return {};

    if (search_size > MAX_SEARCH_SIZE_) search_size = MAX_SEARCH_SIZE_;
    if (lookahead_size > MAX_LOOKAHEAD_SIZE_) lookahead_size = MAX_LOOKAHEAD_SIZE_;
    if (min_match < 3) min_match = 3;
    if (min_match > 6) min_match = 6;
    if (dp_depth < 3) dp_depth = 3;
    if (dp_depth > lookahead_size) dp_depth = lookahead_size;
    if (max_chain_length == 0) max_chain_length = 1;

    size_t in_len = input.size();

    std::vector<uint32_t> head(HASH_SIZE_, NULL_PTR_);
    std::vector<uint32_t> prev(search_size, NULL_PTR_);

    std::vector<MatchInfo> best_matches(in_len, {0, 0});

    for (size_t pos = 0; pos < in_len; pos++) {
        if (pos + 2 < in_len) {
            uint16_t hash_val = getHash(input, pos);

            uint32_t match_pos = head[hash_val];
            size_t best_len = 0;
            size_t best_dist = 0;
            size_t chain_count = max_chain_length;

            while (match_pos != NULL_PTR_ && chain_count-- > 0) {
                size_t distance = pos - match_pos;
                if (distance > search_size || distance == 0) break;

                size_t max_possible = std::min(lookahead_size, in_len - pos);
                size_t match_len = 0;
                while (match_len < max_possible &&
                       input[match_pos + match_len] == input[pos + match_len]) {
                    match_len++;
                }

                if (match_len > best_len) {
                    best_len = match_len;
                    best_dist = distance;
                    if (match_len == max_possible) break;
                }

                match_pos = prev[match_pos % search_size];
            }

            if (best_len >= min_match) {
                best_matches[pos] = {best_len, best_dist};
            }

            prev[pos % search_size] = head[hash_val];
            head[hash_val] = static_cast<uint32_t>(pos);
        }
    }

    std::vector<DPNode> dp(in_len + 1);
    dp[0] = {0, 0, 0};
    for (size_t i = 1; i <= in_len; i++) {
        dp[i] = {static_cast<size_t>(-1), 0, 0};
    }

    for (size_t i = 0; i < in_len; i++) {
        if (dp[i].cost + 1 < dp[i + 1].cost) {
            dp[i + 1] = {dp[i].cost + 1, 0, 0};
        }

        if (best_matches[i].length >= min_match) {
            size_t len = best_matches[i].length;
            if (dp[i].cost + 1 < dp[i + len].cost) {
                dp[i + len] = {dp[i].cost + 1, len, best_matches[i].distance};
            }

            for (size_t slen = min_match; slen < len && slen <= dp_depth; slen++) {
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
    decisions.reserve(in_len);

    size_t ti = in_len;
    while (ti > 0) {
        decisions.push_back({dp[ti].match_len, dp[ti].match_dist});
        ti -= (dp[ti].match_len > 0) ? dp[ti].match_len : 1;
    }
    std::reverse(decisions.begin(), decisions.end());

    std::vector<CrazyFlateToken> tokens;
    tokens.reserve(decisions.size());

    size_t literal_pos = 0;
    for (const auto& dec : decisions) {
        if (dec.match_len >= min_match) {
            CrazyFlateToken tok;
            tok.is_literal = false;
            getLengthCode(dec.match_len, tok.code, tok.length_extra_bits, tok.length_extra_val);
            getDistCode(dec.match_dist, tok.dist_code, tok.dist_extra_bits, tok.dist_extra_val);
            tokens.push_back(tok);
            literal_pos += dec.match_len;
        } else {
            CrazyFlateToken tok;
            tok.is_literal = true;
            tok.code = input[literal_pos];
            tokens.push_back(tok);
            literal_pos++;
        }
    }

    std::vector<uint32_t> lit_len_freq(LIT_LEN_ALPHABET_SIZE, 0);
    std::vector<uint32_t> dist_freq(DIST_ALPHABET_SIZE, 0);
    for (const auto& tok : tokens) {
        lit_len_freq[tok.code]++;
        if (!tok.is_literal) {
            dist_freq[tok.dist_code]++;
        }
    }
    lit_len_freq[256] = 1;

    HuffmanTree lit_len_tree(lit_len_freq, LIT_LEN_ALPHABET_SIZE, LIT_LEN_SYMBOL_BITS);
    HuffmanTree dist_tree(dist_freq, DIST_ALPHABET_SIZE, DIST_SYMBOL_BITS);

    auto lit_len_dict = lit_len_tree.buildDictionary();
    auto dist_dict = dist_tree.buildDictionary();

    size_t out_capacity = in_len + in_len / 2 + 4096;
    std::vector<uint8_t> output(out_capacity, 0);
    utils::BitWriter writer{std::span<uint8_t>(output)};

    writer.writeBits(in_len, 32);
    writer.writeBits(min_match, 5);

    lit_len_tree.serializeTree(writer);
    dist_tree.serializeTree(writer);

    for (const auto& tok : tokens) {
        const auto& main_code = lit_len_dict[tok.code];
        for (int i = main_code.length - 1; i >= 0; i--) {
            writer.writeBit((main_code.code >> i) & 1);
        }

        if (!tok.is_literal) {
            if (tok.length_extra_bits > 0) {
                writer.writeBits(tok.length_extra_val, tok.length_extra_bits);
            }

            const auto& dc = dist_dict[tok.dist_code];
            for (int i = dc.length - 1; i >= 0; i--) {
                writer.writeBit((dc.code >> i) & 1);
            }

            if (tok.dist_extra_bits > 0) {
                writer.writeBits(tok.dist_extra_val, tok.dist_extra_bits);
            }
        }
    }

    const auto& eof_code = lit_len_dict[256];
    for (int i = eof_code.length - 1; i >= 0; i--) {
        writer.writeBit((eof_code.code >> i) & 1);
    }

    size_t total_bytes = writer.flush();
    output.resize(total_bytes);
    return output;
}

std::vector<uint8_t> CrazyFlate::decompress(
    const std::vector<uint8_t>& input) {
    if (input.size() < 6) return {};

    utils::BitReader reader{std::span<const uint8_t>(input)};

    uint64_t original_size = reader.readBits(32);
    uint64_t min_match = reader.readBits(5);

    if (original_size == 0) return {};

    node* lit_len_root = readTree(reader, LIT_LEN_SYMBOL_BITS);
    if (!lit_len_root) return {};

    node* dist_root = readTree(reader, DIST_SYMBOL_BITS);
    if (!dist_root) {
        destroyTree(lit_len_root);
        return {};
    }

    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(original_size));

    while (output.size() < original_size) {
        node* cur = lit_len_root;
        while (cur && !cur->isLeaf()) {
            if (!reader.ensureBits(1)) goto done;
            uint64_t bit = reader.readBit();
            cur = (bit == 0) ? cur->left : cur->right;
        }
        if (!cur) goto done;

        uint16_t symbol = cur->symbol;

        if (symbol == 256) {
            goto done;
        }

        if (symbol < 256) {
            output.push_back(static_cast<uint8_t>(symbol));
        } else {
            int idx = symbol - 257;
            if (idx < 0 || idx >= 29) goto done;

            size_t length = LENGTH_BASES[idx];
            if (LENGTH_EXTRA[idx] > 0) {
                if (!reader.ensureBits(LENGTH_EXTRA[idx])) goto done;
                length += reader.readBits(LENGTH_EXTRA[idx]);
            }

            node* dcur = dist_root;
            while (dcur && !dcur->isLeaf()) {
                if (!reader.ensureBits(1)) goto done;
                uint64_t dbit = reader.readBit();
                dcur = (dbit == 0) ? dcur->left : dcur->right;
            }
            if (!dcur) goto done;

            uint8_t dist_code = static_cast<uint8_t>(dcur->symbol);
            if (dist_code >= 30) goto done;

            size_t distance = DIST_BASES[dist_code];
            if (DIST_EXTRA[dist_code] > 0) {
                if (!reader.ensureBits(DIST_EXTRA[dist_code])) goto done;
                distance += reader.readBits(DIST_EXTRA[dist_code]);
            }

            if (distance == 0 || distance > output.size()) {
                destroyTree(lit_len_root);
                destroyTree(dist_root);
                throw std::runtime_error("CrazyFlate decompress: distance out of range");
            }

            size_t copy_start = output.size() - distance;
            for (size_t k = 0; k < length; k++) {
                output.push_back(output[copy_start + k]);
            }
        }
    }

done:
    output.resize(static_cast<size_t>(original_size));
    destroyTree(lit_len_root);
    destroyTree(dist_root);
    return output;
}

}  // namespace algorithm
}  // namespace compressor
