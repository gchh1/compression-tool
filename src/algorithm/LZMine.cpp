#include "LZMine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <stdexcept>
#include <vector>

namespace compressor {
namespace algorithm {

namespace {

inline std::vector<uint8_t> to_u8_le(size_t data, size_t bytelength) {
    std::vector<uint8_t> out(bytelength, 0);
    for (size_t i = 0; i < bytelength; i++) {
        out[i] = static_cast<uint8_t>((data >> (i * 8)) & 0xFF);
    }
    return out;
}

template <typename T>
inline T from_u8_le(const uint8_t* data, size_t bytelength) {
    T out = 0;
    for (size_t i = 0; i < bytelength; i++) {
        out |= static_cast<T>(data[i]) << (i * 8);
    }
    return out;
}

template <typename T, typename Iter>
inline T from_u8_le(Iter begin, size_t bytelength) {
    return from_u8_le<T>(&(*begin), bytelength);
}

inline void vec_concat(std::vector<uint8_t>& dst,
                        const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

inline void vec_concat(std::vector<uint8_t>& dst,
                        std::vector<uint8_t>::const_iterator start,
                        std::vector<uint8_t>::const_iterator end) {
    dst.insert(dst.end(), start, end);
}

struct MaxHeap {
    bool operator()(const LZMine::Triple* a, const LZMine::Triple* b) {
        return a->length < b->length;
    }
    bool operator()(const LZMine::Triple& a, const LZMine::Triple& b) {
        return a.length < b.length;
    }
};

} // namespace

std::vector<size_t> LZMine::kmp_next(
    std::vector<uint8_t>::const_iterator pattern, size_t len) {
    std::vector<size_t> next(len, 0);
    if (len == 0) return next;
    size_t j = 0;
    for (size_t i = 1; i < len; i++) {
        while (pattern[i] != pattern[j] && j != 0) {
            j = next[j - 1];
        }
        if (pattern[i] == pattern[j]) j++;
        next[i] = j;
    }
    return next;
}

std::vector<LZMine::Triple> LZMine::kmpMatch(
    std::vector<uint8_t>::const_iterator search_window,
    size_t search_window_size,
    std::vector<uint8_t>::const_iterator lookahead_window,
    size_t lookahead_window_size,
    size_t range) {
    if (search_window_size > max_search_size_) {
        search_window_size = max_search_size_;
        std::printf("log:search_window_size truncated to %zu\n", max_search_size_);
    }
    if (lookahead_window_size > max_look_size_) {
        lookahead_window_size = max_look_size_;
        std::printf("log:lookahead_window_size truncated to %zu\n",
                    max_look_size_);
    }
    std::priority_queue<Triple, std::vector<Triple>, MaxHeap> pq;
    size_t maxlen = 0;
    size_t mark_idx = 0;

    if (lookahead_window_size == 0) {
        return std::vector<Triple>{LZMine::Triple(0, 0, 0)};
    }

    std::vector<size_t> next =
        kmp_next(lookahead_window, lookahead_window_size);
    size_t j = 0;
    for (size_t i = 0; i < search_window_size; i++) {
        while (search_window[i] != lookahead_window[j] && j != 0) {
            j = next[j - 1];
        }
        if (search_window[i] == lookahead_window[j]) { j++; }
        if (j == lookahead_window_size - 1) {
            if (range > 1) {
                pq.push(Triple(search_window_size - i + j - 1, j,
                               lookahead_window[j]));
            } else if (range == 1) {
                std::vector<Triple> arrs;
                arrs.push_back(Triple(search_window_size - i + j - 1, j,
                                       lookahead_window[j]));
                return arrs;
            }
        } else if (j >= 4) {
            if (range > 1) {
                pq.push(Triple(search_window_size - i + j - 1, j,
                               lookahead_window[j]));
            } else if (range == 1 && j >= maxlen) {
                maxlen = j;
                mark_idx = i;
            }
        }
    }
    if (range == 1) {
        std::vector<Triple> arrs;
        if (maxlen >= 4) {
            arrs.push_back(
                Triple(search_window_size - mark_idx + maxlen - 1, maxlen,
                       lookahead_window[maxlen]));
        } else {
            arrs.push_back(Triple(0, 0, lookahead_window[0]));
        }
        return arrs;
    }
    std::vector<Triple> arrs(range);
    for (size_t i = 0; !pq.empty() && i < range; i++) {
        arrs[i] = pq.top();
        pq.pop();
    }
    if (arrs.size() < range) {
        arrs.push_back(Triple(0, 0, lookahead_window[0]));
    }
    return arrs;
}

template <typename T>
class ROLListNode {
    friend class LZMine;

private:
    size_t num;
    T data;
    ROLListNode<T>* pre;

public:
    ROLListNode(size_t n = 0, T d = T(), ROLListNode<T>* p = nullptr)
        : num(n), data(d), pre(p) {}
};

std::vector<uint8_t> LZMine::compress_ultra(
    const std::vector<uint8_t>& input, size_t search_size,
    size_t lookahead_size, size_t range, LZMineMatchType match_type) {
    if (search_size > max_search_size_) {
        search_size = max_search_size_;
        std::printf("log:search_size truncated to %zu\n", max_search_size_);
    }
    if (lookahead_size > max_look_size_) {
        lookahead_size = max_look_size_;
        std::printf("log:lookahead_size truncated to %zu\n", max_look_size_);
    }

    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return std::vector<uint8_t>{};
    }

