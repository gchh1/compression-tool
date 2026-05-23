#include "test_deflate_old_wrap.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include "Deflate.hpp"

using namespace compressor::algorithm;

extern "C" {

OldDeflateResult* old_deflate_run_nonstreaming(
    const uint8_t* input, size_t input_len,
    size_t slide_size, size_t min_match, size_t max_chain_length,
    size_t lookahead_max, int use_flag_encoding)
{
    auto* result = new OldDeflateResult();
    result->steps = nullptr;
    result->count = 0;
    result->compressed = nullptr;
    result->compressed_size = 0;
    result->valid = 0;

    try {
        Deflate deflate(slide_size, min_match, max_chain_length,
                        lookahead_max, use_flag_encoding != 0);

        deflate.reset();

        std::vector<Token> all_tokens;
        std::vector<uint8_t> output;

        size_t in_off = 0;
        size_t out_pos = 0;

        output.resize(std::max(input_len * 2 + 65536, size_t{4096}));

        for (;;) {
            if (out_pos >= output.size()) {
                output.resize(std::max(output.size() * 2,
                                       out_pos + input_len + 65536));
            }
            auto in_span = std::span<const uint8_t>(
                input + in_off, input_len - in_off);
            auto out_span = std::span<uint8_t>(
                output.data() + out_pos, output.size() - out_pos);

            bool is_last = (in_off + in_span.size() >= input_len);

            auto prev_state = deflate.state();
            auto st = deflate.process(in_span, out_span, is_last);
            auto cur_state = deflate.state();

            if (prev_state == Deflate::DeflateState::FIND_MATCHES &&
                cur_state == Deflate::DeflateState::BUILD_TREE) {
                const auto& tokens = deflate.tokens();
                all_tokens.insert(all_tokens.end(), tokens.begin(), tokens.end());
            }
            if (st.need_output && cur_state == Deflate::DeflateState::FLUSH_TOKENS) {
                const auto& tokens = deflate.tokens();
                if (!tokens.empty()) {
                    all_tokens.insert(all_tokens.end(), tokens.begin(), tokens.end());
                }
            }

            in_off += st.bytes_consumed;
            out_pos += st.bytes_produced;

            if (st.done) {
                const auto& tokens = deflate.tokens();
                if (!tokens.empty()) {
                    all_tokens.insert(all_tokens.end(), tokens.begin(), tokens.end());
                }
                break;
            }

            if (st.need_output && st.bytes_produced == 0) {
                output.resize(std::max(output.size() * 2,
                                       out_pos + input_len + 65536));
                continue;
            }
            if (st.need_input && in_off >= input_len) {
                auto out_span2 = std::span<uint8_t>(
                    output.data() + out_pos, output.size() - out_pos);
                st = deflate.process(std::span<const uint8_t>{}, out_span2, true);
                out_pos += st.bytes_produced;
                const auto& tokens = deflate.tokens();
                if (!tokens.empty()) {
                    all_tokens.insert(all_tokens.end(), tokens.begin(), tokens.end());
                }
                break;
            }
        }

        output.resize(out_pos);

        result->count = all_tokens.size();
        if (result->count > 0) {
            result->steps = new OldDeflateLZStep[result->count];
            for (size_t i = 0; i < result->count; ++i) {
                const auto& t = all_tokens[i];
                result->steps[i].is_literal = t.is_literal ? 1 : 0;
                if (t.is_literal) {
                    result->steps[i].lit_val = t.code;
                    result->steps[i].match_len = 0;
                    result->steps[i].match_dist = 0;
                } else {
                    result->steps[i].lit_val = 0;
                    result->steps[i].match_len = t.match_len;
                    result->steps[i].match_dist = t.match_dist;
                }
            }
        }

        result->compressed_size = output.size();
        if (result->compressed_size > 0) {
            result->compressed = new uint8_t[result->compressed_size];
            std::memcpy(result->compressed, output.data(), result->compressed_size);
        }

        result->valid = 1;
    } catch (...) {
        result->valid = 0;
    }

    return result;
}

void old_deflate_free_result(OldDeflateResult* r) {
    if (!r) return;
    delete[] r->steps;
    delete[] r->compressed;
    delete r;
}

}