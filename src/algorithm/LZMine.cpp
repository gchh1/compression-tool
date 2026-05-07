#include "LZMine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_map>
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
    size_t range,
    size_t min_match_length) {
    if (search_window_size > max_search_size_) {
        search_window_size = max_search_size_;
    }
    if (lookahead_window_size > max_look_size_) {
        lookahead_window_size = max_look_size_;
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
        if (j >= lookahead_window_size) {
            if (lookahead_window_size - 1 >= min_match_length) {
                if (range > 1) {
                    pq.push(Triple(search_window_size - i + lookahead_window_size - 1,
                                   lookahead_window_size - 1,
                                   lookahead_window[lookahead_window_size - 1]));
                } else {
                    std::vector<Triple> arrs;
                    arrs.push_back(Triple(search_window_size - i + lookahead_window_size - 1,
                                           lookahead_window_size - 1,
                                           lookahead_window[lookahead_window_size - 1]));
                    return arrs;
                }
            }
            j = (lookahead_window_size > 1) ? next[lookahead_window_size - 1] : 0;
        } else if (j == lookahead_window_size - 1) {
            if (j >= min_match_length) {
                if (range > 1) {
                    pq.push(Triple(search_window_size - i + j - 1, j,
                                   lookahead_window[j]));
                } else {
                    std::vector<Triple> arrs;
                    arrs.push_back(Triple(search_window_size - i + j - 1, j,
                                           lookahead_window[j]));
                    return arrs;
                }
            }
        } else if (j >= min_match_length) {
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
        if (maxlen >= min_match_length) {
            arrs.push_back(
                Triple(search_window_size - mark_idx + maxlen - 1, maxlen,
                       lookahead_window[maxlen]));
        } else {
            arrs.push_back(Triple(0, 0, lookahead_window[0]));
        }
        return arrs;
    }
    std::vector<Triple> arrs;
    for (size_t i = 0; !pq.empty() && i < range; i++) {
        arrs.push_back(pq.top());
        pq.pop();
    }
    if (arrs.empty()) {
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
    }
    if (lookahead_size > max_look_size_) {
        lookahead_size = max_look_size_;
    }

    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return std::vector<uint8_t>{};
    }

    using Node = ROLListNode<Triple>;
    std::vector<Node*> dp(in_len + 1, nullptr);
    dp[1] = new Node(1, Triple(0, 0, input[0]), nullptr);

    for (size_t pos = 0; pos < in_len; pos++) {
        if (dp[pos] == nullptr) { continue; }

        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len == 0) { continue; }

        std::vector<Triple> match =
            kmpMatch(input.begin() + search_start, search_len,
                     input.begin() + pos, look_len, range);
        for (Triple m : match) {
            size_t advance = (m.offset > 0)
                ? m.length + 1
                : std::max(m.length, static_cast<size_t>(1));
            size_t target = pos + advance;
            if (target > in_len) { continue; }

            if (dp[target] == nullptr) {
                dp[target] =
                    new Node(dp[pos]->num + 1, m, dp[pos]);
            }
            if (dp[pos]->num + 1 < dp[target]->num) {
                dp[target]->num = dp[pos]->num + 1;
                dp[target]->data = m;
                dp[target]->pre = dp[pos];
            }
        }
    }

    std::vector<Triple> triples;
    Node* cur = dp[in_len];
    size_t safety = 0;
    while (cur != nullptr && safety < in_len + 2) {
        triples.push_back(cur->data);
        cur = cur->pre;
        safety++;
    }

    std::vector<uint8_t> encoded;
    size_t flag_idx = 0;
    uint8_t flag_byte = 0;
    std::vector<uint8_t> token_buf;

    auto flush_flag = [&]() {
        if (flag_idx == 0) return;
        encoded.push_back(flag_byte);
        encoded.insert(encoded.end(), token_buf.begin(), token_buf.end());
        flag_byte = 0;
        flag_idx = 0;
        token_buf.clear();
    };

    for (size_t i = triples.size(); i-- > 0;) {
        Triple t = triples[i];
        if (t.offset == 0) {
            flag_byte |= (1 << flag_idx);
            token_buf.push_back(t.next_byte);
            flag_idx++;
            if (flag_idx == 8) flush_flag();
        } else {
            vec_concat(token_buf, to_u8_le(t.offset, SEARCH_BYTELENGTH_));
            vec_concat(token_buf, to_u8_le(t.length, LOOKAHEAD_BYTELENGTH_));
            token_buf.push_back(t.next_byte);
            flag_idx++;
            if (flag_idx == 8) flush_flag();
        }
    }
    flush_flag();

    std::set<Node*> deleted;
    for (size_t i = 0; i <= in_len; i++) {
        if (dp[i] != nullptr && deleted.find(dp[i]) == deleted.end()) {
            deleted.insert(dp[i]);
            delete dp[i];
        }
    }

    encoded.insert(encoded.begin(), { static_cast<uint8_t>(LOOKAHEAD_BYTELENGTH_),
                                     static_cast<uint8_t>(SEARCH_BYTELENGTH_) });
    return encoded;
}

