#include "Zstd.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace compressor::algorithm {

namespace {

constexpr size_t WINDOW_LOG = 17;
constexpr size_t WINDOW_SIZE = 1 << WINDOW_LOG;
constexpr size_t MIN_MATCH = 3;
constexpr size_t MAX_MATCH = 258;
constexpr size_t HASH_LOG = 16;
constexpr size_t HASH_SIZE = 1 << HASH_LOG;
constexpr size_t CHAIN_LIMIT = 128;

constexpr uint8_t ML_BITS[53] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,2,2,3,3,4,4,5,7,8,9,10,11,
    12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,31
};

constexpr uint8_t ML_BASE[53] = {
    3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
    19,20,21,22,23,24,25,26,27,28,29,30,31,32,
    33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55
};

constexpr uint8_t OF_BITS[29] = {
    0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,
    4,4,4,4,5,5,5,5,6,6,6,6,7
};

constexpr uint32_t OF_BASE[29] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385
};

auto hash3(const uint8_t* p) -> uint32_t {
    return (p[0] << 16) ^ (p[1] << 8) ^ p[2];
}

struct Match {
    size_t length{0};
    size_t offset{0};
};

class ZstdCompressorImpl {
public:
    explicit ZstdCompressorImpl(int level) : level_(level) {
        window_.resize(WINDOW_SIZE);
        hash_table_.resize(HASH_SIZE, 0);
        chain_.resize(CHAIN_LIMIT);
    }

    auto compress(const std::vector<uint8_t>& input) -> std::vector<uint8_t> {
        if (input.empty()) return {};

        std::vector<uint8_t> output;
        output.reserve(input.size() + input.size() / 100 + 128);

        writeFrameHeader(output, input.size());

        const uint8_t* src = input.data();
        size_t size = input.size();
        size_t pos = 0;

        std::vector<uint8_t> literals;

        while (pos < size) {
            literals.clear();

            size_t block_end = std::min(pos + 128 * 1024, size);

            while (pos < block_end) {
                Match best;
                if (pos + MIN_MATCH <= block_end) {
                    best = findMatch(src, pos, block_end);
                }

                if (best.length >= MIN_MATCH) {
                    if (!literals.empty()) {
                        writeLiteralsBlock(output, literals);
                        literals.clear();
                    }
                    writeMatch(output, best);
                    for (size_t i = 0; i < best.length; ++i) {
                        updateHash(src, pos + i, size);
                    }
                    pos += best.length;
                } else {
                    literals.push_back(src[pos]);
                    updateHash(src, pos, size);
                    ++pos;
                }
            }

            if (!literals.empty()) {
                writeLiteralsBlock(output, literals);
            }
        }

        return output;
    }

private:
    int level_;
    std::vector<uint8_t> window_;
    std::vector<uint32_t> hash_table_;
    std::vector<uint32_t> chain_;

    auto findMatch(const uint8_t* src, size_t pos, size_t end) -> Match {
        if (pos + MIN_MATCH > end) return {};

        uint32_t h = hash3(src + pos) & (HASH_SIZE - 1);
        uint32_t candidate = hash_table_[h];

        Match best;
        size_t chain_count = 0;
        size_t max_chain = (level_ > 10) ? CHAIN_LIMIT : CHAIN_LIMIT / 2;

        while (candidate != 0 && chain_count < max_chain) {
            size_t offset = pos - candidate;
            if (offset > WINDOW_SIZE || offset == 0) break;

            size_t len = 0;
            size_t max_len = std::min(end - pos, MAX_MATCH);
            while (len < max_len && src[candidate + len] == src[pos + len]) {
                ++len;
            }

            if (len >= MIN_MATCH && len > best.length) {
                best.length = len;
                best.offset = offset;
                if (len >= 128) break;
            }

            candidate = chain_[chain_count % CHAIN_LIMIT];
            ++chain_count;
        }

        return best;
    }

    auto updateHash(const uint8_t* src, size_t pos, size_t size) -> void {
        if (pos + MIN_MATCH > size) return;
        uint32_t h = hash3(src + pos) & (HASH_SIZE - 1);
        chain_[pos % CHAIN_LIMIT] = hash_table_[h];
        hash_table_[h] = static_cast<uint32_t>(pos);
    }

    auto writeFrameHeader(std::vector<uint8_t>& out, size_t orig_size) -> void {
        out.push_back(0x28);
        out.push_back(0xB5);
        out.push_back(0x2F);
        out.push_back(0xFD);

        out.push_back(0x22 | 0x04);

        uint8_t fhd = (WINDOW_LOG - 10) << 3;
        out.push_back(fhd);

        out.push_back(orig_size & 0xFF);
        out.push_back((orig_size >> 8) & 0xFF);
        out.push_back((orig_size >> 16) & 0xFF);
        out.push_back((orig_size >> 24) & 0xFF);
    }

