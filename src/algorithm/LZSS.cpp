#include "LZSS.hpp"

#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

// ============================================================================
// LZSS_Core — 纯函数贪婪匹配
// ============================================================================

LZSSCoreResult LZSS::lzss_core(const VirtualBuffer<3>& input,
                                size_t search_size,
                                size_t min_match_length,
                                size_t max_match_length) {
    LZSSCoreResult result;
    const size_t in_len = input.size();
    if (in_len == 0) return result;

    size_t cursor = 0;
    while (cursor < in_len) {
        size_t match_offset = 0;
        size_t match_length = 0;

        size_t search_start = (cursor > search_size) ? cursor - search_size : 0;

        for (size_t i = search_start; i < cursor; ++i) {
            size_t cur_len = 0;
            while (cur_len < max_match_length &&
                   cursor + cur_len < in_len &&
                   input[i + cur_len] == input[cursor + cur_len]) {
                cur_len++;
            }
            if (cur_len > match_length) {
                match_length = cur_len;
                match_offset = cursor - i;
            }
        }

        if (match_length < min_match_length) {
            result.triples.push_back({0, 0, input[cursor]});
            cursor++;
        } else {
            result.triples.push_back({match_offset, match_length, 0});
            cursor += match_length;
        }
    }

    return result;
}

// ============================================================================
// encode_triples — Triple 列表 → 字节流
// ============================================================================

std::vector<uint8_t> LZSS::encode_triples(const std::vector<LZSSTriple>& triples,
                                           size_t search_size,
                                           size_t min_match_length,
                                           bool use_flag_encoding) {
    std::vector<uint8_t> result;

    if (use_flag_encoding) {
        uint8_t flag_byte = 0;
        uint8_t flag_byte_idx = 0;
        std::vector<uint8_t> token_buffer;

        auto flush_token = [&]() {
            result.push_back(flag_byte);
            result.insert(result.end(), token_buffer.begin(), token_buffer.end());
            flag_byte = 0;
            flag_byte_idx = 0;
            token_buffer.clear();
        };

        for (const auto& t : triples) {
            if (t.offset == 0) {
                flag_byte |= (1 << flag_byte_idx);
                token_buffer.push_back(t.literal);
            } else {
                uint16_t token = (t.offset << 4) | (t.length - min_match_length);
                token_buffer.push_back(token >> 8);
                token_buffer.push_back(token & 0xFF);
            }
            flag_byte_idx++;

            if (flag_byte_idx == 8) {
                flush_token();
            }
        }

        if (flag_byte_idx != 0) {
            flush_token();
        }
    } else {
        size_t i = 0;
        while (i < triples.size()) {
            const auto& t = triples[i];
            if (t.offset == 0) {
                size_t run_len = 0;
                std::vector<uint8_t> literals;
                while (i + run_len < triples.size() && run_len < 15 &&
                       triples[i + run_len].offset == 0) {
                    literals.push_back(triples[i + run_len].literal);
                    run_len++;
                }
                uint16_t token = (0 << 4) | (run_len & 0x0F);
                result.push_back(token >> 8);
                result.push_back(token & 0xFF);
                result.insert(result.end(), literals.begin(), literals.end());
                i += run_len;
            } else {
                uint16_t token = (t.offset << 4) | (t.length - min_match_length);
                result.push_back(token >> 8);
                result.push_back(token & 0xFF);
                i++;
            }
        }
    }

    return result;
}

// ============================================================================
// LZSS::compress — 非流式入口（薄包装）
// ============================================================================

std::vector<uint8_t> LZSS::compress(const std::vector<uint8_t>& input,
                                     size_t dictionary_buffer_size,
                                     size_t min_match_length,
                                     bool use_flag_encoding) {
    std::vector<uint8_t> result;

    if (input.empty()) return result;

    // 头 4 字节：原始大小
    uint32_t original_size = static_cast<uint32_t>(input.size());
    result.push_back((original_size >> 24) & 0xFF);
    result.push_back((original_size >> 16) & 0xFF);
    result.push_back((original_size >> 8) & 0xFF);
    result.push_back(original_size & 0xFF);

    // Core 匹配
    std::vector<std::span<const uint8_t>> segs;
    segs.emplace_back(input);
    VirtualBuffer<3> vb(std::move(segs));
    auto core_result = lzss_core(vb, dictionary_buffer_size, min_match_length, MAX_MATCH_LENGTH_);

    // 编码
    auto encoded = encode_triples(core_result.triples, dictionary_buffer_size,
                                   min_match_length, use_flag_encoding);
    result.insert(result.end(), encoded.begin(), encoded.end());

    return result;
}

