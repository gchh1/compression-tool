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
        size_t order;

        Node(uint32_t s, size_t f, size_t ord = 0)
            : sym(s), freq(f), left(nullptr), right(nullptr), is_leaf(true), order(ord) {}
    };

    Node* root_{nullptr};
    std::vector<Node*> nodes_;
    std::unordered_map<uint32_t, size_t> freq_;
    std::vector<Code> codes_;
    std::unordered_map<uint64_t, uint32_t> reverse_map_;
    size_t node_order_{0};

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
        auto cmp = [](const Node* a, const Node* b) {
            if (a->freq != b->freq) return a->freq > b->freq;
            return a->order > b->order;
        };
        std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> pq(cmp);

        node_order_ = 0;
        for (auto& kv : freq_) {
            pq.push(new Node(kv.first, kv.second, node_order_++));
        }

        if (pq.empty()) return;
        if (pq.size() == 1) {
            Node* leaf = pq.top();
            pq.pop();
            Node* root = new Node(0, leaf->freq, node_order_++);
            root->left = leaf;
            root->is_leaf = false;
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
            Node* parent = new Node(0, a->freq + b->freq, node_order_++);
            parent->left = a;
            parent->right = b;
            parent->is_leaf = false;
            pq.push(parent);
            nodes_.push_back(parent);
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

    uint32_t decodeFromMap(compressor::utils::BitReader& reader) const {
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
        compressor::utils::BitWriter bw(out);
        size_t count = 0;
        for (size_t i = 0; i < codes_.size(); i++) {
            if (codes_[i].length > 0) count++;
        }

        bw.writeBits(count, 16);

        size_t written = 0;
        for (size_t i = 0; i < codes_.size() && written < count; i++) {
            if (codes_[i].length > 0) {
                bw.writeBits(codes_[i].bits, 32);
                bw.writeBits(codes_[i].length, 8);
                bw.writeBits(static_cast<uint64_t>(i), 16);
                written++;
            }
        }

        return bw.getOutput();
    }

    void decode(const std::vector<uint8_t>& data) {
        compressor::utils::BitReader br(data);
        uint64_t count;
        br.readBits(count, 16);

        codes_.clear();
        reverse_map_.clear();

        for (size_t i = 0; i < count; i++) {
            uint64_t bits, len, sym;
            br.readBits(bits, 32);
            br.readBits(len, 8);
            br.readBits(sym, 16);

            if (len > 0) {
                uint64_t key = (bits << 8) | len;
                reverse_map_[key] = static_cast<uint32_t>(sym);

                size_t need = static_cast<size_t>(sym) + 1;
                if (need > codes_.size()) codes_.resize(need);
                codes_[sym] = {bits, static_cast<uint16_t>(len)};
            }
        }
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