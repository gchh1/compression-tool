#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "LZDP.hpp"
#include "LZencoding.hpp"

using namespace compressor::algorithm;

int main() {
    std::vector<uint8_t> input = { 'h','e','l','l','o' };

    LZDPConfig cfg(15, 15, 3, models::MatchEngine::HashChain, true);

    LZDP lzdp(cfg);
    std::vector<models::DPNode> dp;
    dp.assign(input.size(), models::DPNode{});
    dp[0] = models::DPNode(0, 0, -1, Triple(0, 1, input[0]));

    printf("Before dpforward:\n");
    for (size_t i = 0; i < dp.size(); ++i) {
        printf("  dp[%zu]: lit=%zu match=%zu pre=%d triple(%u,%u,%u)\n",
               i, dp[i].literal_count, dp[i].match_count, dp[i].pre_pos,
               dp[i].triple.offset, dp[i].triple.length, (unsigned)dp[i].triple.literal);
    }

    VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp, 0, input.size());

    printf("\nAfter dpforward:\n");
    for (size_t i = 0; i < dp.size(); ++i) {
        printf("  dp[%zu]: lit=%zu match=%zu pre=%d triple(%u,%u,%u)\n",
               i, dp[i].literal_count, dp[i].match_count, dp[i].pre_pos,
               dp[i].triple.offset, dp[i].triple.length, (unsigned)dp[i].triple.literal);
    }

    int cur_pos = 0;
    auto triples = lzdp.dpbacktrack(dp, cur_pos, 0);

    printf("\nBacktrack: triples=%zu\n", triples.size());
    for (size_t i = 0; i < triples.size(); ++i) {
        printf("  [%zu] offset=%u len=%u lit=%u\n", i,
               triples[i].offset, triples[i].length, (unsigned)triples[i].literal);
    }

    return 0;
}
