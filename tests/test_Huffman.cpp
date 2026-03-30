#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "HuffmanTree.hpp"

int main() {
    std::vector<uint8_t> v = {'a', 'b', 'c', 'd', 'e', 'f'};
    compressor::algorithm::HuffmanTree huffman_tree(v);
    std::vector<std::string> res;
    res = huffman_tree.encode();

    for (const auto& item : v) {
        std::cout << item << ": " << res[item] << std::endl;
    }
}
