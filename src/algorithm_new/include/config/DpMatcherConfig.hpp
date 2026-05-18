#pragma once

#include <cstddef>

#include "Models.hpp"

namespace compressor::algorithm::config {

struct DpMatcherConfig {
    size_t dp_top;
    models::MatchEngine match_engine;

    DpMatcherConfig(
        size_t dp = 3,
        models::MatchEngine me = models::MatchEngine::HashChain)
        : dp_top(dp), match_engine(me) {}
};

}  // namespace compressor::algorithm::config