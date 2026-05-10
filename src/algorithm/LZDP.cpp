#include "LZDP.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace compressor {
namespace algorithm {

size_t LZDP::calcBitWidth(size_t max_val) {
    if (max_val == 0) return 1;
    size_t bits = 0;
    while (max_val > 0) {
        bits++;
        max_val >>= 1;
    }
    return bits;
}

namespace {

struct BitWriter {
    std::vector<uint8_t>& buf;
    uint8_t current;
    int pos;

    BitWriter(std::vector<uint8_t>& b) : buf(b), current(0), pos(0) {}

    void writeBits(uint64_t value, int num_bits) {
        for (int i = 0; i < num_bits; i++) {
            if (value & (1ULL << i)) {
                current |= (1 << pos);
            }
            pos++;
            if (pos == 8) {
                buf.push_back(current);
                current = 0;
                pos = 0;
            }
        }
    }

    void flush() {
        if (pos > 0) {
            buf.push_back(current);
            current = 0;
            pos = 0;
        }
    }
};

struct BitReader {
    const uint8_t* data;
    size_t size;
    size_t byte_pos;
    int bit_pos;

    BitReader(const uint8_t* d, size_t s)
        : data(d), size(s), byte_pos(0), bit_pos(0) {}

    size_t getRemainingBits() const {
        if (byte_pos >= size) return 0;
        return (size - byte_pos) * 8 - bit_pos;
    }

    bool ensureBits(int num_bits) const {
        return getRemainingBits() >= static_cast<size_t>(num_bits);
    }

    uint64_t readBits(int num_bits) {
        uint64_t value = 0;
        for (int i = 0; i < num_bits; i++) {
            if (byte_pos >= size) break;
            if (data[byte_pos] & (1 << bit_pos)) {
                value |= (1ULL << i);
            }
            bit_pos++;
            if (bit_pos == 8) {
                bit_pos = 0;
                byte_pos++;
            }
        }
        return value;
    }
};

} // namespace

template <typename T>
class ROLListNode {
    friend class LZDP;

private:
    size_t num;
    T data;
    ROLListNode<T>* pre;

public:
    ROLListNode(size_t n = 0, T d = T(), ROLListNode<T>* p = nullptr)
        : num(n), data(d), pre(p) {}
};

std::vector<uint8_t> LZDP::compress_dp(
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
        return std::vector<uint8_t>{};
    }

    using Node = ROLListNode<Triple>;
    std::vector<Node*> dp(in_len + 1, nullptr);
    dp[0] = new Node(0, Triple(0, 0, 0), nullptr);

    std::vector<size_t> head;
    std::vector<size_t> prev;
    if (match_engine_ == 1) {
        head.assign(max_search_size_ + 1, SIZE_MAX);
        prev.assign(in_len, SIZE_MAX);
    }

    for (size_t pos = 0; pos < in_len; pos++) {
        if (match_engine_ == 1 && pos + 2 < in_len) {
            uint16_t hash_val = ((input[pos] << 10) ^ (input[pos + 1] << 5) ^ input[pos + 2]) & (max_search_size_);
            prev[pos] = head[hash_val];
            head[hash_val] = pos;
        }
        if (dp[pos] == nullptr) { continue; }

        if (dp[pos + 1] == nullptr) {
            dp[pos + 1] = new Node(dp[pos]->num + 1,
                Triple(0, 0, input[pos]), dp[pos]);
        } else if (dp[pos]->num + 1 < dp[pos + 1]->num) {
            dp[pos + 1]->num = dp[pos]->num + 1;
            dp[pos + 1]->data = Triple(0, 0, input[pos]);
            dp[pos + 1]->pre = dp[pos];
        }

        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len < get_min_match()) { continue; }

        struct MatchRes { size_t offset; size_t length; };
        std::vector<MatchRes> match_results;

        if (match_engine_ == 0) {
            auto kmp_results = kmpSearch(
                input.begin() + search_start, search_len,
                input.begin() + pos, look_len, range, get_min_match());
            for (auto& kr : kmp_results) {
                match_results.push_back({kr.offset, kr.length});
            }
        } else {
            size_t match_pos = prev[pos]; 
            size_t chain_length = range * 8; 
            while (match_pos != SIZE_MAX && chain_length-- > 0) {
                size_t dist = pos - match_pos;
                if (dist > search_size || dist == 0) break;

                size_t match_len = 0;
                while (match_len < look_len && input[pos + match_len] == input[match_pos + match_len]) {
                    match_len++;
                }

                if (match_len >= get_min_match()) {
                    match_results.push_back({dist, match_len});
                }
                match_pos = prev[match_pos];
            }
            if (match_results.size() > range) {
                std::sort(match_results.begin(), match_results.end(), [](const MatchRes& a, const MatchRes& b) {
                    return a.length > b.length;
                });
                match_results.resize(range);
            }
        }

