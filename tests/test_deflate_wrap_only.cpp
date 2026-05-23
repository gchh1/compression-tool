#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include "test_deflate_old_wrap.h"

int main() {
    const char* pat = "REGRESS_LZDP_DPFLATE_COMPARE_TEST_V1";
    std::vector<uint8_t> input;
    for (int i = 0; i < 16; ++i) {
        input.insert(input.end(), pat, pat + 36);
    }

    fprintf(stderr, "Input: %zu bytes\n", input.size());

    auto* r = old_deflate_run_nonstreaming(
        input.data(), input.size(), 4096, 3, 256, 258, 0);

    fprintf(stderr, "valid=%d count=%zu compressed_size=%zu\n",
            r->valid, r->count, r->compressed_size);

    if (r->valid && r->compressed_size > 0) {
        fprintf(stderr, "First 20 compressed bytes: ");
        for (size_t i = 0; i < std::min(r->compressed_size, size_t{20}); ++i) {
            fprintf(stderr, "%02x ", r->compressed[i]);
        }
        fprintf(stderr, "\n");
    }

    if (r->count > 0) {
        fprintf(stderr, "First 5 tokens:\n");
        for (size_t i = 0; i < std::min(r->count, size_t{5}); ++i) {
            if (r->steps[i].is_literal) {
                fprintf(stderr, "  [%zu] LIT 0x%02x\n", i, r->steps[i].lit_val);
            } else {
                fprintf(stderr, "  [%zu] MATCH dist=%u len=%u\n",
                        i, r->steps[i].match_dist, r->steps[i].match_len);
            }
        }
    }

    old_deflate_free_result(r);
    fprintf(stderr, "SUCCESS\n");
    return 0;
}