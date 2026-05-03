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
#include <unordered_map>

#include "IAlgorithm.hpp"

namespace compressor::core {

enum class AlgorithmID { None, Deflate, Inflate, DeltaEncode, DeltaDecode };

auto createAlgorithm(AlgorithmID id) -> std::unique_ptr<algorithm::IAlgorithm>;

inline AlgorithmID getDecompressorID(AlgorithmID algo) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::None, AlgorithmID::None},
        {AlgorithmID::Deflate, AlgorithmID::Inflate},
        {AlgorithmID::Inflate, AlgorithmID::Deflate},
        {AlgorithmID::DeltaEncode, AlgorithmID::DeltaDecode},
        {AlgorithmID::DeltaDecode, AlgorithmID::DeltaEncode},
    };
    return map.at(algo);
}

}  // namespace compressor::core