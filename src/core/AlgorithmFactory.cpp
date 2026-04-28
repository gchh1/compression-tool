/**
 * @file AlgorithmFactory.cpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AlgorithmFactory.hpp"

#include <memory>

#include "Deflate.hpp"
#include "Inflate.hpp"

namespace compressor::core {

auto createAlgorithm(AlgorithmID id) -> std::unique_ptr<algorithm::IAlgorithm> {
    switch (id) {
        case AlgorithmID::Deflate:
            return std::make_unique<algorithm::Deflate>();
        case AlgorithmID::Inflate:
            return std::make_unique<algorithm::Inflate>();
    }
}

}  // namespace compressor::core