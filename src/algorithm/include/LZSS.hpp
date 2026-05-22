#pragma once

#include <cstdint>
#include <vector>

#include "IAlgorithm.hpp"
#include "VirtualBuffer.hpp"

namespace compressor {
namespace algorithm {

/**
 * @brief LZSS Core — 纯函数贪婪匹配，服务于流式和非流式
 *
 * 与 LZDP::dp_core 对等：输入 VirtualBuffer，输出 LZSSCoreResult（Triple 列表）。
 * 编码/解码分离到 encode_triples / decompress，core 只做匹配。
 */
struct LZSSTriple {
    size_t offset;     // 0 = literal
    size_t length;     // 0 for literal, match length otherwise
    uint8_t literal;   // literal byte (valid when offset == 0)
};

struct LZSSCoreResult {
    std::vector<LZSSTriple> triples;
};

/**
 * @brief Optimized LZ77. Rather than output the tuple (position, length, char),
 *        output one of the following two formats (0, position, length) or (1, char)
 */
class LZSS {
   public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input,
                                         size_t dictionary_buffer_size = 4095,
                                         size_t min_match_length = 3,
                                         bool use_flag_encoding = true);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input,
                                           size_t min_match_length = 3,
                                           bool use_flag_encoding = true);

    /// 纯函数贪婪匹配核心 — 输入 VirtualBuffer，输出 Triple 列表
    static LZSSCoreResult lzss_core(const VirtualBuffer<3>& input,
                                    size_t search_size,
                                    size_t min_match_length,
                                    size_t max_match_length);

    /// 将 Triple 列表编码为字节流（flag 或 non-flag 模式）
    static std::vector<uint8_t> encode_triples(const std::vector<LZSSTriple>& triples,
                                                size_t search_size,
                                                size_t min_match_length,
                                                bool use_flag_encoding);

    static constexpr uint16_t DICTIONARY_BUFFER_SIZE_ = 4095;  // 12 bits
    static constexpr uint8_t MIN_MATCH_LENGTH_ = 3;            // 4 bits
    static constexpr uint8_t MAX_MATCH_LENGTH_ = 18;  // MIN_MATCH_LENGTH + 15

   private:
};

/**
 * @brief LZSS 流式压缩 — 状态机驱动，对齐 streaming-compression-design.md
 *
 * 状态机构造：
 *   COLLECT_INPUT → EMIT_TOKENS → DONE
 *
 * COLLECT_INPUT 阶段累积输入，调用 lzss_core() 完成贪婪匹配；
 * EMIT_TOKENS 阶段将 Triple 编码为字节流输出。
 */
class LZSS_OutOfCore : public AlgorithmBase {
public:
    LZSS_OutOfCore(size_t search_size = 4095, size_t min_match_length = 3,
                   bool use_flag_encoding = true);

    auto reset(void) -> void override;

protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

private:
    enum class State { COLLECT_INPUT, EMIT_TOKENS, DONE };
    State state_{State::COLLECT_INPUT};

    size_t search_size_;
    size_t min_match_length_;
    bool use_flag_encoding_;

    // 输入累积
    std::vector<uint8_t> input_buffer_;
    size_t total_in_len_{0};

    // Core 输出
    std::vector<LZSSTriple> triples_;
    size_t emitted_tokens_{0};

    // 头 4 字节（原始大小）已发出？
    bool header_emitted_{false};

    auto handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleEmitTokens(AlgorithmStatus& status, bool is_last_chunk) -> void;

    using StateHandler = void (LZSS_OutOfCore::*)(AlgorithmStatus&, bool);
    static constexpr StateHandler kStateHandlers[3] = {
        &LZSS_OutOfCore::handleCollectInput,
        &LZSS_OutOfCore::handleEmitTokens,
        nullptr  // DONE
    };
};

}  // namespace algorithm
}  // namespace compressor