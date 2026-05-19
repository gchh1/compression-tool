#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::utils {

struct _buffer{
    uint64_t buf;
    int count;
    _buffer(uint64_t buf=0, int count=0) : buf(buf), count(count) {}
};
// LSB-first bit processor
class BitWriter {
    std::vector<uint8_t>& out_;
    _buffer buffer;
    void buf(uint8_t byte, int nbits=8){
        buffer.buf<<=nbits;
        buffer.buf |= static_cast<uint64_t>(byte);
        buffer.count += nbits;
    }
    void flush(){
        if (buffer.count >=8){
            //获取高8位
            uint8_t byte = static_cast<uint8_t>(buffer.buf >> (buffer.count - 8));
            out_.push_back(byte);
            buffer.count -= 8;
            // 截断清除高8位
            buffer.buf &= (1ULL << buffer.count) - 1;
        }
    }

public:
    explicit BitWriter(std::vector<uint8_t>& out, _buffer buf = utils::_buffer()) : out_(out), buffer(buf) {}

    void writeBits(uint64_t value, int nbits) {
        /*
        端序说明：
        原bit流(假设编码9位)：0101 1111 0|000 1111 10|11 1100...
        out_: 01011111 0|0001111 10|111100...
        */
        if (nbits <= 0 || nbits > 64) return;
        while (nbits >= 8) {
            uint8_t byte = static_cast<uint8_t>((value >> (nbits - 8)) & 0xFF);
            buf(byte);
            flush();
            nbits -= 8;
        }
        if (nbits > 0) {
            uint8_t byte = static_cast<uint8_t>(value & ((1ULL << nbits) - 1));
            buf(byte, nbits);
            flush();
        }
    }
    void setBuf(_buffer buf){
        buffer = buf;
    }
    _buffer getBuf() const { return buffer; }
    std::vector<uint8_t> getOutput()const {
        return out_;
    }

    int getPendingBits() const { return buffer.count; }
};

class BitReader{
    const std::vector<uint8_t>& data_;
    size_t byte_pos_{0};

    _buffer buffer;

    void fill(){//
        while (buffer.count <= 56 && byte_pos_ < data_.size()){
            buffer.buf<<=8;
            buffer.buf |= static_cast<uint64_t>(data_[byte_pos_++]);
            buffer.count += 8;
        }
    }
public:
    explicit BitReader(const std::vector<uint8_t>& data, _buffer buf = _buffer())
        : data_(data), buffer(buf) {}
    void setBuf(_buffer buf){
        buffer = buf;
    }
    _buffer getBuf() const { return buffer; }

    void readBits(uint64_t &value, const int& nbits){
        if (nbits <= 0 || nbits > 64) return;
        fill();
        uint64_t mask = (1ULL << (nbits)) - 1;
        mask <<= (buffer.count - nbits);
        value = (buffer.buf & mask) >> (buffer.count - nbits);
        buffer.count -= nbits;
        buffer.buf &= (1ULL << buffer.count) - 1;
    }
    int getPendingBits() const { return buffer.count; }
    size_t getBytePos() const { return byte_pos_; }

    bool ensureBits(int needed) {
        fill();
        return buffer.count >= needed;
    }

    void reset() {
        buffer.count = 0;
        buffer.buf = 0;
    }
};

}  // namespace compressor::utils