// ============================================================================
// LZSS::decompress
// ============================================================================

std::vector<uint8_t> LZSS::decompress(const std::vector<uint8_t>& input,
                                       size_t min_match_length,
                                       bool use_flag_encoding) {
    std::vector<uint8_t> result;
    if (input.size() < 4) return result;

    uint32_t original_size = (static_cast<uint32_t>(input[0]) << 24) |
                             (static_cast<uint32_t>(input[1]) << 16) |
                             (static_cast<uint32_t>(input[2]) << 8) |
                             static_cast<uint32_t>(input[3]);

    size_t i = 4;

    if (use_flag_encoding) {
        while (i < input.size() && result.size() < original_size) {
            uint8_t flag_byte = input[i];
            i++;
            for (uint8_t j = 0; j < 8; ++j) {
                if (result.size() >= original_size || i >= input.size()) break;
                if ((flag_byte >> j) & 1) {
                    result.push_back(input[i]);
                    i++;
                } else {
                    if (i + 1 >= input.size()) break;
                    uint16_t token = (static_cast<uint16_t>(input[i]) << 8) | input[i + 1];
                    uint16_t position = token >> 4;
                    uint8_t length = (token & 0x0F) + min_match_length;
                    i += 2;
                    size_t start = result.size() - position;
                    for (uint8_t k = 0; k < length; ++k) {
                        result.push_back(result[start + k]);
                    }
                }
            }
        }
    } else {
        while (i + 1 < input.size() && result.size() < original_size) {
            uint16_t token = (static_cast<uint16_t>(input[i]) << 8) | input[i + 1];
            i += 2;
            uint16_t position = token >> 4;
            uint8_t length_val = token & 0x0F;

            if (position == 0) {
                for (uint8_t k = 0; k < length_val; ++k) {
                    if (i >= input.size()) break;
                    result.push_back(input[i]);
                    i++;
                }
            } else {
                uint8_t length = length_val + min_match_length;
                size_t start = result.size() - position;
                for (uint8_t k = 0; k < length; ++k) {
                    result.push_back(result[start + k]);
                }
            }
        }
    }

    return result;
}

// ============================================================================
// LZSS_OutOfCore — 流式压缩状态机
// ============================================================================

LZSS_OutOfCore::LZSS_OutOfCore(size_t search_size, size_t min_match_length,
                                 bool use_flag_encoding)
    : search_size_(search_size),
      min_match_length_(min_match_length),
      use_flag_encoding_(use_flag_encoding) {
    reset();
}

auto LZSS_OutOfCore::reset(void) -> void {
    state_ = State::COLLECT_INPUT;
    input_buffer_.clear();
    total_in_len_ = 0;
    triples_.clear();
    emitted_tokens_ = 0;
    header_emitted_ = false;
}

auto LZSS_OutOfCore::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        switch (state_) {
            case State::COLLECT_INPUT:
                handleCollectInput(status, is_last_chunk);
                break;
            case State::EMIT_TOKENS:
                handleEmitTokens(status, is_last_chunk);
                break;
            case State::DONE:
                status.done = true;
                return;
        }
        if (status.need_input || status.need_output || status.done) {
            return;
        }
    }
}

