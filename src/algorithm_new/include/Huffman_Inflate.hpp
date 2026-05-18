#pragma once

#include <unordered_map>

#include "Huffman.hpp"
#include "BitProcessor.hpp"
#include "Models.hpp"

namespace compressor::algorithm{

struct Huffman_InflateConfig{
    size_t offset_bits;
    size_t length_bits;
    size_t offset_chunk_bits;
    size_t length_chunk_bits;
};

class Huffman_Inflate{
    const size_t litearal_bitwidth{8};
    Huffman_InflateConfig config;
    EncodingConfig encoding_config;

    size_t offset_remainder;
    size_t length_remainder;

    Huffman literal_tree, offset_tree, length_tree;

    void encodeMultiLevel(uint32_t value, size_t value_bits,
                          const std::vector<Huffman::Code>& codes,
                          size_t chunk_bits,
                          utils::BitWriter& bw) const {
        size_t num_chunks = (value_bits + chunk_bits - 1) / chunk_bits;
        const size_t mask = (size_t{1} << chunk_bits) - 1;
        for (size_t i = 0; i < num_chunks; ++i) {
            size_t chunk = (static_cast<size_t>(value) >> (i * chunk_bits)) & mask;
            bw.writeBits(codes[chunk].bits, codes[chunk].length);
        }
    }

    uint32_t decodeMultiLevel(const Huffman& tree, size_t value_bits,
                              size_t chunk_bits, utils::BitReader& reader) const {
        size_t num_chunks = (value_bits + chunk_bits - 1) / chunk_bits;
        uint32_t value = 0;
        size_t last_chunk_valid_bits = value_bits % chunk_bits;
        if (last_chunk_valid_bits == 0) {
            last_chunk_valid_bits = chunk_bits;
        }

        for (size_t i = 0; i < num_chunks; ++i) {
            size_t chunk = tree.decodeFromMap(reader);
            if (i == num_chunks - 1 && last_chunk_valid_bits < chunk_bits) {
                chunk &= (size_t{1} << last_chunk_valid_bits) - 1;
            }
            value |= static_cast<uint32_t>(chunk << (i * chunk_bits));
        }
        return value;
    }

public:
    explicit Huffman_Inflate(Huffman_InflateConfig config, EncodingConfig encoding_config)
        : config(config), encoding_config(encoding_config) {
            size_t dividend = config.offset_bits / config.offset_chunk_bits;
            offset_remainder = config.offset_bits - dividend * config.offset_chunk_bits;
            dividend = config.length_bits / config.length_chunk_bits;
            length_remainder = config.length_bits - dividend * config.length_chunk_bits;
        }
    ~Huffman_Inflate() = default;

    void countFreq(const std::vector<Triple>& triples){
        for (Triple t : triples){
            if (t.offset == 0 && t.length == 0){
                literal_tree.addSymbol(t.literal);
            } else {
                size_t tmp = encoding_config.offset_bits;
                uint32_t offset = t.offset;

                tmp -= offset_remainder;
                offset_tree.addSymbol(offset >> tmp);
                offset &= (1 << tmp) - 1;
                while (tmp >= config.offset_chunk_bits) {
                    tmp -= config.offset_chunk_bits;
                    offset_tree.addSymbol(offset >> tmp);
                    offset &= (1 << tmp) - 1;
                }

                tmp = encoding_config.length_bits;
                uint32_t length = t.length;

                tmp -= length_remainder;
                length_tree.addSymbol(length >> tmp);
                length &= (1 << tmp) - 1;
                while (tmp >= config.length_chunk_bits) {
                    tmp -= config.length_chunk_bits;
                    length_tree.addSymbol(length >> tmp);
                    length &= (1 << tmp) - 1;
                }
            }
        }
    }

    void buildTree(){
        literal_tree.build();
        offset_tree.build();
        length_tree.build();
    }

    std::vector<uint8_t> encode(const std::vector<Triple>& triples){
        std::vector<uint8_t> out;
        utils::BitWriter bw(out);

        const auto& literal_codes = literal_tree.getCodes();
        const auto& offset_codes = offset_tree.getCodes();
        const auto& length_codes = length_tree.getCodes();

        auto writeCode = [&](const Huffman::Code& code) {
            bw.writeBits(code.bits, code.length);
        };

        for (const Triple& t : triples) {
            if (t.offset == 0) {
                bw.writeBits(1, 1);
                writeCode(literal_codes[t.literal]);
            } else {
                bw.writeBits(0, 1);
                encodeMultiLevel(t.offset, config.offset_bits, offset_codes, config.offset_chunk_bits, bw);
                encodeMultiLevel(t.length, config.length_bits, length_codes, config.length_chunk_bits, bw);
            }
        }

        return bw.getOutput();
    }

    std::vector<Triple> decode(const std::vector<uint8_t>& data){
        utils::BitReader br(data);
        std::vector<Triple> result;
        uint64_t bit;

        while (br.getBytePos() < data.size()) {
            br.readBits(bit, 1);
            if (bit == 1) {
                uint8_t literal = static_cast<uint8_t>(literal_tree.decodeSymbol(br));
                result.emplace_back(0, 1, literal);
            } else {
                uint32_t offset = decodeMultiLevel(offset_tree, config.offset_bits, config.offset_chunk_bits, br);
                uint32_t length = decodeMultiLevel(length_tree, config.length_bits, config.length_chunk_bits, br);
                result.emplace_back(offset, length, 0);
            }
        }

        return result;
    }
};

}  // namespace compressor::algorithm