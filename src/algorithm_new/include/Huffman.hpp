#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BitProcessor.hpp"

namespace compressor::algorithm {

class Huffman{//要求单词编码位宽相同
public:
    struct Code {
        uint64_t bits{0};
        uint16_t length{0};
    };
private:
    struct Node {
        uint32_t sym;
        size_t freq;
        Node* left;
        Node* right;
        bool is_leaf;

        Node(uint32_t s, size_t f)
            : sym(s), freq(f), left(nullptr), right(nullptr), is_leaf(true) {}
    };

    Node* root_{nullptr};
    std::vector<Node*> nodes_;
    std::unordered_map<uint32_t, size_t> freq_;
    std::vector<Code> codes_;
    std::unordered_map<uint64_t, uint32_t> reverse_map_;

    void generateCodes(const Node* node, uint64_t bits, uint16_t len) {
        if (!node) return;
        if (node->is_leaf) {
            size_t need = static_cast<size_t>(node->sym) + 1;
            if (need > codes_.size()) {
                codes_.resize(need);
            }
            codes_[node->sym] = {bits, len};
            return;
        }
        generateCodes(node->left, bits << 1, len + 1);
        generateCodes(node->right, (bits << 1) | 1, len + 1);
    }
public:
    void addSymbol(uint32_t symbol) {
        freq_[symbol]++;
    }

    void build() {
        auto cmp = [](const Node* a, const Node* b) { return a->freq > b->freq; };
        std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> pq(cmp);

        for (auto& kv : freq_) {
            pq.push(new Node(kv.first, kv.second));
        }

        if (pq.empty()) return;
        if (pq.size() == 1) {
            Node* leaf = pq.top();
            pq.pop();
            Node* root = new Node(0, leaf->freq);
            root->left = leaf;
            nodes_.push_back(leaf);
            nodes_.push_back(root);
            root_ = root;
            generateCodes(root, 0, 0);
            buildReverseMap();
            return;
        }

        while (pq.size() > 1) {
            Node* a = pq.top(); pq.pop();
            Node* b = pq.top(); pq.pop();
            Node* parent = new Node(0, a->freq + b->freq);
            parent->left = a;
            parent->right = b;
            pq.push(parent);
        }

        root_ = pq.top(); pq.pop();
        generateCodes(root_, 0, 0);
        buildReverseMap();
    }

    const std::vector<Code>& getCodes() const { return codes_; }
    const std::unordered_map<uint64_t, uint32_t>& getReverseMap() const { return reverse_map_; }

    void buildReverseMap() {
        reverse_map_.clear();
        for (size_t sym = 0; sym < codes_.size(); sym++) {
            if (codes_[sym].length > 0) {
                uint64_t key = (codes_[sym].bits << 8) | codes_[sym].length;
                reverse_map_[key] = static_cast<uint32_t>(sym);
            }
        }
    }

    uint32_t decodeFromMap(utils::BitReader& reader) const {
        uint64_t code = 0;
        int len = 0;
        while (true) {
            uint64_t bit;
            reader.readBits(bit, 1);
            code = (code << 1) | bit;
            len++;
            uint64_t key = (code << 8) | static_cast<uint64_t>(len);
            auto it = reverse_map_.find(key);
            if (it != reverse_map_.end()) {
                return it->second;
            }
        }
    }
    bool has(uint32_t symbol) const {
        return symbol < codes_.size() && codes_[symbol].length > 0;
    }

    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> out;
        utils::BitWriter bw(out);
        size_t count = 0;
        for (size_t i = 0; i < codes_.size(); i++) {
            if (codes_[i].length > 0) count++;
        }

        if (count > 255) count = 255;
        bw.writeBits(count, 8);

        size_t written = 0;
        for (size_t i = 0; i < codes_.size() && written < count; i++) {
            if (codes_[i].length > 0) {
                bw.writeBits(static_cast<uint64_t>(i), 16);
                bw.writeBits(codes_[i].length, 8);
                written++;
            }
        }

        return bw.getOutput();
    }

    void rebuildCodesFromLengths(const std::vector<std::pair<uint32_t, uint8_t>>& entries) {
        freq_.clear();
        codes_.clear();
        if (entries.empty()) return;

        int max_len = 0;
        for (auto& e : entries) {
            if (e.second > max_len) max_len = e.second;
        }

        std::vector<int> bl_count(static_cast<size_t>(max_len) + 1, 0);
        for (auto& e : entries) bl_count[e.second]++;

        std::vector<uint64_t> next_code(static_cast<size_t>(max_len) + 1, 0);
        uint64_t code = 0;
        for (int i = 1; i <= max_len; i++) {
            code = (code + static_cast<uint64_t>(bl_count[static_cast<size_t>(i) - 1])) << 1;
            next_code[static_cast<size_t>(i)] = code;
        }

        auto sorted = entries;
        std::sort(sorted.begin(), sorted.end());
        for (auto& e : sorted) {
            size_t need = static_cast<size_t>(e.first) + 1;
            if (need > codes_.size()) codes_.resize(need);
            codes_[e.first] = {next_code[e.second], e.second};
            next_code[e.second]++;
        }
    }

    void decode(const std::vector<uint8_t>& data) {
        utils::BitReader br(data);
        uint64_t count;
        br.readBits(count, 8);

        std::vector<std::pair<uint32_t, uint8_t>> entries;
        for (size_t i = 0; i < count; i++) {
            uint64_t sym, len;
            br.readBits(sym, 16);
            br.readBits(len, 8);
            if (len > 0) {
                entries.emplace_back(static_cast<uint32_t>(sym), static_cast<uint8_t>(len));
            }
        }

        rebuildCodesFromLengths(entries);
        buildReverseMap();
    }

    void reset() {
        for (Node* n : nodes_) delete n;
        nodes_.clear();
        freq_.clear();
        codes_.clear();
        reverse_map_.clear();
        root_ = nullptr;
    }

    ~Huffman() {
        for (Node* n : nodes_) delete n;
        root_ = nullptr;
    }
};

}  // namespace compressor::algorithm