    static auto writeLiteralsBlock(std::vector<uint8_t>& out,
                                    const std::vector<uint8_t>& lits) -> void {
        size_t size = lits.size();

        if (size < 32) {
            out.push_back(static_cast<uint8_t>(size));
        } else if (size < 4096) {
            out.push_back(static_cast<uint8_t>((size >> 4) | 0x40));
            out.push_back(static_cast<uint8_t>((size << 4) & 0xFF));
        } else {
            out.push_back(0x80);
            out.push_back(size & 0xFF);
            out.push_back((size >> 8) & 0xFF);
            out.push_back((size >> 16) & 0xFF);
        }

        out.insert(out.end(), lits.begin(), lits.end());
    }

    static auto writeMatch(std::vector<uint8_t>& out, const Match& m) -> void {
        size_t ml_code = 0;
        while (ml_code < 52 && ML_BASE[ml_code + 1] <= m.length) ++ml_code;

        size_t of_code = 0;
        while (of_code < 28 && OF_BASE[of_code + 1] <= m.offset) ++of_code;

        uint8_t token = static_cast<uint8_t>((ml_code << 3) | of_code);
        out.push_back(token);

        if (ml_code >= 52) {
            size_t extra = m.length - ML_BASE[ml_code];
            while (extra >= 255) {
                out.push_back(255);
                extra -= 255;
            }
            out.push_back(static_cast<uint8_t>(extra));
        }

        if (of_code >= 28) {
            size_t extra = m.offset - OF_BASE[of_code];
            while (extra >= 255) {
                out.push_back(255);
                extra -= 255;
            }
            out.push_back(static_cast<uint8_t>(extra));
        }
    }
};

class ZstdDecompressorImpl {
public:
    auto decompress(const std::vector<uint8_t>& input) -> std::vector<uint8_t> {
        if (input.size() < 6) return {};

        size_t pos = 0;
        if (input[0] != 0x28 || input[1] != 0xB5 ||
            input[2] != 0x2F || input[3] != 0xFD) {
            return {};
        }
        pos = 4;

        uint8_t frame_header_desc = input[pos++];
        uint8_t fhd = input[pos++];
        (void)frame_header_desc;
        (void)fhd;

        uint32_t content_size = input[pos] | (input[pos+1] << 8) |
                               (input[pos+2] << 16) | (input[pos+3] << 24);
        pos += 4;

        std::vector<uint8_t> output;
        output.reserve(content_size);

        while (pos < input.size() && output.size() < content_size) {
            if (pos >= input.size()) break;

            uint8_t block_header = input[pos++];

            if ((block_header & 0x80) == 0) {
                size_t lit_size;
                if ((block_header & 0xC0) == 0x40) {
                    lit_size = ((block_header & 0x3F) << 4) | (input[pos] >> 4);
                    ++pos;
                } else if ((block_header & 0xC0) == 0x80) {
                    lit_size = input[pos] | (input[pos+1] << 8) | (input[pos+2] << 16);
                    pos += 3;
                } else {
                    lit_size = block_header;
                }

                if (pos + lit_size > input.size()) break;
                output.insert(output.end(), input.begin() + pos,
                              input.begin() + pos + lit_size);
                pos += lit_size;
            } else {
                uint8_t token = block_header;
                size_t ml_code = (token >> 3) & 0x1F;
                size_t of_code = token & 0x07;

                size_t match_len = ML_BASE[ml_code];
                if (ml_code >= 52) {
                    while (pos < input.size() && input[pos] == 255) {
                        match_len += 255;
                        ++pos;
                    }
                    if (pos < input.size()) {
                        match_len += input[pos++];
                    }
                }

                size_t offset = OF_BASE[of_code];
                if (of_code >= 28) {
                    while (pos < input.size() && input[pos] == 255) {
                        offset += 255;
                        ++pos;
                    }
                    if (pos < input.size()) {
                        offset += input[pos++];
                    }
                }

                if (offset > output.size() || offset == 0) break;

                size_t copy_pos = output.size() - offset;
                for (size_t i = 0; i < match_len; ++i) {
                    if (copy_pos + i < output.size()) {
                        output.push_back(output[copy_pos + i]);
                    } else {
                        output.push_back(output[copy_pos + (i % offset)]);
                    }
                }
            }
        }

        return output;
    }
};

}  // namespace

auto zstd_compress(const std::vector<uint8_t>& data, int compression_level)
    -> std::vector<uint8_t> {
    ZstdCompressorImpl impl(compression_level);
    return impl.compress(data);
}

auto zstd_decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
    ZstdDecompressorImpl impl;
    return impl.decompress(data);
}

}  // namespace compressor::algorithm