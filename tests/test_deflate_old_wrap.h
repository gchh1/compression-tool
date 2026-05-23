#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int is_literal;
    uint32_t lit_val;
    uint32_t match_len;
    uint32_t match_dist;
} OldDeflateLZStep;

typedef struct {
    OldDeflateLZStep* steps;
    size_t count;
    uint8_t* compressed;
    size_t compressed_size;
    int valid;
} OldDeflateResult;

OldDeflateResult* old_deflate_run_nonstreaming(
    const uint8_t* input, size_t input_len,
    size_t slide_size, size_t min_match, size_t max_chain_length,
    size_t lookahead_max, int use_flag_encoding);

void old_deflate_free_result(OldDeflateResult* r);

#ifdef __cplusplus
}
#endif