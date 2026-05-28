#pragma once

#include "ICompressor.hpp"

namespace compressor::core {

class VideoH264Compressor : public ICompressor {
public:
    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_quality(int v) { quality_ = v; }
    int get_quality() const { return quality_; }

private:
    int quality_{26};
};

class VideoOpenH264Compressor : public ICompressor {
public:
    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_quality(int v) { quality_ = v; }
    int get_quality() const { return quality_; }

private:
    int quality_{26};
};

}  // namespace compressor::core