        for (auto& kr : match_results) {
            if (kr.offset == 0 || kr.length < get_min_match()) { continue; }
            size_t target = pos + kr.length;
            if (target > in_len) { continue; }

            if (dp[target] == nullptr) {
                dp[target] = new Node(dp[pos]->num + 1,
                    Triple(kr.offset, kr.length, 0), dp[pos]);
            } else if (dp[pos]->num + 1 < dp[target]->num) {
                dp[target]->num = dp[pos]->num + 1;
                dp[target]->data = Triple(kr.offset, kr.length, 0);
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

    std::vector<Triple> forward;
    for (size_t i = triples.size(); i-- > 0;) {
        Triple t = triples[i];
        if (i == triples.size() - 1 && t.offset == 0 && t.length == 0) { continue; }
        forward.push_back(t);
    }

    std::vector<uint8_t> encoded;
    encoded.push_back(static_cast<uint8_t>((offset_bits_ & 0x7F) | (use_flag_encoding_ ? 0x80 : 0)));
    encoded.push_back(static_cast<uint8_t>(length_bits_));

    BitWriter bw(encoded);
    for (size_t i = 0; i < forward.size(); ) {
        Triple t = forward[i];
        if (use_flag_encoding_) {
            if (t.offset == 0) {
                bw.writeBits(1, 1);
                bw.writeBits(t.literal, 8);
                i++;
            } else {
                bw.writeBits(0, 1);
                bw.writeBits(t.offset, static_cast<int>(offset_bits_));
                bw.writeBits(t.length, static_cast<int>(length_bits_));
                i++;
            }
        } else {
            if (t.offset == 0) {
                size_t run_len = 0;
                size_t run_start = i;
                while (i < forward.size() && forward[i].offset == 0) {
                    run_len++;
                    i++;
                }
                size_t max_run = (size_t{1} << length_bits_) - 1;
                size_t current = 0;
                while (run_len > 0) {
                    size_t chunk = (run_len > max_run) ? max_run : run_len;
                    bw.writeBits(0, static_cast<int>(offset_bits_));
                    bw.writeBits(chunk, static_cast<int>(length_bits_));
                    for (size_t j = 0; j < chunk; j++) {
                        bw.writeBits(forward[run_start + current + j].literal, 8);
                    }
                    current += chunk;
                    run_len -= chunk;
                }
            } else {
                bw.writeBits(t.offset, static_cast<int>(offset_bits_));
                bw.writeBits(t.length, static_cast<int>(length_bits_));
                i++;
            }
        }
    }
    bw.flush();

    std::set<Node*> deleted;
    for (size_t i = 0; i <= in_len; i++) {
        if (dp[i] != nullptr && deleted.find(dp[i]) == deleted.end()) {
            deleted.insert(dp[i]);
            delete dp[i];
        }
    }

    return encoded;
}

std::vector<LZDP::Triple> LZDP::compress_dp_triples(
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
    dp[0] = new Node(0, Triple(0, 0, 0), nullptr);

    std::vector<size_t> head;
    std::vector<size_t> prev;
    if (match_engine_ == 1) {
        head.assign(max_search_size_ + 1, SIZE_MAX);
        prev.assign(in_len, SIZE_MAX);
    }

    for (size_t pos = 0; pos < in_len; pos++) {
        if (match_engine_ == 1 && pos + 2 < in_len) {
            uint16_t hash_val = ((input[pos] << 10) ^ (input[pos + 1] << 5) ^ input[pos + 2]) & (max_search_size_);
            prev[pos] = head[hash_val];
            head[hash_val] = pos;
        }
        if (dp[pos] == nullptr) { continue; }

        if (dp[pos + 1] == nullptr) {
            dp[pos + 1] = new Node(dp[pos]->num + 1,
                Triple(0, 0, input[pos]), dp[pos]);
        } else if (dp[pos]->num + 1 < dp[pos + 1]->num) {
            dp[pos + 1]->num = dp[pos]->num + 1;
            dp[pos + 1]->data = Triple(0, 0, input[pos]);
            dp[pos + 1]->pre = dp[pos];
        }

        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len < get_min_match()) { continue; }

        struct MatchRes { size_t offset; size_t length; };
        std::vector<MatchRes> match_results;

        if (match_engine_ == 0) {
            auto kmp_results = kmpSearch(
                input.begin() + search_start, search_len,
                input.begin() + pos, look_len, range, get_min_match());
            for (auto& kr : kmp_results) {
                match_results.push_back({kr.offset, kr.length});
            }
        } else {
            size_t match_pos = prev[pos]; 
            size_t chain_length = range * 8; 
            while (match_pos != SIZE_MAX && chain_length-- > 0) {
                size_t dist = pos - match_pos;
                if (dist > search_size || dist == 0) break;

                size_t match_len = 0;
                while (match_len < look_len && input[pos + match_len] == input[match_pos + match_len]) {
                    match_len++;
                }

                if (match_len >= get_min_match()) {
                    match_results.push_back({dist, match_len});
                }
                match_pos = prev[match_pos];
            }
            if (match_results.size() > range) {
                std::sort(match_results.begin(), match_results.end(), [](const MatchRes& a, const MatchRes& b) {
                    return a.length > b.length;
                });
                match_results.resize(range);
            }
        }

        for (auto& kr : match_results) {
            if (kr.offset == 0 || kr.length < get_min_match()) { continue; }
            size_t target = pos + kr.length;
            if (target > in_len) { continue; }

            if (dp[target] == nullptr) {
                dp[target] = new Node(dp[pos]->num + 1,
                    Triple(kr.offset, kr.length, 0), dp[pos]);
            } else if (dp[pos]->num + 1 < dp[target]->num) {
                dp[target]->num = dp[pos]->num + 1;
                dp[target]->data = Triple(kr.offset, kr.length, 0);
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
    if (!triples.empty() && triples[0].offset == 0 && triples[0].length == 0 && triples[0].literal == 0) {
        triples.erase(triples.begin());
    }

    std::set<Node*> deleted;
    for (size_t i = 0; i <= in_len; i++) {
        if (dp[i] != nullptr && deleted.find(dp[i]) == deleted.end()) {
            deleted.insert(dp[i]);
            delete dp[i];
        }
    }
    return triples;
}

std::vector<uint8_t> LZDP::compress(
    const std::vector<uint8_t>& input, size_t search_size,
    size_t lookahead_size) {
    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return std::vector<uint8_t>{};
    }

    std::vector<uint8_t> encoded;
    encoded.push_back(static_cast<uint8_t>((offset_bits_ & 0x7F) | (use_flag_encoding_ ? 0x80 : 0)));
    encoded.push_back(static_cast<uint8_t>(length_bits_));

    BitWriter bw(encoded);
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

        auto kmp_results = kmpSearch(
            input.begin() + search_start, static_cast<size_t>(search_len),
            input.begin() + pos, static_cast<size_t>(look_len), 1, get_min_match());

        if (kmp_results.empty()) {
            if (use_flag_encoding_) {
                bw.writeBits(1, 1);
                bw.writeBits(input[pos], 8);
                pos++;
            } else {
                size_t run_len = 0;
                size_t run_start = pos;
                while (pos < in_len) {
                    size_t slen = (pos > search_size) ? static_cast<size_t>(search_size) : pos;
                    size_t sstart = pos - slen;
                    size_t rem = in_len - pos;
                    size_t llen = (rem > lookahead_size) ? static_cast<size_t>(lookahead_size) : rem;
                    if (llen == 0) break;
                    auto peek = kmpSearch(
                        input.begin() + sstart, slen,
                        input.begin() + pos, llen, 1, get_min_match());
                    if (!peek.empty()) break;
                    run_len++;
                    pos++;
                }
                size_t max_run = (size_t{1} << length_bits_) - 1;
                size_t current = 0;
                while (run_len > 0) {
                    size_t chunk = (run_len > max_run) ? max_run : run_len;
                    bw.writeBits(0, static_cast<int>(offset_bits_));
                    bw.writeBits(chunk, static_cast<int>(length_bits_));
                    for (size_t k = 0; k < chunk; k++) {
                        bw.writeBits(input[run_start + current + k], 8);
                    }
                    current += chunk;
                    run_len -= chunk;
                }
            }
        } else {
            auto& m = kmp_results[0];
            if (use_flag_encoding_) {
                bw.writeBits(0, 1);
            }
            bw.writeBits(m.offset, static_cast<int>(offset_bits_));
            bw.writeBits(m.length, static_cast<int>(length_bits_));
            pos += m.length;
        }
    }

    bw.flush();

    if (encoded.size() <= 2) { return std::vector<uint8_t>{}; }
    return encoded;
}

std::vector<uint8_t> LZDP::decompress(const std::vector<uint8_t>& input) {
    if (input.size() < 2) {
        return std::vector<uint8_t>{};
    }

    size_t ob = input[0] & 0x7F;
    bool use_flag = (input[0] & 0x80) != 0;
    size_t lb = input[1];
    if (ob < 1 || ob > 24 || lb < 1 || lb > 24) {
        throw std::runtime_error("LZDP decompress: invalid bit widths in header");
    }

    BitReader br(input.data() + 2, input.size() - 2);
    std::vector<uint8_t> out;
    out.reserve(input.size() * 2);

    while (br.byte_pos < br.size) {
        if (use_flag) {
            if (br.getRemainingBits() == 0) break;
            if (!br.ensureBits(1)) break;
            uint64_t is_lit = br.readBits(1);
            if (is_lit) {
                if (!br.ensureBits(8)) break;
                uint64_t lit = br.readBits(8);
                out.push_back(static_cast<uint8_t>(lit));
            } else {
                if (!br.ensureBits(static_cast<uint8_t>(ob + lb))) break;
                uint64_t offset = br.readBits(static_cast<int>(ob));
                uint64_t length = br.readBits(static_cast<int>(lb));
                size_t out_size = out.size();
                if (out_size < static_cast<size_t>(offset)) {
                    throw std::runtime_error(
                        "Offset in LZDP decompression out of range: offset=" + std::to_string(offset) + " out_size=" + std::to_string(out_size));
                }
                size_t copy_start = out_size - static_cast<size_t>(offset);
                for (size_t k = 0; k < static_cast<size_t>(length); k++) {
                    out.push_back(out[copy_start + k]);
                }
            }
        } else {
            if (br.getRemainingBits() < ob + lb) break;
            if (!br.ensureBits(static_cast<uint8_t>(ob + lb))) break;
            uint64_t offset = br.readBits(static_cast<int>(ob));
            uint64_t length = br.readBits(static_cast<int>(lb));
            if (offset == 0) {
                for (size_t k = 0; k < static_cast<size_t>(length); k++) {
                    if (!br.ensureBits(8)) break;
                    uint64_t lit = br.readBits(8);
                    out.push_back(static_cast<uint8_t>(lit));
                }
            } else {
                size_t out_size = out.size();
                if (out_size < static_cast<size_t>(offset)) {
                    throw std::runtime_error(
                        "Offset in LZDP decompression out of range: offset=" + std::to_string(offset) + " out_size=" + std::to_string(out_size));
                }
                size_t copy_start = out_size - static_cast<size_t>(offset);
                for (size_t k = 0; k < static_cast<size_t>(length); k++) {
                    out.push_back(out[copy_start + k]);
                }
            }
        }
    }

    return out;
}

LZDP::DPVisualization LZDP::get_dp_visualization(
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

    size_t min_match = get_min_match();

    using Node = ROLListNode<Triple>;
    std::vector<Node*> dp(in_len + 1, nullptr);
    dp[0] = new Node(0, Triple(0, 0, 0), nullptr);

    std::vector<size_t> head;
    std::vector<size_t> prev;
    if (match_engine_ == 1) {
        head.assign(max_search_size_ + 1, SIZE_MAX);
        prev.assign(in_len, SIZE_MAX);
    }

    for (size_t pos = 0; pos < in_len; pos++) {
        if (match_engine_ == 1 && pos + 2 < in_len) {
            uint16_t hash_val = ((input[pos] << 10) ^ (input[pos + 1] << 5) ^ input[pos + 2]) & (max_search_size_);
            prev[pos] = head[hash_val];
            head[hash_val] = pos;
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
        if (look_len < get_min_match()) { continue; }

        struct MatchRes { size_t offset; size_t length; };
        std::vector<MatchRes> match_results;

        if (match_engine_ == 0) {
            auto kmp_results = kmpSearch(
                input.begin() + search_start, search_len,
                input.begin() + pos, look_len, range, get_min_match());
            for (auto& kr : kmp_results) {
                match_results.push_back({kr.offset, kr.length});
            }
        } else {
            size_t match_pos = prev[pos]; 
            size_t chain_length = range * 8; 
            while (match_pos != SIZE_MAX && chain_length-- > 0) {
                size_t dist = pos - match_pos;
                if (dist > search_size || dist == 0) break;

                size_t match_len = 0;
                while (match_len < look_len && input[pos + match_len] == input[match_pos + match_len]) {
                    match_len++;
                }

                if (match_len >= get_min_match()) {
                    match_results.push_back({dist, match_len});
                }
                match_pos = prev[match_pos];
            }
            if (match_results.size() > range) {
                std::sort(match_results.begin(), match_results.end(), [](const MatchRes& a, const MatchRes& b) {
                    return a.length > b.length;
                });
                match_results.resize(range);
            }
        }

        DPStep step;
        step.position = pos;
        step.best_token_count = dp[pos]->num;

        for (auto& kr : match_results) {
            if (kr.offset == 0) continue;
            size_t target = pos + kr.length;
            if (target > in_len) continue;

            size_t new_count = dp[pos]->num + 1;

            bool is_chosen = false;
            if (dp[target] == nullptr) {
                dp[target] = new Node(new_count, Triple(kr.offset, kr.length, 0), dp[pos]);
                is_chosen = true;
            } else if (new_count < dp[target]->num) {
                dp[target]->num = new_count;
                dp[target]->data = Triple(kr.offset, kr.length, 0);
                dp[target]->pre = dp[pos];
                is_chosen = true;
            }

            step.candidates.push_back(DPCandidate{
                kr.offset, kr.length, 0, is_chosen});
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