std::vector<LZMine::Triple> LZMine::compress_ultra_triples(
    const std::vector<uint8_t>& input, size_t search_size,
    size_t lookahead_size, size_t range) {
    if (search_size > max_search_size_) {
        search_size = max_search_size_;
    }
    if (lookahead_size > max_look_size_) {
        lookahead_size = max_look_size_;
    }

    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return std::vector<Triple>{};
    }

    using Node = ROLListNode<Triple>;
    std::vector<Node*> dp(in_len + 1, nullptr);
    dp[1] = new Node(1, Triple(0, 0, input[0]), nullptr);

    for (size_t pos = 0; pos < in_len; pos++) {
        if (dp[pos] == nullptr) { continue; }

        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len == 0) { continue; }

        std::vector<Triple> match =
            kmpMatch(input.begin() + search_start, search_len,
                     input.begin() + pos, look_len, range);
        for (Triple m : match) {
            size_t advance = (m.offset > 0)
                ? m.length + 1
                : std::max(m.length, static_cast<size_t>(1));
            size_t target = pos + advance;
            if (target > in_len) { continue; }

            if (dp[target] == nullptr) {
                dp[target] =
                    new Node(dp[pos]->num + 1, m, dp[pos]);
            }
            if (dp[pos]->num + 1 < dp[target]->num) {
                dp[target]->num = dp[pos]->num + 1;
                dp[target]->data = m;
                dp[target]->pre = dp[pos];
            }
        }
    }

    std::vector<Triple> triples;
    Node* cur = dp[in_len];
    size_t safety = 0;
    while (cur != nullptr && safety < in_len + 2) {
        triples.push_back(cur->data);
        cur = cur->pre;
        safety++;
    }
    std::reverse(triples.begin(), triples.end());

    std::set<Node*> deleted;
    for (size_t i = 0; i <= in_len; i++) {
        if (dp[i] != nullptr && deleted.find(dp[i]) == deleted.end()) {
            deleted.insert(dp[i]);
            delete dp[i];
        }
    }
    return triples;
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

    size_t flag_idx = 0;
    uint8_t flag_byte = 0;
    std::vector<uint8_t> token_buf;

    auto flush_flag = [&]() {
        if (flag_idx == 0) return;
        encoded.push_back(flag_byte);
        encoded.insert(encoded.end(), token_buf.begin(), token_buf.end());
        flag_byte = 0;
        flag_idx = 0;
        token_buf.clear();
    };

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

        if (t.offset == 0) {
            flag_byte |= (1 << flag_idx);
            token_buf.push_back(t.next_byte);
            flag_idx++;
            if (flag_idx == 8) flush_flag();
            pos++;
        } else {
            vec_concat(token_buf, to_u8_le(t.offset, SEARCH_BYTELENGTH_));
            vec_concat(token_buf, to_u8_le(t.length, LOOKAHEAD_BYTELENGTH_));
            token_buf.push_back(t.next_byte);
            flag_idx++;
            if (flag_idx == 8) flush_flag();
            pos += t.length + 1;
        }
    }

    flush_flag();

    if (encoded.empty()) { return std::vector<uint8_t>{}; }
    encoded.insert(encoded.begin(), { static_cast<uint8_t>(LOOKAHEAD_BYTELENGTH_),
                                     static_cast<uint8_t>(SEARCH_BYTELENGTH_) });
    return encoded;
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
    if (input.size() < 2) {
        throw std::runtime_error("LZMine decompress: data too short for header");
    }

    size_t la_bl = input[0];
    size_t s_bl = input[1];
    if (la_bl < 1 || la_bl > 4 || s_bl < 1 || s_bl > 4) {
        throw std::runtime_error("LZMine decompress: invalid byte lengths in header");
    }

    size_t in_pos = 2;
    size_t in_len = input.size();
    std::vector<uint8_t> out;
    out.reserve(in_len * 2);

    while (in_pos < in_len) {
        uint8_t flag_byte = input[in_pos++];

        for (int j = 0; j < 8 && in_pos < in_len; j++) {
            if ((flag_byte >> j) & 1) {
                if (in_pos >= in_len) break;
                out.push_back(input[in_pos++]);
            } else {
                if (in_pos + s_bl + la_bl + 1 > in_len) break;
                size_t offset =
                    static_cast<size_t>(
                        from_u8_le<size_t>(input.begin() + in_pos, s_bl));
                in_pos += s_bl;
                size_t length =
                    static_cast<size_t>(
                        from_u8_le<size_t>(input.begin() + in_pos, la_bl));
                in_pos += la_bl;
                uint8_t next_byte = input[in_pos++];

                if (offset == 0) { break; }

                size_t out_size = out.size();
                if (out_size < offset) {
                    throw std::runtime_error(
                        "Offset in LZMine decompression out of range");
                }
                size_t copy_start = out_size - offset;

                for (size_t k = 0; k < length; k++) {
                    out.push_back(out[copy_start + k]);
                }
                out.push_back(next_byte);
            }
        }
    }

    return out;
}