auto LZSS_OutOfCore::handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void {
    // 1. 读入数据
    size_t remain = reader_.getRemainSize();
    if (remain > 0) {
        size_t old_len = input_buffer_.size();
        input_buffer_.resize(old_len + remain);
        size_t copied = reader_.readBytes(input_buffer_.data() + old_len, remain);
        if (copied < remain) {
            input_buffer_.resize(old_len + copied);
        }
    }

    // 2. 只有末块才触发 core 匹配
    if (!is_last_chunk) {
        status.need_input = true;
        return;
    }

    total_in_len_ = input_buffer_.size();

    // 3. 调用 lzss_core
    std::vector<std::span<const uint8_t>> segs;
    segs.emplace_back(input_buffer_);
    VirtualBuffer<3> vb(std::move(segs));
    auto core_result = LZSS::lzss_core(vb, search_size_, min_match_length_,
                                        LZSS::MAX_MATCH_LENGTH_);
    triples_ = std::move(core_result.triples);
    emitted_tokens_ = 0;

    state_ = State::EMIT_TOKENS;
}

auto LZSS_OutOfCore::handleEmitTokens(AlgorithmStatus& status, bool is_last_chunk) -> void {
    // 1. 发头 4 字节
    if (!header_emitted_) {
        if (!writer_.ensureSpace(32)) {
            status.need_output = true;
            return;
        }
        uint32_t original_size = static_cast<uint32_t>(total_in_len_);
        uint8_t hdr[4] = {
            static_cast<uint8_t>((original_size >> 24) & 0xFF),
            static_cast<uint8_t>((original_size >> 16) & 0xFF),
            static_cast<uint8_t>((original_size >> 8) & 0xFF),
            static_cast<uint8_t>(original_size & 0xFF)
        };
        writer_.writeBytes(hdr, 4);
        header_emitted_ = true;
    }

    // 2. 编码 Triple 并输出
    if (use_flag_encoding_) {
        while (emitted_tokens_ < triples_.size()) {
            if (!writer_.ensureSpace(24)) {
                status.need_output = true;
                return;
            }

            // 每 8 个 token 一组，先写 flag byte
            uint8_t flag_byte = 0;
            uint8_t token_buf[16];
            size_t token_bytes = 0;
            size_t group_end = emitted_tokens_ + 8;
            if (group_end > triples_.size()) group_end = triples_.size();

            for (size_t j = emitted_tokens_; j < group_end; ++j) {
                const auto& t = triples_[j];
                if (t.offset == 0) {
                    flag_byte |= (1 << (j - emitted_tokens_));
                    token_buf[token_bytes++] = t.literal;
                } else {
                    uint16_t token = (t.offset << 4) | (t.length - min_match_length_);
                    token_buf[token_bytes++] = static_cast<uint8_t>(token >> 8);
                    token_buf[token_bytes++] = static_cast<uint8_t>(token & 0xFF);
                }
            }

            writer_.writeBytes(&flag_byte, 1);
            writer_.writeBytes(token_buf, token_bytes);
            emitted_tokens_ = group_end;
        }
    } else {
        while (emitted_tokens_ < triples_.size()) {
            if (!writer_.ensureSpace(24)) {
                status.need_output = true;
                return;
            }

            const auto& t = triples_[emitted_tokens_];
            if (t.offset == 0) {
                // 收集连续字面量
                size_t run_len = 0;
                uint8_t lit_buf[15];
                while (emitted_tokens_ + run_len < triples_.size() && run_len < 15 &&
                       triples_[emitted_tokens_ + run_len].offset == 0) {
                    lit_buf[run_len] = triples_[emitted_tokens_ + run_len].literal;
                    run_len++;
                }
                uint16_t token = (0 << 4) | (run_len & 0x0F);
                uint8_t h = static_cast<uint8_t>(token >> 8);
                uint8_t l = static_cast<uint8_t>(token & 0xFF);
                writer_.writeBytes(&h, 1);
                writer_.writeBytes(&l, 1);
                writer_.writeBytes(lit_buf, run_len);
                emitted_tokens_ += run_len;
            } else {
                uint16_t token = (t.offset << 4) | (t.length - min_match_length_);
                uint8_t h = static_cast<uint8_t>(token >> 8);
                uint8_t l = static_cast<uint8_t>(token & 0xFF);
                writer_.writeBytes(&h, 1);
                writer_.writeBytes(&l, 1);
                emitted_tokens_++;
            }
        }
    }

    writer_.flush();
    state_ = State::DONE;
    status.done = true;
}

}  // namespace algorithm
}  // namespace compressor