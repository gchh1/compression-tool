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
                          compressor::utils::BitWriter& bw) const {
        size_t num_chunks = (value_bits + chunk_bits - 1) / chunk_bits;
        const size_t mask = (size_t{1} << chunk_bits) - 1;
        for (size_t i = 0; i < num_chunks; ++i) {
            size_t chunk = (static_cast<size_t>(value) >> (i * chunk_bits)) & mask;
            bw.writeBits(codes[chunk].bits, codes[chunk].length);
        }
    }

    uint32_t decodeMultiLevel(const Huffman& tree, size_t value_bits,
                              size_t chunk_bits, compressor::utils::BitReader& reader) const {
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

    const Huffman& getLiteralTree() const { return literal_tree; }
    const Huffman& getOffsetTree() const { return offset_tree; }
    const Huffman& getLengthTree() const { return length_tree; }

    void countFreq(const std::vector<Triple>& triples){
        for (Triple t : triples){
            if (t.offset == 0){
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
        auto lit_tree_data = literal_tree.encode();
        auto off_tree_data = offset_tree.encode();
        auto len_tree_data = length_tree.encode();

        std::vector<uint8_t> out;

        uint32_t count = static_cast<uint32_t>(triples.size());
        out.push_back(static_cast<uint8_t>(count & 0xFF));
        out.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((count >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((count >> 24) & 0xFF));

        uint16_t lit_sz = static_cast<uint16_t>(lit_tree_data.size());
        uint16_t off_sz = static_cast<uint16_t>(off_tree_data.size());
        uint16_t len_sz = static_cast<uint16_t>(len_tree_data.size());
        out.push_back(static_cast<uint8_t>(lit_sz & 0xFF));
        out.push_back(static_cast<uint8_t>(lit_sz >> 8));
        out.push_back(static_cast<uint8_t>(off_sz & 0xFF));
        out.push_back(static_cast<uint8_t>(off_sz >> 8));
        out.push_back(static_cast<uint8_t>(len_sz & 0xFF));
        out.push_back(static_cast<uint8_t>(len_sz >> 8));
        out.insert(out.end(), lit_tree_data.begin(), lit_tree_data.end());
        out.insert(out.end(), off_tree_data.begin(), off_tree_data.end());
        out.insert(out.end(), len_tree_data.begin(), len_tree_data.end());

        compressor::utils::BitWriter bw(out);

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

        auto buf = bw.getBuf();
        if (buf.count > 0) {
            int pad = 8 - buf.count;
            buf.buf <<= pad;
            out.push_back(static_cast<uint8_t>(buf.buf & 0xFFu));
        }

        return out;
    }

    std::vector<Triple> decode(const std::vector<uint8_t>& data){
        if (data.size() < 10) return {};

        uint32_t triple_count = static_cast<uint32_t>(data[0])
            | (static_cast<uint32_t>(data[1]) << 8)
            | (static_cast<uint32_t>(data[2]) << 16)
            | (static_cast<uint32_t>(data[3]) << 24);

        uint16_t lit_sz = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
        uint16_t off_sz = static_cast<uint16_t>(data[6]) | (static_cast<uint16_t>(data[7]) << 8);
        uint16_t len_sz = static_cast<uint16_t>(data[8]) | (static_cast<uint16_t>(data[9]) << 8);

        size_t header_end = 10 + static_cast<size_t>(lit_sz) + static_cast<size_t>(off_sz) + static_cast<size_t>(len_sz);
        if (data.size() < header_end) return {};

        if (lit_sz > 0) {
            std::vector<uint8_t> lit_data(data.begin() + 10, data.begin() + 10 + lit_sz);
            literal_tree.decode(lit_data);
        }
        if (off_sz > 0) {
            std::vector<uint8_t> off_data(data.begin() + 10 + lit_sz, data.begin() + 10 + lit_sz + off_sz);
            offset_tree.decode(off_data);
        }
        if (len_sz > 0) {
            std::vector<uint8_t> len_data(data.begin() + 10 + lit_sz + off_sz, data.begin() + header_end);
            length_tree.decode(len_data);
        }

        std::vector<uint8_t> triple_data(data.begin() + static_cast<std::ptrdiff_t>(header_end), data.end());
        compressor::utils::BitReader br(triple_data);
        std::vector<Triple> result;

        for (uint32_t i = 0; i < triple_count; ++i) {
            uint64_t bit;
            br.readBits(bit, 1);
            if (bit == 1) {
                uint8_t literal = static_cast<uint8_t>(literal_tree.decodeFromMap(br));
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