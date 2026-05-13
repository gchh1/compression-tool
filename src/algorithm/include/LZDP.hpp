#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IAlgorithm.hpp"
#include "KMPMatcher.hpp"
#include "SpillBitStream.hpp"
#include "TempFile.hpp"
#include "VirtualBuffer.hpp"

namespace compressor {
namespace algorithm {

class LZDP {
public:
    size_t get_min_match() const {
        if (min_match_ == 0) {
            return get_match_bits() / 8 + 1;
        }
        return min_match_;
    }

    struct Triple {
        size_t offset;
        size_t length;
        uint8_t literal;
        Triple(size_t a, size_t b, uint8_t c)
            : offset(a), length(b), literal(c) {}
        Triple() : offset(0), length(0), literal(0) {}
    };

    struct DPCandidate {
        size_t offset;
        size_t length;
        uint8_t literal;
        bool is_chosen;
    };

    struct DpCoreResult {
        std::vector<Triple> triples;
    };

    /// Per-byte visualization / array slot with explicit index and
    /// reachability (choice == Triple(offset,length,literal)).
    struct DPState {
        size_t position;
        bool reachable;
        size_t token_count;
        size_t literal_count;
        size_t match_count;
        size_t predecessor;
        Triple choice;
    };

    struct DPStep {
        size_t position;
        std::vector<DPCandidate> candidates;
        size_t best_token_count;
    };

    struct DPVisualization {
        std::vector<DPStep> steps;
        std::vector<DPState> dp_array;
        std::vector<Triple> optimal_path;
        size_t input_length;
        size_t search_size;
        size_t lookahead_size;
    };

private:
    size_t offset_bits_;
    size_t length_bits_;
    size_t max_search_size_;
    size_t max_look_size_;
    bool use_flag_encoding_;
    int match_engine_{0}; // 0 = KMP, 1 = HashChain
    size_t min_match_{0};

    static size_t calcBitWidth(size_t max_val);

public:
    LZDP(size_t offset_bits = 0, size_t length_bits = 0)
        : offset_bits_(offset_bits),
          length_bits_(length_bits),
          use_flag_encoding_(false),
          match_engine_(0) {
        if (offset_bits_ == 0) offset_bits_ = 12;
        if (length_bits_ == 0) length_bits_ = 8;
        max_search_size_ = (size_t{1} << offset_bits_) - 1;
        max_look_size_ = (size_t{1} << length_bits_) - 1;
    }

    void autoBitWidth(size_t search_size, size_t lookahead_size) {
        offset_bits_ = calcBitWidth(search_size);
        length_bits_ = calcBitWidth(lookahead_size);
        max_search_size_ = (size_t{1} << offset_bits_) - 1;
        max_look_size_ = (size_t{1} << length_bits_) - 1;
    }

    size_t get_offset_bits() const { return offset_bits_; }
    size_t get_length_bits() const { return length_bits_; }

    /// non-flag 匹配编码总位宽 = Ob + Lb
    /// 用于 match_cost、剩余位检查、min_match 启发式等场景
    size_t get_match_bits() const { return offset_bits_ + length_bits_; }

    void set_use_flag_encoding(bool use) { use_flag_encoding_ = use; }
    bool get_use_flag_encoding() const { return use_flag_encoding_; }

    void set_match_engine(int v) { match_engine_ = v; }
    int get_match_engine() const { return match_engine_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match_param() const { return min_match_; }

    DpCoreResult dp_core(
        const std::vector<uint8_t>& input,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3,
        size_t core_begin = 0,
        size_t core_end = SIZE_MAX);

    /// VirtualBuffer 重载 — 零拷贝"假"拼接，用于流式分块场景
    DpCoreResult dp_core(
        const VirtualBuffer& vb,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3,
        size_t core_begin = 0,
        size_t core_end = SIZE_MAX);

    std::vector<uint8_t> encode_triples(
        const std::vector<Triple>& triples,
        size_t offset_bits,
        size_t length_bits,
        bool use_flag_encoding) const;

    DPVisualization get_dp_visualization(
        const std::vector<uint8_t>& input,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3);

    std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);
};

class LZDP_OutOfCore : public AlgorithmBase {
    /**
     * ``lzdp`` = LZDP；``ooc`` = Out-of-core（见 ``docs/缩写对照表.md``）。
     * COLLECT_INPUT 单步：前向 bit-cost DP + packed link 写 temp A；对齐 ``streaming-compression-design.md`` §3.1 / §5.4。
     */
    friend void lzdp_ooc_collect_one_index(LZDP_OutOfCore& self, size_t pos_idx,
                                           uint32_t abs_pos);

public:
    LZDP_OutOfCore(size_t search_size = 4096, size_t lookahead_size = 256,
                   size_t min_match = 0, size_t dp_top = 3,
                   bool use_flag_encoding = false, int match_engine = 0);

