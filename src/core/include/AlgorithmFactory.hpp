/**
 * @file AlgorithmFactory.hpp
 * @author yhc
 * @brief
 * @version 0.1
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <memory>

#include "IAlgorithm.hpp"

namespace compressor::core {

enum class AlgorithmID { None, Deflate, Inflate, DeltaEncode, DeltaDecode };

auto createAlgorithm(AlgorithmID id) -> std::unique_ptr<algorithm::IAlgorithm>;

inline AlgorithmID getDecompressorID(AlgorithmID comp) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::Deflate, AlgorithmID::Inflate},
    };
    return map.at(comp);
}

inline AlgorithmID getPostpressorID(AlgorithmID pre) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::None, AlgorithmID::None},
        {AlgorithmID::DeltaEncode, AlgorithmID::DeltaDecode},
    };
    return map.at(pre);
}

}  // namespace compressor::core