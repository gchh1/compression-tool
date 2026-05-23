#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "DPFlate.hpp"

namespace {

template <typename T>
auto compress_to_end(T& enc, const std::vector<uint8_t>& input,
                     std::vector<uint8_t>& out)
    -> compressor::algorithm::AlgorithmStatus {
    enc.reset();
    size_t in_off = 0;
    size_t out_pos = 0;
    out.resize(std::max(input.size() * 2 + 65536, size_t{4096}));
    compressor::algorithm::AlgorithmStatus st{};

    for (;;) {
        if (out_pos >= out.size()) {
            out.resize(std::max(out.size() * 2, out_pos + input.size() + 65536));
        }

        const auto in_span = std::span<const uint8_t>(
            input.data() + in_off, input.size() - in_off);
        const auto out_span =
            std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);

        st = enc.process(in_span, out_span, in_off + in_span.size() >= input.size());
        in_off += st.bytes_consumed;
        out_pos += st.bytes_produced;

        if (st.done) break;
        if (st.need_output && st.bytes_produced == 0) {
            out.resize(std::max(out.size() * 2, out_pos + input.size() + 65536));
            continue;
        }
        if (st.need_input && in_off >= input.size()) break;
    }

    out.resize(out_pos);
    return st;
}

std::vector<uint8_t> read_stdin() {
    std::vector<uint8_t> buf;
    int ch;
    while ((ch = std::getchar()) != EOF) {
        buf.push_back(static_cast<uint8_t>(ch));
    }
    return buf;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 9) {
        std::fprintf(stderr, "Usage: %s search_size lookahead min_match dp_top dp_sub_match_max use_3hm huffman_chunk_bits input_file output_file\n", argv[0]);
        return 1;
    }

    const size_t search_size    = static_cast<size_t>(std::atoll(argv[1]));
    const size_t lookahead_size = static_cast<size_t>(std::atoll(argv[2]));
    const size_t min_match      = static_cast<size_t>(std::atoll(argv[3]));
    const size_t dp_top         = static_cast<size_t>(std::atoll(argv[4]));
    const size_t dp_sub_match_max = static_cast<size_t>(std::atoll(argv[5]));
    const bool   use_3hm        = std::atoi(argv[6]) != 0;
    const size_t huffman_chunk  = static_cast<size_t>(std::atoll(argv[7]));
    const char*  input_file     = argv[8];
    const char*  output_file    = argv[9];

    std::vector<uint8_t> input;
    {
        std::ifstream fin(input_file, std::ios::binary | std::ios::ate);
        if (!fin) {
            std::fprintf(stderr, "[OLD-TOOL] cannot open input: %s\n", input_file);
            return 1;
        }
        const auto sz = fin.tellg();
        fin.seekg(0);
        input.resize(static_cast<size_t>(sz));
        fin.read(reinterpret_cast<char*>(input.data()), static_cast<std::streamsize>(sz));
    }
    std::fprintf(stderr, "[OLD-TOOL] read %zu bytes, compressing...\n", input.size());

    compressor::algorithm::DPFlate dpflate(
        search_size, lookahead_size, min_match, dp_top, dp_sub_match_max);
    dpflate.set_use_3hfmtree(use_3hm);
    dpflate.set_huffman_offset_chunk_bits(huffman_chunk);
    dpflate.set_huffman_length_chunk_bits(huffman_chunk);

    std::vector<uint8_t> out;
    (void)compress_to_end(dpflate, input, out);

    {
        std::ofstream fout(output_file, std::ios::binary | std::ios::trunc);
        if (!fout) {
            std::fprintf(stderr, "[OLD-TOOL] cannot open output: %s\n", output_file);
            return 1;
        }
        fout.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    std::fprintf(stderr, "[OLD-TOOL] wrote %zu bytes to %s\n", out.size(), output_file);
    return 0;
}