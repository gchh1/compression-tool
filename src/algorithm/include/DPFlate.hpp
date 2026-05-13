#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <cstdio>

#include "Deflate.hpp"
#include "HuffmanTree.hpp"
#include "HuffmanTree3HM.hpp"
#include "IAlgorithm.hpp"
#include "Inflate.hpp"
#include "SpillBitStream.hpp"
#include "TempFile.hpp"

namespace compressor::algorithm {

/**
 * DPFlate：DP（Dynamic Programming）匹配决策 + FLATE 风格或 3HfMTree 熵编码。
 * 流式阶段划分见 ``docs/design/streaming-compression-design.md``（COLLECT_INPUT / BACKTRACK / …）。
 */
class DPFlate : public AlgorithmBase {
public:
    DPFlate(size_t search_size = 32768, size_t lookahead_size = 258,
            size_t min_match = 3, size_t dp_top = 4,
            size_t dp_sub_match_max = 6);

    ~DPFlate() override = default;

    auto reset(void) -> void override;

    void set_match_engine(int v) { match_engine_ = v; }
    int get_match_engine() const { return match_engine_; }
    void set_use_flag_encoding(bool v) { use_flag_encoding_ = v; }
    bool get_use_flag_encoding() const { return use_flag_encoding_; }
    void set_prefer_disk_dp_tables(bool v) { prefer_disk_dp_tables_ = v; }
    bool get_prefer_disk_dp_tables() const { return prefer_disk_dp_tables_; }
    void set_use_3hfmtree(bool v) { use_3hfmtree_ = v; }
    bool get_use_3hfmtree() const { return use_3hfmtree_; }
    /** Sets both offset and length Huffman slot widths (bits per chunk). */
    void set_huffman_chunk_bits(size_t k) {
        huffman_offset_chunk_bits_ = k;
        huffman_length_chunk_bits_ = k;
    }
    size_t get_huffman_chunk_bits() const { return huffman_offset_chunk_bits_; }
    void set_huffman_offset_chunk_bits(size_t k) { huffman_offset_chunk_bits_ = k; }
    void set_huffman_length_chunk_bits(size_t k) { huffman_length_chunk_bits_ = k; }
    size_t get_huffman_offset_chunk_bits() const { return huffman_offset_chunk_bits_; }
    size_t get_huffman_length_chunk_bits() const { return huffman_length_chunk_bits_; }

    /** @brief Number of bits needed to represent the maximum offset (SEARCH_SIZE) */
    size_t offset_bits_3hm() const {
        size_t v = SEARCH_SIZE;
        if (v == 0) return 1;
        v--;
        size_t bits = 0;
        while (v > 0) { bits++; v >>= 1; }
        return bits == 0 ? 1 : bits;
    }

    /** @brief Number of bits needed to represent the maximum length (LOOKAHEAD_SIZE) */
    size_t length_bits_3hm() const {
        size_t v = LOOKAHEAD_SIZE;
        if (v == 0) return 1;
        v--;
        size_t bits = 0;
        while (v > 0) { bits++; v >>= 1; }
        return bits == 0 ? 1 : bits;
    }

protected:
    auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk)
        -> void override;

private:
    /**
     * COLLECT_INPUT：单字节 push-DP 步进 + 向 temp A 写 packed link（与 ``LZDP_OutOfCore`` 同构的 Phase1 形态）。
     * 见 ``docs/design/streaming-compression-design.md`` §5.4 / §3.1；缩写见 ``docs/缩写对照表.md``。
     */
    friend void dpflate_collect_input_one_index(DPFlate& self, size_t pos_idx, uint32_t abs_pos);

    enum class DPFlateState { COLLECT_INPUT, BACKTRACK, BUILD_TREE, EMIT_TOKENS };

    DPFlateState state_{DPFlateState::COLLECT_INPUT};

    auto hashBucket3(size_t pos_idx) const -> size_t;
    auto reseedHashChainPrefix(size_t end_exclusive) -> void;

