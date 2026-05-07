#pragma once

#include <memory>
#include <unordered_map>

#include "IAlgorithm.hpp"

namespace compressor::core {

enum class AlgorithmID {
    None,
    Deflate,
    Inflate,
    DeltaEncode,
    DeltaDecode,
    LZSS,
    LZSSDecompress,
    LZMine,
    LZMineDecompress,
    MyFlate,
};

auto createAlgorithm(AlgorithmID id) -> std::unique_ptr<algorithm::IAlgorithm>;

inline AlgorithmID getDecompressorID(AlgorithmID comp) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::Deflate, AlgorithmID::Inflate},
        {AlgorithmID::LZSS, AlgorithmID::LZSSDecompress},
        {AlgorithmID::LZMine, AlgorithmID::LZMineDecompress},
    };
    auto it = map.find(comp);
    return it != map.end() ? it->second : AlgorithmID::None;
}

inline AlgorithmID getPostpressorID(AlgorithmID pre) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::None, AlgorithmID::None},
        {AlgorithmID::DeltaEncode, AlgorithmID::DeltaDecode},
    };
    auto it = map.find(pre);
    return it != map.end() ? it->second : AlgorithmID::None;
}

}
