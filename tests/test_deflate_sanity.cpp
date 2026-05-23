#include <cstdio>
#include <cstdint>
#include <span>
#include <vector>
#include "Deflate.hpp"

int main() {
    using namespace compressor::algorithm;

    const char* pat = "REGRESS_LZDP_DPFLATE_COMPARE_TEST_V1";
    std::vector<uint8_t> input;
    for (int i = 0; i < 16; ++i) {
        input.insert(input.end(), pat, pat + 36);
    }

    fprintf(stderr, "Input: %zu bytes\n", input.size());

    Deflate deflate(4096, 3, 256, 258, false);
    deflate.reset();

    std::vector<uint8_t> output;
    output.resize(input.size() * 2 + 65536);

    fprintf(stderr, "Calling process...\n");
    auto st = deflate.process(input, output, true);
    fprintf(stderr, "Done: consumed=%zu produced=%zu done=%d\n",
            st.bytes_consumed, st.bytes_produced, (int)st.done);

    output.resize(st.bytes_produced);
    fprintf(stderr, "Compressed: %zu bytes\n", output.size());

    fprintf(stderr, "SUCCESS\n");
    return 0;
}