    auto handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleBacktrack(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleEmitTokens(AlgorithmStatus& status, bool is_last_chunk) -> void;

    /// Huffman / 熵层：重置频率表与树对象（FLATE 与 3HfM 分支均在 ``DPFlate.cpp``）。
    void huff_reset_entropy_tables();
    /// BACKTRACK：逆向读 temp A 上一条 token 后，累计 FLATE 与（若启用）3HfM 频率。
    void huff_backtrack_accumulate_token(uint16_t length, uint16_t offset, size_t& literal_run_len_3hm);
    /// BACKTRACK 末尾：刷出 3HfM 字面量游程在频率表中的收尾（FLATE 路径为空操作）。
    void huff_backtrack_finalize_literals(size_t& literal_run_len_3hm);
    /// BUILD_TREE：建树并序列化到 ``writer_``。
    bool huff_build_tree_and_write_trees(AlgorithmStatus& st);
    /// EMIT_TOKENS：按 temp B 逆向 token 序发码至 ``writer_``。
    bool huff_emit_token_stream(AlgorithmStatus& st);

    using StateHandler = void (DPFlate::*)(AlgorithmStatus&, bool);
    static constexpr StateHandler kStateHandlers[4] = {
        &DPFlate::handleCollectInput, &DPFlate::handleBacktrack,
        &DPFlate::handleBuildTree, &DPFlate::handleEmitTokens};

    size_t SEARCH_SIZE;       ///< 搜索窗口（字节），缩写含义见 ``docs/缩写对照表.md``
    size_t LOOKAHEAD_SIZE;    ///< 前瞻窗口（字节）
    size_t MIN_MATCH;         ///< 最短匹配长度
    size_t DP_TOP;            ///< DP 每步保留匹配候选数上界（Top-K）
    size_t DP_SUB_MATCH_MAX;  ///< 子匹配链枚举深度相关上界

    int match_engine_{1};     ///< 0=KMP，1=HashChain
    bool use_flag_encoding_{false}; ///< 1-bit flag 区分字面量/匹配等打包方案
    bool prefer_disk_dp_tables_{false};
    bool use_3hfmtree_{false}; ///< true=3HfMTree 三树；false=FLATE 两树
    size_t huffman_offset_chunk_bits_{8};  ///< 3HfM：offset 槽宽（bit）
    size_t huffman_length_chunk_bits_{8}; ///< 3HfM：length 槽宽（bit）

    std::vector<uint8_t> input_buffer_;
    
    struct DpState {
        uint32_t cost{UINT32_MAX};
        uint16_t length{0};
        uint16_t offset{0};
    };
    std::vector<DpState> dp_states_; ///< 环形 DP 列（大小 ``dp_slot_count_``）
    size_t dp_slot_count_{0};        ///< DP 槽数量（覆盖最大回溯长度）
    std::vector<uint32_t> head_;     ///< HashChain 桶头
    std::vector<uint32_t> prev_buf_; ///< HashChain 前向链
    
    uint64_t window_abs_pos_{0}; ///< 滑动窗口绝对起点
    size_t current_i_{0};        ///< 当前在 ``input_buffer_`` 内的处理下标
    uint32_t total_in_len_{0};   ///< 输入总长度（字节，末块确定）
    
    TempFile temp_file_A_; ///< temp A：前向 DP packed link（OOC）
    TempFile temp_file_B_; ///< temp B：回溯后 token 流（OOC）
    TempFileBitAppender spill_a_{&temp_file_A_};
    TempFileBitAppender spill_b_{&temp_file_B_};
    PackedDpLinkSpec spill_spec_{}; ///< packed DP link 位布局规格
    
    uint64_t total_tokens_{0};
    uint64_t emitted_tokens_{0};
    
    std::vector<uint32_t> freq_map_; ///< FLATE 主树频率（286 符号空间）
    std::vector<uint32_t> dist_freq_; ///< FLATE 距离树频率（30 符号空间）

    uint32_t lit_cost_{1};   ///< 字面量转移比特代价（随 flag 编码变）
    uint32_t match_cost_{1}; ///< 匹配转移比特代价

    std::unique_ptr<HuffmanTree> huffman_tree_; ///< FLATE：字面量/长度树
    std::unique_ptr<HuffmanTree> dist_tree_;    ///< FLATE：距离树
    std::vector<HuffmanCode> dictionary_;     ///< FLATE：主码表
    std::vector<HuffmanCode> dist_dictionary_;///< FLATE：距离码表

    std::unique_ptr<HuffmanTree3HM> huffman_tree_3hm_; ///< 3HfM：三树打包对象
    std::vector<uint32_t> literal_freq_3hm_;  ///< 3HfM：字面量频率
    std::vector<uint32_t> offset_freq_3hm_;   ///< 3HfM：offset 槽频率
    std::vector<uint32_t> length_freq_3hm_;   ///< 3HfM：length 槽频率
    size_t offset_count_3hm_{0}; ///< 3HfM offset 字母表大小上界
    size_t length_count_3hm_{0}; ///< 3HfM length 字母表大小上界

    static constexpr size_t DEFLATE_ALPHABET_SIZE = 286;   ///< FLATE 主字母表大小（含 EOB）
    static constexpr size_t DISTANCE_DICTIONARY_SIZE = 30; ///< FLATE 距离字母表大小
    static constexpr size_t DEFLATE_SYMBOL_BITS = 9;
    static constexpr size_t DISTANCE_SYMBOL_BITS = 5;

    void getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits, uint16_t& extra_val);
    void getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits, uint16_t& extra_val);
};

using DPFlateDecompress = Inflate;

}  // namespace compressor::algorithm