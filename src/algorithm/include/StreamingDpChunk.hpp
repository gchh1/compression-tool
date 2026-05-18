#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "DPFlateBin64kDebug.hpp"
#include "LZDPStreamDebug.hpp"

namespace compressor::algorithm {

/// 流式 COLLECT 单格 DP 状态（与 ``LZDP_Streaming::DpState`` 同构）。
struct StreamingDpCell {
    uint32_t cost{UINT32_MAX};
    uint16_t length{0};
    uint16_t offset{0};
    /// Path to this absolute position (``LZDP::compress_dp`` ROL node); used by ``LZDP_Streaming`` only.
    uint32_t literal_count{0};
    uint32_t match_count{0};
};

/**
 * 输入分块 + DP 分块双缓冲：
 * - ``cur``：当前正在提交 link 的绝对位置区间
 * - ``next``：由 match/literal 松弛写入的未来位置（尚未提交，不可丢弃）
 *
 * 仅当 ``cur`` 区间全部写入 temp A / link 镜像后，调用 ``rotate`` 释放 ``cur``，
 * 并把 ``next`` 中仍有效的状态提升为新的 ``cur`` 起点。
 */
class StreamingDpTwoChunk {
public:
    void reset(size_t lookahead) {
        lookahead_ = lookahead;
        cur_base_ = 0;
        next_base_ = 0;
        cur_.clear();
        next_.clear();
        next_.resize(1);
        next_[0].cost = 0;
        next_[0].literal_count = 0;
        next_[0].match_count = 0;
    }

    StreamingDpCell& cell_at(uint32_t abs_pos) {
        if (!next_.empty() && abs_pos >= next_base_ &&
            abs_pos < next_base_ + static_cast<uint32_t>(next_.size())) {
            return next_[static_cast<size_t>(abs_pos - next_base_)];
        }
        if (abs_pos >= cur_base_ &&
            abs_pos < cur_base_ + static_cast<uint32_t>(cur_.size())) {
            return cur_[static_cast<size_t>(abs_pos - cur_base_)];
        }
        if (next_.empty() || abs_pos < next_base_) {
            next_base_ = abs_pos;
            next_.clear();
        }
        const size_t need = static_cast<size_t>(abs_pos - next_base_) + 1;
        if (next_.size() < need) {
            const size_t old = next_.size();
            next_.resize(need);
            // 仅在大步扩容时记录，避免 64k DP 逐格 resize 刷爆日志
            if (need > old && (old == 0 || need - old >= 4096 || (need & (need - 1)) == 0)) {
                dpflate_bin64k_log_streaming_dp("next_resize", abs_pos, cur_.size(), next_.size(),
                                               cur_base_, next_base_);
            }
        }
        return next_[static_cast<size_t>(abs_pos - next_base_)];
    }

    /// 本批 COLLECT 已把 ``[old next_base, commit_until_abs)`` 写入 temp A；丢弃该前缀，保留
    /// ``commit_until_abs`` 及之后的 forward 状态（不 ``assign`` 清空未提交格）。
    void rotate(uint32_t commit_until_abs) {
        cur_base_ = commit_until_abs;
        cur_.clear();

        if (commit_until_abs > next_base_) {
            const size_t drop = static_cast<size_t>(commit_until_abs - next_base_);
            if (drop >= next_.size()) {
                next_.clear();
            } else {
                next_.erase(next_.begin(),
                            next_.begin() + static_cast<std::ptrdiff_t>(drop));
            }
            next_base_ = commit_until_abs;
        }

        if (next_.empty()) {
            next_.resize(1);
            if (commit_until_abs == 0) {
                next_[0].cost = 0;
            }
        }

        dpflate_bin64k_log_streaming_dp("rotate", commit_until_abs, cur_.size(), next_.size(),
                                       cur_base_, next_base_);
        lzdp_stream_log_streaming_dp("rotate", commit_until_abs, cur_.size(), next_.size(),
                                     cur_base_, next_base_);
        debug_dump_cells("after_rotate");
    }

    void prune_before(uint32_t abs_pos) {
        dpflate_bin64k_log_streaming_dp("prune_before_enter", abs_pos, cur_.size(), next_.size(),
                                       cur_base_, next_base_);
        if (cur_base_ < abs_pos) {
            if (abs_pos < cur_base_ + static_cast<uint32_t>(cur_.size())) {
                const size_t off = static_cast<size_t>(abs_pos - cur_base_);
                cur_.erase(cur_.begin(),
                           cur_.begin() + static_cast<std::ptrdiff_t>(off));
                cur_base_ = abs_pos;
            } else {
                cur_.clear();
                cur_base_ = abs_pos;
            }
        }
        if (next_base_ < abs_pos) {
            if (abs_pos < next_base_ + static_cast<uint32_t>(next_.size())) {
                const size_t off = static_cast<size_t>(abs_pos - next_base_);
                next_.erase(next_.begin(),
                            next_.begin() + static_cast<std::ptrdiff_t>(off));
                next_base_ = abs_pos;
            } else {
                next_.clear();
                next_base_ = abs_pos;
            }
        }
        dpflate_bin64k_log_streaming_dp("prune_before_done", abs_pos, cur_.size(), next_.size(),
                                       cur_base_, next_base_);
    }

    uint32_t cur_base() const { return cur_base_; }
    uint32_t next_base() const { return next_base_; }

    /// 调试：打印 cur_/next_ 有效区间内的 DP 格内容。
    void debug_dump_cells(const char* tag) const {
        if (!DPFlateBin64kDebug::active() || !tag) {
            return;
        }
        if (!cur_.empty()) {
            DPFlateBin64kDebug::log_dp_cells("STREAMING_DP_DUMP", tag, cur_.data(), cur_.size(),
                                             cur_base_, 0);
        }
        if (!next_.empty()) {
            const size_t show = std::min(next_.size(), static_cast<size_t>(32));
            DPFlateBin64kDebug::log_dp_cells("STREAMING_DP_DUMP", tag, next_.data(), show,
                                             next_base_, 0);
        }
    }

private:
    size_t lookahead_{0};
    uint32_t cur_base_{0};
    std::vector<StreamingDpCell> cur_;
    uint32_t next_base_{0};
    std::vector<StreamingDpCell> next_;
};

}  // namespace compressor::algorithm
