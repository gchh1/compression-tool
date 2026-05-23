#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "BitProcessor.hpp"
#include "HuffmanTree3HM.hpp"
#include "LZencoding.hpp"

namespace compressor::algorithm {

struct Inflate3HMEncodeResult {
    std::vector<uint8_t> data;
    size_t total_tree_bits;
};

inline Inflate3HMEncodeResult inflate3hm_encode(
    const std::vector<Triple>& triples,
    const HuffmanTree3HMConfig& tree_cfg,
    compressor::utils::_buffer& pending) {
    Inflate3HMEncodeResult result;
    if (triples.empty()) return result;

    uint32_t oc = tree_cfg.offset_chunks();
    uint32_t lc = tree_cfg.length_chunks();
    uint32_t mask = tree_cfg.chunk_mask();
    uint32_t cb = tree_cfg.chunk_bits;

    std::vector<uint32_t> lit_freq(tree_cfg.literal_dict_size(), 0);
    std::vector<uint32_t> off_freq(tree_cfg.offset_dict_size(), 0);
    std::vector<uint32_t> len_freq(tree_cfg.length_dict_size(), 0);

    auto count_chunks = [&](uint32_t value, std::vector<uint32_t>& freq,
                            uint32_t chunks, uint32_t chunk_bits, uint32_t chunk_mask) {
        for (uint32_t i = 0; i < chunks; ++i) {
            uint32_t chunk = (value >> (i * chunk_bits)) & chunk_mask;
            freq[chunk]++;
        }
    };

    std::vector<uint8_t> run_buf;
    auto flush_run = [&]() {
        if (run_buf.empty()) return;
        uint32_t run_len = static_cast<uint32_t>(run_buf.size());
        count_chunks(0, off_freq, oc, cb, mask);
        count_chunks(run_len, len_freq, lc, cb, mask);
        for (uint8_t lit : run_buf) {
            lit_freq[lit]++;
        }
        run_buf.clear();
    };

    for (const auto& t : triples) {
        if (t.offset == 0) {
            run_buf.push_back(t.literal);
        } else {
            flush_run();
            count_chunks(t.offset, off_freq, oc, cb, mask);
            count_chunks(t.length, len_freq, lc, cb, mask);
        }
    }
    flush_run();

    count_chunks(0, off_freq, oc, cb, mask);
    count_chunks(0, len_freq, lc, cb, mask);

    for (auto& f : lit_freq)  if (f == 0) f = 1;
    for (auto& f : off_freq)  if (f == 0) f = 1;
    for (auto& f : len_freq)  if (f == 0) f = 1;

    HuffmanTree3HM tree(tree_cfg);
    tree.buildTrees(lit_freq, off_freq, len_freq);
    result.total_tree_bits = tree.getTotalTreeSize();

    compressor::utils::BitWriter writer(result.data, pending);

    tree.serialize(writer);

    run_buf.clear();
    for (const auto& t : triples) {
        if (t.offset == 0) {
            run_buf.push_back(t.literal);
        } else {
            if (!run_buf.empty()) {
                tree.encodeMatch(writer, 0, static_cast<uint32_t>(run_buf.size()));
                for (uint8_t lit : run_buf) {
                    tree.encodeLiteral(writer, lit);
                }
                run_buf.clear();
            }
            tree.encodeMatch(writer, t.offset, t.length);
        }
    }
    if (!run_buf.empty()) {
        tree.encodeMatch(writer, 0, static_cast<uint32_t>(run_buf.size()));
        for (uint8_t lit : run_buf) {
            tree.encodeLiteral(writer, lit);
        }
        run_buf.clear();
    }

    tree.encodeMatch(writer, 0, 0);

    pending = writer.getBuf();
    return result;
}

struct Inflate3HMDecodeResult {
    std::vector<uint8_t> data;
};

inline Inflate3HMDecodeResult inflate3hm_decode(
    const std::vector<uint8_t>& compressed,
    compressor::utils::_buffer& pending) {
    Inflate3HMDecodeResult result;

    if (compressed.size() < 4) return result;

    compressor::utils::BitReader reader(compressed, pending);

    HuffmanTree3HM tree;
    tree.deserialize(reader);

    size_t max_iterations = compressed.size() * 8 + 1024;
    size_t iter = 0;

    while (iter < max_iterations) {
        ++iter;
        if (!reader.ensureBits(1)) break;

        uint32_t offset = tree.decodeOffset(reader);
        if (!reader.ensureBits(1)) break;

        uint32_t length = tree.decodeMatchLength(reader);

        if (offset == 0 && length == 0) break;

        if (offset == 0) {
            for (uint32_t i = 0; i < length; ++i) {
                if (!reader.ensureBits(1)) break;
                uint8_t lit = tree.decodeLiteral(reader);
                result.data.push_back(lit);
            }
        } else {
            size_t copy_start = result.data.size() > offset
                ? result.data.size() - offset : 0;
            for (uint32_t i = 0; i < length; ++i) {
                if (copy_start + i < result.data.size()) {
                    result.data.push_back(result.data[copy_start + i]);
                }
            }
        }
    }

    pending = reader.getBuf();
    return result;
}

}  // namespace compressor::algorithm