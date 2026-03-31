#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "HuffmanTree.hpp"

int main() {
    std::vector<uint8_t> v = {'a', 'b', 'c', 'd', 'e', 'f'};
    std::vector<uint32_t> f(256, 0);
    for (const auto& item : v) {
        f[item] = 1;
    }
    compressor::algorithm::HuffmanTree huffman_tree(f);
    std::vector<std::string> res;
    res = huffman_tree.encode();

    for (const auto& item : v) {
        std::cout << item << ": " << res[item] << std::endl;
    }
}
