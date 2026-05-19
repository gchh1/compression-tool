#pragma once

#include <unordered_map>

#include "Huffman.hpp"
#include "BitProcessor.hpp"
#include "Models.hpp"

namespace compressor::algorithm{

struct Huffman_3HfMTConfig{
    size_t huffman_offset_bitwidth;
    size_t huffman_length_bitwidth;
};

class Huffman_3HfMT{
    const size_t litearal_bitwidth{8};
    Huffman_3HfMTConfig config;
    EncodingConfig encoding_config;

    size_t offset_remainder;
    size_t length_remainder;

    Huffman literal_tree, offset_tree, length_tree;

    void encodeMultiLevel(uint32_t value, size_t total_bits, size_t chunk_bits, size_t remainder,
                          const std::vector<Huffman::Code>& codes,
                          compressor::utils::BitWriter& bw) const {
        size_t remaining = total_bits;

        remaining -= remainder;
        bw.writeBits(codes[value >> remaining].bits, codes[value >> remaining].length);
        value &= (1 << remaining) - 1;

        while (remaining >= chunk_bits) {
            remaining -= chunk_bits;
            bw.writeBits(codes[value >> remaining].bits, codes[value >> remaining].length);
            value &= (1 << remaining) - 1;
        }
    }

    uint32_t decodeMultiLevel(const Huffman& tree, size_t total_bits, size_t chunk_bits, size_t remainder, compressor::utils::BitReader& reader) const {
        size_t remaining = total_bits;
        uint32_t value = 0;

        remaining -= remainder;
        value = tree.decodeFromMap(reader) << remaining;

        while (remaining >= chunk_bits) {
            remaining -= chunk_bits;
            value |= tree.decodeFromMap(reader) << remaining;
        }
        return value;
    }

public:
    explicit Huffman_3HfMT(Huffman_3HfMTConfig config,EncodingConfig encoding_config)
        : config(config), encoding_config(encoding_config){
            size_t dividend = (config.huffman_offset_bitwidth ) / encoding_config.offset_bits;
            offset_remainder = encoding_config.offset_bits - dividend * encoding_config.offset_bits;
            dividend = (config.huffman_length_bitwidth ) / encoding_config.length_bits;
            length_remainder = encoding_config.length_bits - dividend * encoding_config.length_bits;
        }
    ~Huffman_3HfMT() = default;
    
    void countFreq(const std::vector<Triple>& triples){
        for (Triple t : triples){
            if (t.offset == 0){
                literal_tree.addSymbol(t.literal);
            }else{
                size_t tmp = encoding_config.offset_bits;
                uint32_t offset = t.offset;

                tmp -= offset_remainder;
                offset_tree.addSymbol(static_cast<uint32_t>(offset>>(tmp)));
                offset &= (1<<tmp) -1;
                while(tmp>=config.huffman_offset_bitwidth){//直到为0
                    tmp -= config.huffman_offset_bitwidth;
                    offset_tree.addSymbol(static_cast<uint32_t>(offset>>(tmp)));
                    offset &= (1<<tmp) -1;
                }

                tmp = encoding_config.length_bits;
                uint32_t length = t.length;
  
                tmp -= length_remainder;
                length_tree.addSymbol(static_cast<uint32_t>(length>>(tmp)));
                length &= (1<<tmp) -1;
                while(tmp>=config.huffman_length_bitwidth){//直到为0
                    tmp -= config.huffman_length_bitwidth;
                    length_tree.addSymbol(static_cast<uint32_t>(length>>(tmp)));
                    length &= (1<<tmp) -1;
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

        const auto& literal_map = literal_tree.getCodes();
        const auto& offset_map = offset_tree.getCodes();
        const auto& length_map = length_tree.getCodes();

        auto writeCode = [&](const Huffman::Code& code) {
            bw.writeBits(code.bits, code.length);
        };

        for (const Triple& t : triples) {
            if (t.offset == 0) {
                bw.writeBits(1, 1);
                writeCode(literal_map[t.literal]);
            } else {
                bw.writeBits(0, 1);
                encodeMultiLevel(t.offset, encoding_config.offset_bits, config.huffman_offset_bitwidth, offset_remainder, offset_map, bw);
                encodeMultiLevel(t.length, encoding_config.length_bits, config.huffman_length_bitwidth, length_remainder, length_map, bw);
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
                uint32_t offset = decodeMultiLevel(offset_tree, encoding_config.offset_bits, config.huffman_offset_bitwidth, offset_remainder, br);
                uint32_t length = decodeMultiLevel(length_tree, encoding_config.length_bits, config.huffman_length_bitwidth, length_remainder, br);
                result.emplace_back(offset, length, 0);
            }
        }

        return result;
    }
};

}  // namespace compressor::algorithm