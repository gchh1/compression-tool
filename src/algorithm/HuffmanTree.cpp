#include "HuffmanTree.hpp"

#include <cstddef>
#include <cstdint>
#include <queue>
#include <span>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"

namespace compressor::algorithm {

static void destroyNode(node* n) {
    if (!n) return;
    destroyNode(n->left);
    destroyNode(n->right);
    delete n;
}

HuffmanTree::HuffmanTree() = default;

HuffmanTree::~HuffmanTree() {
    destroyNode(root_);
    root_ = nullptr;
}

HuffmanTree::HuffmanTree(HuffmanTree&& other) noexcept
    : root_(std::exchange(other.root_, nullptr)),
      dictionary_size_(other.dictionary_size_),
      symbol_bits_(other.symbol_bits_),
      tree_size_(other.tree_size_) {}

HuffmanTree& HuffmanTree::operator=(HuffmanTree&& other) noexcept {
    if (this != &other) {
        destroyNode(root_);
        root_ = std::exchange(other.root_, nullptr);
        dictionary_size_ = other.dictionary_size_;
        symbol_bits_ = other.symbol_bits_;
        tree_size_ = other.tree_size_;
    }
    return *this;
}

HuffmanTree::HuffmanTree(std::span<const uint8_t> symbols) {
    if (symbols.empty()) {
        return;
    }
    std::vector<uint32_t> freq_map(256, 0);
    for (const uint8_t& item : symbols) {
        freq_map[item]++;
    }

    buildTree(freq_map);
}

HuffmanTree::HuffmanTree(const std::vector<uint32_t>& freq_map,
                         size_t dictionary_size, size_t symbol_bits)
    : dictionary_size_(dictionary_size), symbol_bits_(symbol_bits) {
    buildTree(freq_map);
}

HuffmanTree::HuffmanTree(node* root, size_t dictionary_size, size_t symbol_bits)
    : root_(root), dictionary_size_(dictionary_size), symbol_bits_(symbol_bits) {
    tree_size_ = calcSerializedBits(root_);
}

HuffmanTree::HuffmanTree(utils::BitReader& reader, size_t dictionary_size,
                         size_t symbol_bits)
    : dictionary_size_(dictionary_size), symbol_bits_(symbol_bits) {
    auto buildTreeRecursive = [&reader, symbol_bits](auto& self) -> node* {
        if (reader.getRemainingBits() == 0) {
            return nullptr;
        }
        uint8_t bit = static_cast<uint8_t>(reader.readBit());
        if (reader.getRemainingBits() == 0 && bit == 0) {
            return nullptr;
        }
        if (bit == 0) {
            node* left = self(self);
            if (!left) return nullptr;
            node* right = self(self);
            if (!right) return nullptr;
            return new node(left, right);
        } else {
            if (!reader.ensureBits(static_cast<uint8_t>(symbol_bits)))
                return nullptr;
            uint16_t symbol =
                static_cast<uint16_t>(reader.readBits(static_cast<uint8_t>(symbol_bits)));
            return new node(symbol, 0);
        }
    };

    root_ = buildTreeRecursive(buildTreeRecursive);
    tree_size_ = calcSerializedBits(root_);
}

HuffmanTree::HuffmanTree(utils::BitReader& reader)
    : HuffmanTree(reader, 256, 8) {}

auto HuffmanTree::buildTree(const std::vector<uint32_t>& freq_map) -> void {
    destroyNode(root_);
    root_ = nullptr;

    std::priority_queue<node*, std::vector<node*>, Compare> pq;

    for (size_t i = 0; i < static_cast<size_t>(dictionary_size_); ++i) {
        if (freq_map[i] > 0) {
            pq.push(new node(static_cast<uint16_t>(i), freq_map[i]));
        }
    }

    if (pq.empty()) {
        root_ = new node(
            new node(static_cast<uint16_t>(0), static_cast<uint32_t>(0)),
            new node(static_cast<uint16_t>(1), static_cast<uint32_t>(0)));
        tree_size_ = calcSerializedBits(root_);
        return;
    }
    if (pq.size() == 1) {
        node* temp = pq.top();
        pq.pop();
        root_ = new node(
            temp, new node(static_cast<uint16_t>(0), static_cast<uint32_t>(0)));
        tree_size_ = calcSerializedBits(root_);
        return;
    }

    while (pq.size() >= 2) {
        node* left = pq.top();
        pq.pop();
        node* right = pq.top();
        pq.pop();

        pq.push(new node(left, right));
    }

    root_ = pq.top();
    tree_size_ = calcSerializedBits(root_);
}

auto HuffmanTree::calcSerializedBits(const node* n) const -> size_t {
    if (!n) return 0;
    if (n->isLeaf()) return 1 + symbol_bits_;
    return 1 + calcSerializedBits(n->left) + calcSerializedBits(n->right);
}

auto HuffmanTree::buildDictionary() const -> std::vector<HuffmanCode> {
    std::vector<HuffmanCode> dict(dictionary_size_);
    std::vector<uint8_t> bits;
    generateCodes(root_, 0, 0, bits, dict);
    return dict;
}

auto HuffmanTree::generateCodes(node* n, uint64_t current_code,
                                uint16_t current_length,
                                std::vector<uint8_t>& current_bits,
                                std::vector<HuffmanCode>& dict) const -> void {
    if (!n) {
        return;
    }

    if (n->isLeaf()) {
        dict[n->symbol] = {current_code, current_length, current_bits};
        return;
    }

    current_bits.push_back(0);
    generateCodes(n->left, current_length < 64 ? (current_code << 1) : 0,
                  current_length + 1, current_bits, dict);

    current_bits.back() = 1;
    generateCodes(n->right,
                  current_length < 64 ? ((current_code << 1) | 1) : 0,
                  current_length + 1, current_bits, dict);
    current_bits.pop_back();
}

auto HuffmanTree::serializeTree(utils::BitWriter& writer) const -> void {
    serializeNode(writer, root_);
}

auto HuffmanTree::serializeNode(utils::BitWriter& writer, node* n) const
    -> void {
    if (!n) {
        return;
    }

    if (n->isLeaf()) {
        writer.writeBit(1);
        writer.writeBits(n->symbol, symbol_bits_);
    } else {
        writer.writeBit(0);
        serializeNode(writer, n->left);
        serializeNode(writer, n->right);
    }
}

void HuffmanTree::deserialize(utils::BitReader& reader) {
    destroyNode(root_);
    root_ = nullptr;

    auto buildTreeRecursive = [&reader, this](auto& self) -> node* {
        if (reader.getRemainingBits() == 0) {
            return nullptr;
        }
        uint8_t bit = static_cast<uint8_t>(reader.readBit());
        if (reader.getRemainingBits() == 0 && bit == 0) {
            return nullptr;
        }
        if (bit == 0) {
            node* left = self(self);
            if (!left) return nullptr;
            node* right = self(self);
            if (!right) return nullptr;
            return new node(left, right);
        } else {
            if (!reader.ensureBits(static_cast<uint8_t>(symbol_bits_)))
                return nullptr;
            uint16_t symbol =
                static_cast<uint16_t>(reader.readBits(static_cast<uint8_t>(symbol_bits_)));
            return new node(symbol, 0);
        }
    };

    root_ = buildTreeRecursive(buildTreeRecursive);
    tree_size_ = calcSerializedBits(root_);
}

}  // namespace compressor::algorithm