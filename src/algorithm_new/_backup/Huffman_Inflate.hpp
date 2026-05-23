#pragma once

#include <unordered_map>

#include "Huffman.hpp"
#include "BitProcessor.hpp"
#include "Models.hpp"

namespace compressor::algorithm {

struct Huffman_InflateConfig {
    size_t offset_bits;
    size_t length_bits;
};

class Huffman_Inflate {
    const size_t literal_bitwidth{8};
    Huffman_InflateConfig config;
    EncodingConfig encoding_config;

    Huffman literal_tree, offset_tree, length_tree;

    enum class DecodeState {
        READ_FLAG,
        READ_LITERAL,
        READ_OFFSET,
        READ_LENGTH,
    };

    static void writeLe16(std::vector<uint8_t>& out, uint16_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }

    static void writeLe32(std::vector<uint8_t>& out, uint32_t v) {
        writeLe16(out, static_cast<uint16_t>(v & 0xFFFF));
        writeLe16(out, static_cast<uint16_t>(v >> 16));
    }

    void encodeHeader(std::vector<uint8_t>& out,
                      const std::vector<uint8_t>& lit_data,
                      const std::vector<uint8_t>& off_data,
                      const std::vector<uint8_t>& len_data,
                      uint32_t triple_count) const {
        writeLe32(out, triple_count);
        writeLe16(out, static_cast<uint16_t>(lit_data.size()));
        writeLe16(out, static_cast<uint16_t>(off_data.size()));
        writeLe16(out, static_cast<uint16_t>(len_data.size()));
        out.insert(out.end(), lit_data.begin(), lit_data.end());
        out.insert(out.end(), off_data.begin(), off_data.end());
        out.insert(out.end(), len_data.begin(), len_data.end());
    }

    void encodePayload(std::vector<uint8_t>& out,
                       const std::vector<Triple>& triples) const {
        compressor::utils::BitWriter bw(out);
        const auto& literal_codes = literal_tree.getCodes();
        const auto& offset_codes = offset_tree.getCodes();
        const auto& length_codes = length_tree.getCodes();

        for (const Triple& t : triples) {
            if (t.offset == 0) {
                bw.writeBits(1, 1);
                bw.writeBits(literal_codes[t.literal].bits,
                             literal_codes[t.literal].length);
            } else {
                bw.writeBits(0, 1);
                bw.writeBits(offset_codes[t.offset].bits,
                             offset_codes[t.offset].length);
                bw.writeBits(length_codes[t.length].bits,
                             length_codes[t.length].length);
            }
        }

        auto buf = bw.getBuf();
        if (buf.count > 0) {
            int pad = 8 - buf.count;
            buf.buf <<= pad;
            out.push_back(static_cast<uint8_t>(buf.buf & 0xFFu));
        }
    }

public:
    explicit Huffman_Inflate(Huffman_InflateConfig config,
                             EncodingConfig encoding_config)
        : config(config), encoding_config(encoding_config) {}

    ~Huffman_Inflate() = default;

    const Huffman& getLiteralTree() const { return literal_tree; }
    const Huffman& getOffsetTree() const { return offset_tree; }
    const Huffman& getLengthTree() const { return length_tree; }

    void countFreq(const std::vector<Triple>& triples) {
        for (const Triple& t : triples) {
            if (t.offset == 0) {
                literal_tree.addSymbol(t.literal);
            } else {
                offset_tree.addSymbol(t.offset);
                length_tree.addSymbol(t.length);
            }
        }
    }

    void buildTree() {
        literal_tree.build();
        offset_tree.build();
        length_tree.build();
    }

    std::vector<uint8_t> encode(const std::vector<Triple>& triples) {
        countFreq(triples);
        buildTree();

        auto lit_tree_data = literal_tree.encode();
        auto off_tree_data = offset_tree.encode();
        auto len_tree_data = length_tree.encode();

        std::vector<uint8_t> out;
        encodeHeader(out, lit_tree_data, off_tree_data, len_tree_data,
                     static_cast<uint32_t>(triples.size()));
        encodePayload(out, triples);
        return out;
    }

    std::vector<Triple> decode(const std::vector<uint8_t>& data) {
        if (data.size() < 10) return {};

        uint32_t triple_count =
            static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24);

        uint16_t lit_sz =
            static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
        uint16_t off_sz =
            static_cast<uint16_t>(data[6]) | (static_cast<uint16_t>(data[7]) << 8);
        uint16_t len_sz =
            static_cast<uint16_t>(data[8]) | (static_cast<uint16_t>(data[9]) << 8);

        size_t header_end =
            10 + static_cast<size_t>(lit_sz) + static_cast<size_t>(off_sz) +
            static_cast<size_t>(len_sz);
        if (data.size() < header_end) return {};

        if (lit_sz > 0) {
            std::vector<uint8_t> lit_data(data.begin() + 10,
                                          data.begin() + 10 + lit_sz);
            literal_tree.decode(lit_data);
        }
        if (off_sz > 0) {
            std::vector<uint8_t> off_data(
                data.begin() + 10 + lit_sz,
                data.begin() + 10 + lit_sz + off_sz);
            offset_tree.decode(off_data);
        }
        if (len_sz > 0) {
            std::vector<uint8_t> len_data(data.begin() + 10 + lit_sz + off_sz,
                                          data.begin() + header_end);
            length_tree.decode(len_data);
        }

        std::vector<uint8_t> payload(
            data.begin() + static_cast<std::ptrdiff_t>(header_end), data.end());
        compressor::utils::BitReader br(payload);
        std::vector<Triple> result;

        DecodeState state = DecodeState::READ_FLAG;
        uint32_t pending_offset = 0;
        uint32_t decoded_count = 0;

        while (decoded_count < triple_count) {
            switch (state) {
                case DecodeState::READ_FLAG: {
                    uint64_t bit;
                    br.readBits(bit, 1);
                    state = (bit == 1) ? DecodeState::READ_LITERAL
                                       : DecodeState::READ_OFFSET;
                    break;
                }
                case DecodeState::READ_LITERAL: {
                    uint32_t literal = literal_tree.decodeFromMap(br);
                    result.emplace_back(0, 1, static_cast<uint8_t>(literal));
                    decoded_count++;
                    state = DecodeState::READ_FLAG;
                    break;
                }
                case DecodeState::READ_OFFSET: {
                    pending_offset = offset_tree.decodeFromMap(br);
                    state = DecodeState::READ_LENGTH;
                    break;
                }
                case DecodeState::READ_LENGTH: {
                    uint32_t length = length_tree.decodeFromMap(br);
                    result.emplace_back(pending_offset, length, 0);
                    decoded_count++;
                    state = DecodeState::READ_FLAG;
                    break;
                }
            }
        }

        return result;
    }
};

}  // namespace compressor::algorithm