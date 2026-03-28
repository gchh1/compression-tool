#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "LZ77.hpp"

int main() {
    std::string s = "aacaacabcabaaac";
    std::vector<uint8_t> v = compressor::algorithm::LZ77::compress(
        std::vector<uint8_t>(s.begin(), s.end()));
    std::vector<uint8_t> v1 = compressor::algorithm::LZ77::decompress(v);
}