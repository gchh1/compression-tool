#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor::algorithm {

class ZstdCompress : public AlgorithmBase {
public:
    ZstdCompress(int compression_level = 3);

    auto reset(void) -> void override;

    auto compress(const std::vector<uint8_t>& data) -> std::vector<uint8_t>;
    static auto compress(const std::vector<uint8_t>& data, int level) -> std::vector<uint8_t>;

protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

private:
    int compression_level_;
    std::vector<uint8_t> input_buffer_;
    std::vector<uint8_t> output_buffer_;
    bool finished_{false};

    static constexpr uint32_t MAGIC = 0xFD2FB528;
    static constexpr uint8_t VERSION = 5;
};

class ZstdDecompress : public AlgorithmBase {
public:
    ZstdDecompress();

    auto reset(void) -> void override;

    auto decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t>;

protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

private:
    std::vector<uint8_t> input_buffer_;
    std::vector<uint8_t> output_buffer_;
    size_t input_pos_{0};
    bool finished_{false};

    auto readFrameHeader(const uint8_t* data, size_t size, size_t& pos) -> bool;
};

}
