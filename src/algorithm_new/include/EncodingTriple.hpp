#pragma once

#include <cstdint>
#include <vector>

#include "BitProcessor.hpp"
#include "LZencoding.hpp"

namespace compressor::algorithm {

/// §1.17 encodingTriple: LZ result encode + BitWriter pending relay + EOF pad.
inline std::vector<uint8_t> encoding_triple_lz(
    const std::vector<Triple>& triples,
    const EncodingConfig& config,
    compressor::utils::_buffer& pending,
    bool final_flush) {
    auto out = writetriple(triples, config, pending);
    if (!final_flush) {
        return out;
    }
    if (pending.count > 0) {
        const int pad = 8 - pending.count;
        pending.buf <<= pad;
        out.push_back(static_cast<uint8_t>(pending.buf & 0xFFu));
        pending.count = 0;
        pending.buf = 0;
    }
    return out;
}

}  // namespace compressor::algorithm