    /// non-flag 匹配编码总位宽 = Ob + Lb
    size_t get_match_bits() const { return offset_bits_ + length_bits_; }

    auto reset(void) -> void override;

protected:
    auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk) -> void override;

private:
    enum class State { COLLECT_INPUT, BACKTRACK, EMIT_TOKENS };
    State state_{State::COLLECT_INPUT};

    size_t SEARCH_SIZE;
    size_t LOOKAHEAD_SIZE;
    /// Ring size for forward DP columns (must exceed max match length to avoid slot aliasing).
    size_t dp_slot_count_{0};
    size_t MIN_MATCH;
    size_t DP_TOP;

    bool use_flag_encoding_{false};
    int match_engine_{0}; // 0 = KMP, 1 = HashChain

    std::vector<uint8_t> input_buffer_;
    
    struct DpState {
        uint32_t cost{UINT32_MAX};
        uint16_t length{0};
        uint16_t offset{0};
    };
    std::vector<DpState> dp_states_;
    std::vector<uint32_t> head_;
    /// Per buffer index — same semantics as ``compress_dp``'s ``prev[pos]`` (not ``abs_pos % SEARCH_SIZE``).
    std::vector<uint32_t> prev_buf_;
    
    uint64_t window_abs_pos_{0};
    size_t current_i_{0};
    uint32_t total_in_len_{0};
    
    TempFile temp_file_A_;
    TempFile temp_file_B_;
    TempFileBitAppender spill_a_{&temp_file_A_};
    TempFileBitAppender spill_b_{&temp_file_B_};
    PackedDpLinkSpec spill_spec_{};
    
    uint64_t total_tokens_{0};
    uint64_t emitted_tokens_{0};
    /// Emit the same 2-byte prefix as ``LZDP::compress_dp`` / ``decompress`` (not bit-packed only).
    bool emit_lzdp_raw_header_done_{false};

    uint32_t lit_cost_{1};
    uint32_t match_cost_{1};
    size_t offset_bits_{12};
    size_t length_bits_{8};

    auto hashBucket3(size_t pos_idx) const -> size_t;
    auto reseedHashChainPrefix(size_t end_exclusive) -> void;

    auto handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleBacktrack(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleEmitTokens(AlgorithmStatus& status, bool is_last_chunk) -> void;

    using StateHandler = void (LZDP_OutOfCore::*)(AlgorithmStatus&, bool);
    static constexpr StateHandler kStateHandlers[3] = {
        &LZDP_OutOfCore::handleCollectInput, &LZDP_OutOfCore::handleBacktrack,
        &LZDP_OutOfCore::handleEmitTokens};
};

class LZDPDecompress_OutOfCore : public AlgorithmBase {
public:
    LZDPDecompress_OutOfCore(bool use_flag_encoding = false);

    /// non-flag 匹配编码总位宽 = Ob + Lb
    size_t get_match_bits() const { return offset_bits_ + length_bits_; }

    auto reset(void) -> void override;

protected:
    auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk) -> void override;

private:
    bool use_flag_encoding_{false};
    bool read_header_{false};
    /// Accumulate the two raw header bytes across chunked input (streaming decompress).
    uint8_t lzdp_hdr_acc_[2]{};
    uint8_t lzdp_hdr_acc_len_{0};
    size_t offset_bits_{0};
    size_t length_bits_{0};
    std::vector<uint8_t> output_buffer_;
    size_t output_flush_idx_{0};
};

} // namespace algorithm
} // namespace compressor