    using Node = ROLListNode<Triple>;
    std::vector<Node*> dp(in_len);
    dp[0] = new Node(1, Triple(0, 0, input[0]), nullptr);

    for (size_t pos = 1; pos < in_len; pos++) {
        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len == 0 || dp[pos - 1] == nullptr) { continue; }

        std::vector<Triple> match =
            kmpMatch(input.begin() + search_start, search_len,
                     input.begin() + pos, look_len, range);
        for (Triple m : match) {
            size_t len = std::max(m.length, static_cast<size_t>(1));

            if (dp[pos + len] == nullptr) {
                dp[pos + len] =
                    new Node(dp[pos - 1]->num + 1, m, dp[pos - 1]);
            }
            if (dp[pos - 1]->num + 1 < dp[pos + len]->num) {
                dp[pos + len]->num = dp[pos - 1]->num + 1;
                dp[pos + len]->data = m;
                dp[pos + len]->pre = dp[pos - 1];
            }
        }
    }

    std::vector<Triple> triples;
    Node* cur = dp[in_len - 1];
    while (cur != nullptr) {
        triples.push_back(cur->data);
        cur = cur->pre;
    }

    std::vector<uint8_t> encoded;
    for (size_t i = triples.size(); i-- > 0;) {
        Triple t = triples[i];
        vec_concat(encoded, to_u8_le(t.offset, SEARCH_BYTELENGTH_));
        vec_concat(encoded, to_u8_le(t.length, LOOKAHEAD_BYTELENGTH_));
        encoded.push_back(t.next_byte);
    }

    for (size_t i = 0; i < in_len; i++) {
        if (dp[i] != nullptr) delete dp[i];
    }

    return literalrun(encoded);
}

std::vector<uint8_t> LZMine::compress(
    const std::vector<uint8_t>& input, size_t search_size,
    size_t lookahead_size, LZMineMatchType match_type) {
    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return std::vector<uint8_t>{};
    }

    if (match_type != LZMineMatchType::KMPNEXT) {
        throw std::runtime_error(
            "Unsupported match type in LZMine compress");
    }

    std::vector<uint8_t> encoded;
    encoded.reserve(in_len);

    size_t pos = 0;