LZMine::DPVisualization LZMine::get_dp_visualization(
    const std::vector<uint8_t>& input, size_t search_size,
    size_t lookahead_size, size_t range) {
    if (search_size > max_search_size_) {
        search_size = max_search_size_;
    }
    if (lookahead_size > max_look_size_) {
        lookahead_size = max_look_size_;
    }

    DPVisualization viz;
    viz.input_length = input.size();
    viz.search_size = search_size;
    viz.lookahead_size = lookahead_size;

    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return viz;
    }

    size_t min_match = SEARCH_BYTELENGTH_ + LOOKAHEAD_BYTELENGTH_ + 1;

    using Node = ROLListNode<Triple>;
    std::vector<Node*> dp(in_len + 1, nullptr);

    for (size_t pos = 0; pos < in_len; pos++) {
        if (pos == 0) {
            dp[1] = new Node(1, Triple(0, 0, input[0]), nullptr);
        }

        if (dp[pos] == nullptr) continue;

        if (pos + 1 <= in_len) {
            if (dp[pos + 1] == nullptr) {
                dp[pos + 1] = new Node(dp[pos]->num + 1, Triple(0, 0, input[pos]), dp[pos]);
            } else if (dp[pos]->num + 1 < dp[pos + 1]->num) {
                dp[pos + 1]->num = dp[pos]->num + 1;
                dp[pos + 1]->data = Triple(0, 0, input[pos]);
                dp[pos + 1]->pre = dp[pos];
            }
        }

        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len <= 1) continue;

        std::vector<Triple> match =
            kmpMatch(input.begin() + search_start, search_len,
                     input.begin() + pos, look_len, range, min_match);

        DPStep step;
        step.position = pos;
        step.best_token_count = dp[pos]->num;

        for (Triple m : match) {
            if (m.offset == 0) continue;
            size_t target = pos + m.length + 1;
            if (target > in_len) continue;

            size_t new_count = dp[pos]->num + 1;

            bool is_chosen = false;
            if (dp[target] == nullptr) {
                dp[target] = new Node(new_count, m, dp[pos]);
                is_chosen = true;
            } else if (new_count < dp[target]->num) {
                dp[target]->num = new_count;
                dp[target]->data = m;
                dp[target]->pre = dp[pos];
                is_chosen = true;
            }

            step.candidates.push_back(DPCandidate{
                m.offset, m.length, m.next_byte, is_chosen});
        }

        if (!step.candidates.empty()) {
            viz.steps.push_back(std::move(step));
        }
    }

    if (dp[in_len] != nullptr) {
        viz.optimal_path.clear();
        Node* cur = dp[in_len];
        while (cur != nullptr) {
            viz.optimal_path.push_back(cur->data);
            cur = cur->pre;
        }
        std::reverse(viz.optimal_path.begin(), viz.optimal_path.end());
    }

    {
        std::unordered_map<Node*, size_t> node_to_pos;
        for (size_t i = 0; i <= in_len; i++) {
            if (dp[i] != nullptr) {
                node_to_pos[dp[i]] = i;
            }
        }
        viz.dp_array.reserve(in_len + 1);
        for (size_t i = 0; i <= in_len; i++) {
            DPState st;
            st.position = i;
            if (dp[i] != nullptr) {
                st.reachable = true;
                st.token_count = dp[i]->num;
                st.choice = dp[i]->data;
                auto it = node_to_pos.find(dp[i]->pre);
                st.predecessor = (it != node_to_pos.end()) ? it->second : SIZE_MAX;
            } else {
                st.reachable = false;
                st.token_count = 0;
                st.predecessor = SIZE_MAX;
            }
            viz.dp_array.push_back(st);
        }
    }

    std::set<Node*> deleted;
    for (size_t i = 0; i <= in_len; i++) {
        if (dp[i] != nullptr && deleted.find(dp[i]) == deleted.end()) {
            deleted.insert(dp[i]);
            delete dp[i];
        }
    }

    return viz;
}

} // namespace algorithm
} // namespace compressor
