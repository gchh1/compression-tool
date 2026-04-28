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

enum class AlgorithmID { Deflate, Inflate };

auto createAlgorithm(AlgorithmID id) -> std::unique_ptr<algorithm::IAlgorithm>;

}  // namespace compressor::core