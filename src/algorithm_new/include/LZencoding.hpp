#pragma once

#include <cstdint>
#include <vector>

#include "BitProcessor.hpp"

namespace compressor::algorithm {

struct Triple {
    uint32_t offset;
    uint32_t length;
    uint8_t literal;
    Triple(uint32_t offset = 0, uint32_t length = 0, uint8_t literal = 0)
        : offset(offset), length(length), literal(literal) {}
};

struct EncodingConfig {
    uint8_t offset_bits;
    uint8_t length_bits;
    bool use_flag_encoding;
};

// ==================== 非流式（一次性） ====================


inline std::vector<Triple> literalrun(const std::vector<Triple>& triples, const size_t look_size){
    std::vector<Triple> buf;
    std::vector<Triple> result;
    auto flush = [&]{
        if (buf.empty()){
            return;
        }
        size_t size = buf.size();
        size_t buf_offset = 0;
        while (size>=look_size){
            result.push_back(Triple(0,look_size,0));
            size -= look_size;
            for (size_t i = 0; i < look_size; i++){
                result.push_back(buf[buf_offset + i]);
            }
            buf_offset += look_size;
        }
        if (size>0){
            result.push_back(Triple(0,size,0));
            for (size_t i = 0; i < size; i++){
                result.push_back(buf[buf_offset + i]);
            }
        }
        buf.clear();
    };

    for (const Triple &triple:triples){
        if (triple.offset == 0){
            buf.push_back(triple);
        }else{
            flush();
            result.push_back(triple);
        }

    }
    //清理最后残留的buf
    flush();
    return result;
}



inline std::vector<uint8_t> writetriple(
    const std::vector<algorithm::Triple>& triples,
    const EncodingConfig& config,
    compressor::utils::_buffer& buffer
){
    // 如果是非flag编码，这里接受的triples是已经经过literalrun处理后的
    std::vector<uint8_t> result;
    compressor::utils::BitWriter writer(result, buffer);

    uint64_t val;
    if (config.use_flag_encoding){
        for (const algorithm::Triple &triple:triples){
            if (triple.offset == 0){
                val = static_cast<uint64_t>(triple.literal);
                val |= 1ULL<<8;// 标记为字面值
                writer.writeBits(val, 9);
               
            }else{
                val = static_cast<uint64_t>(triple.offset);
                val<<=config.length_bits;
                val|=static_cast<uint64_t>(triple.length);
                val&=(1ULL<<(config.offset_bits+config.length_bits))-1;//保证高位为0
                writer.writeBits(val, config.offset_bits+config.length_bits+1);
            }
        }
    }else{
        for (const algorithm::Triple &triple:triples){
            if (triple.offset==0 && triple.length==1){
                val = static_cast<uint64_t>(triple.literal);
                writer.writeBits(val, 8);
            }else{
                val = static_cast<uint64_t>(triple.offset);
                val<<=config.length_bits;
                val|= static_cast<uint64_t>(triple.length);
                writer.writeBits(val, config.offset_bits+config.length_bits);
            }
        }
    }
    buffer = writer.getBuf();
    return writer.getOutput();
}

inline std::vector<Triple> readtriple(
    const std::vector<uint8_t>& input,
    const EncodingConfig& config,
    compressor::utils::_buffer& buffer
){
    compressor::utils::BitReader reader(input, buffer);
    std::vector<Triple> result;
    uint64_t val;

    //用于非flag编码，记录当前字面值的长度
    size_t literal_size = 0;

    while (reader.getBytePos() < input.size()){
        if (config.use_flag_encoding){
           uint32_t offset;
           uint32_t length;
           uint8_t literal;
           reader.readBits(val, 1);
           if (val == 1){// 字面值
                reader.readBits(val, 8);
                literal = static_cast<uint8_t>(val);
                result.push_back(algorithm::Triple(0,1,literal));
           }else{
                reader.readBits(val, config.offset_bits+config.length_bits);
                offset = static_cast<uint32_t>(val>>(config.length_bits));
                val &= (1ULL<<(config.length_bits)-1);
                length = static_cast<uint32_t>(val);
                result.push_back(algorithm::Triple(offset,length,0));
            }

        }
        else{
            uint32_t offset;
            uint32_t length;
            uint8_t literal;
            if (literal_size == 0){
                reader.readBits(val, config.offset_bits+config.length_bits);
                offset = static_cast<uint32_t>(val>>(config.length_bits));
                if (offset == 0){//返回的结果是不包含literalrun
                    continue;
                }
                val &= (1ULL<<(config.length_bits)-1);
                length = static_cast<uint32_t>(val);
                result.push_back(algorithm::Triple(offset,length,0));
            }else{
                reader.readBits(val, 8);
                literal = static_cast<uint8_t>(val);
                result.push_back(algorithm::Triple(0,1,literal));
                literal_size--;
            }
            if (offset == 0){
                literal_size=length;
            }
        }

    }
    buffer = reader.getBuf();
    return result;

}
// std::vector<uint8_t> encode_triple(
//     const std::vector<Triple>& triples,
//     const EncodingConfig& config,
//     compressor::utils::_buffer& buffer = compressor::utils::_buffer()
// ){
  
// }



inline std::vector<uint8_t> decode_triple(const std::vector<Triple>& triples, const EncodingConfig& config, compressor::utils::_buffer& buffer){
    std::vector<uint8_t> result;
    compressor::utils::BitWriter writer(result, buffer);
    uint64_t val;
    for (const Triple &triple:triples){
        if (config.use_flag_encoding){
            if (triple.offset == 0 && triple.length == 1){
                val = static_cast<uint64_t>(triple.literal);
                writer.writeBits(val, 8);
            }else{
                val = static_cast<uint64_t>(triple.offset);
                val<<=config.length_bits;
                val|=static_cast<uint64_t>(triple.length);
                
                writer.writeBits(val, config.offset_bits+config.length_bits+1);
            }
        }else{
            if (triple.offset == 0 && triple.length == 1){
                val = static_cast<uint64_t>(triple.literal);
                writer.writeBits(val, 8);
            }else{
                val = static_cast<uint64_t>(triple.offset);
                val<<=config.length_bits;
                val|=static_cast<uint64_t>(triple.length);
                writer.writeBits(val, config.offset_bits+config.length_bits);
            }
        }
    }
    buffer = writer.getBuf();
    return writer.getOutput();
}








// ==============用于流式中间文件的编码
//Add namespace prefix to Triple to match encoding
inline std::vector<uint8_t> triple2u8(const std::vector<Triple>& input, compressor::utils::_buffer& buffer){
    std::vector<uint8_t> result;
    compressor::utils::BitWriter writer(result, buffer);
    uint32_t offset;
    uint32_t length;
    uint8_t literal;
    for (const Triple &triple:input){
        offset = triple.offset;
        length = triple.length;
        literal = triple.literal;
        writer.writeBits(offset, 32);
        writer.writeBits(length, 32);
        writer.writeBits(literal, 8);
    }
    buffer = writer.getBuf();
    return writer.getOutput();
}
//Add namespace prefix to Triple to match encoding
inline std::vector<Triple> u82triple(const std::vector<uint8_t>& input, compressor::utils::_buffer& buffer){
    compressor::utils::BitReader reader(input, buffer);
    std::vector<algorithm::Triple> result;
    uint64_t val;
    while (reader.getBytePos() < input.size()){
        reader.readBits(val, 32);
        uint32_t offset = static_cast<uint32_t>(val);
        reader.readBits(val, 32);
        uint32_t length = static_cast<uint32_t>(val);
        reader.readBits(val, 8);
        uint8_t literal = static_cast<uint8_t>(val);
        result.push_back(algorithm::Triple(offset,length,literal));
    }
    buffer = reader.getBuf();
    return result;
}



// ======================



// ==================== 流式 ====================

struct EncodeState {
    EncodingConfig config;
    uint64_t bit_buf{0};
    int bit_count{0};
    bool header_written{false};
};





}  // namespace compressor::algorithm