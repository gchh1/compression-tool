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
                          utils::BitWriter& bw) const {
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

    uint32_t decodeMultiLevel(const Huffman& tree, size_t total_bits, size_t chunk_bits, size_t remainder, utils::BitReader& reader) const {
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
    
    void countFreq(const std::vector<Triple>& triples){//这个triple是literalrun之后的结果
        for (Triple t : triples){
            if (t.offset==0&&t.length==0){
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
        std::vector<uint8_t> out;
        utils::BitWriter bw(out);

        const auto& literal_map = literal_tree.getCodes();
        const auto& offset_map = offset_tree.getCodes();
        const auto& length_map = length_tree.getCodes();

        auto writeCode = [&](const Huffman::Code& code) {
            bw.writeBits(code.bits, code.length);
        };

        for (const Triple& t : triples) {
            if (t.offset == 0) {
                writeCode(literal_map[t.literal]);
            } else {
                encodeMultiLevel(t.offset, encoding_config.offset_bits, config.huffman_offset_bitwidth, offset_remainder, offset_map, bw);
                encodeMultiLevel(t.length, encoding_config.length_bits, config.huffman_length_bitwidth, length_remainder, length_map, bw);
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