    while (pos < in_len) {
        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;

        if (look_len == 0) break;

        Triple t = kmpMatch(input.begin() + search_start,
                             static_cast<size_t>(search_len),
                             input.begin() + pos,
                             static_cast<size_t>(look_len), 1)[0];

        vec_concat(encoded, to_u8_le(t.offset, SEARCH_BYTELENGTH_));
        vec_concat(encoded, to_u8_le(t.length, LOOKAHEAD_BYTELENGTH_));
        encoded.push_back(t.next_byte);

        pos += static_cast<size_t>(t.length) + 1;
    }

    if (encoded.empty()) { return std::vector<uint8_t>{}; }
    return literalrun(encoded);
}

std::vector<uint8_t> LZMine::literalrun(const std::vector<uint8_t>& input) {
    size_t in_len = input.size();
    std::vector<uint8_t> encoded;
    encoded.reserve(in_len * 2);
    std::vector<uint8_t> buffer;

    auto flush = [&]() {
        if (buffer.empty()) return;
        size_t buf_size = buffer.size();
        size_t buf_start = 0;
        while (buf_size > 0) {
            size_t chunk_size =
                (buf_size > max_look_size_) ? max_look_size_ : buf_size;
            vec_concat(encoded,
                       std::vector<uint8_t>(SEARCH_BYTELENGTH_, 0));
            vec_concat(encoded, to_u8_le(chunk_size, LOOKAHEAD_BYTELENGTH_));
            vec_concat(encoded, buffer.begin() + buf_start,
                       buffer.begin() + buf_start + chunk_size);
            buf_start += chunk_size;
            buf_size -= chunk_size;
        }
        buffer.clear();
    };

    for (size_t i = 0; i < in_len;) {
        size_t offset =
            from_u8_le<size_t>(input.begin() + i, SEARCH_BYTELENGTH_);
        i += SEARCH_BYTELENGTH_;
        size_t length =
            from_u8_le<size_t>(input.begin() + i, LOOKAHEAD_BYTELENGTH_);
        i += LOOKAHEAD_BYTELENGTH_;
        uint8_t next_byte = input[i];
        i++;
        if (offset == 0) {
            buffer.push_back(next_byte);
        } else {
            flush();
            vec_concat(encoded, to_u8_le(offset, SEARCH_BYTELENGTH_));
            vec_concat(encoded, to_u8_le(length, LOOKAHEAD_BYTELENGTH_));
            encoded.push_back(next_byte);
        }
    }
    flush();
    return encoded;
}

std::vector<uint8_t> LZMine::decompress(const std::vector<uint8_t>& input) {
    if (input.size() == 0) { return std::vector<uint8_t>{}; }

    size_t in_pos = 0;
    size_t out_pos = 0;
    size_t in_len = input.size();
    std::vector<uint8_t> out;
    out.reserve(in_len * 2);

    while (in_pos < in_len) {
        size_t offset = static_cast<size_t>(
            from_u8_le<size_t>(input.begin() + in_pos, SEARCH_BYTELENGTH_));
        in_pos += SEARCH_BYTELENGTH_;

        size_t length = static_cast<size_t>(
            from_u8_le<size_t>(input.begin() + in_pos, LOOKAHEAD_BYTELENGTH_));
        in_pos += LOOKAHEAD_BYTELENGTH_;

        if (offset == 0) {
            for (size_t i = 0; i < length; i++) {
                if (in_pos >= in_len) break;
                out.push_back(input[in_pos++]);
                out_pos++;
            }
        } else {
            uint8_t next_byte = 0;
            if (in_pos < in_len) { next_byte = input[in_pos++]; }
            size_t copy_start = 0;
            if (out_pos >= offset) {
                copy_start = out_pos - offset;
            } else {
                throw std::runtime_error(
                    "Offset in LZMine decompression out of range");
            }

            if (offset <= length) {
                for (size_t i = 0; i < length; i++) {
                    out.push_back(out[copy_start + i]);
                    out_pos++;
                }
            } else {
                out.insert(out.end(), out.begin() + copy_start,
                           out.begin() + copy_start + length);
                out_pos += length;
            }

            out.push_back(next_byte);
            out_pos++;
        }
    }

    return out;
}

} // namespace algorithm
} // namespace compressor
