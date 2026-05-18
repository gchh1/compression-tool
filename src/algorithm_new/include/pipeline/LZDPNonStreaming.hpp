#pragma once

#include <cstdint>
#include <vector>

#include "EncodingTriple.hpp"
#include "LZDP.hpp"

namespace compressor::algorithm::pipeline {

struct LZDPNonStreamingResult {
    std::vector<uint8_t> compressed;
    std::vector<Triple> triples;
};

LZDPNonStreamingResult compress_bytes(
    const std::vector<uint8_t>& input,
    const LZDPConfig& config);

std::vector<uint8_t> decompress_bytes(
    const std::vector<uint8_t>& compressed,
    const LZDPConfig& config);

}  // namespace compressor::algorithm::pipeline
