#include "Visualization.hpp"

#include "ByteView.hpp"
#include "HashChain.hpp"
#include "KMP.hpp"

#include <algorithm>

namespace compressor::algorithm {

namespace {

size_t cal_cost(const LZDPConfig& config, size_t literal_count, size_t match_count) {
    if (config.encoding.use_flag_encoding) {
        return literal_count * (config.encoding.offset_bits + 1) +
               match_count * (config.encoding.length_bits + 1);
    }
    size_t tmp = literal_count;
    const size_t maxlen = (size_t{1} << config.encoding.length_bits) - 1;
    size_t cost = 0;
    while (tmp > maxlen) {
        cost += config.encoding.offset_bits + config.encoding.length_bits + maxlen * 8;
        tmp -= maxlen;
    }
    cost += config.encoding.offset_bits + config.encoding.length_bits + tmp * 8;
    return cost;
}

void init_dp_frontier(std::vector<models::DPNode>& dp, const std::vector<uint8_t>& input) {
    if (input.empty()) {
        return;
    }
    dp.assign(input.size() + 1, models::DPNode{0, 0, -2, Triple(0, 0, 0)});
    dp[0] = models::DPNode(0, 0, -1, Triple(0, 0, 0));
}

}  // namespace

DPVisualization get_dp_visualization(
    const std::vector<uint8_t>& input,
    const LZDPConfig& config,
    size_t range) {
    DPVisualization viz;
    viz.input_length = input.size();
    viz.search_size = config.window.search_size;
    viz.lookahead_size = config.window.look_size;

    if (input.empty() || config.window.look_size == 0) {
        return viz;
    }
    if (input.size() > 65536) {
        return viz;
    }

    const size_t dp_top = range == 0 ? config.dp.dp_top : range;
    const size_t in_len = input.size();

    std::vector<models::DPNode> dp;
    init_dp_frontier(dp, input);

    LZDP lzdp(config);
    VectorByteInput view{input, 0};

    for (size_t pos = 0; pos < in_len; ++pos) {
        if (dp[pos].pre_pos == -2) {
            continue;
        }

        DPStep step;
        step.position = pos;
        step.best_cost = cal_cost(config, dp[pos].literal_count, dp[pos].match_count);
        step.best_token_count = dp[pos].literal_count + dp[pos].match_count;

        const size_t lit_lit = dp[pos].literal_count + 1;
        const size_t lit_mat = dp[pos].match_count;
        const size_t lit_cost = cal_cost(config, lit_lit, lit_mat);

        if (pos + 1 <= in_len && pos + 1 < dp.size()) {
            if (dp[pos + 1].pre_pos == -2) {
                dp[pos + 1] = models::DPNode(lit_lit, lit_mat, static_cast<int>(pos),
                                             Triple(0, 0, input[pos]));
            } else {
                const size_t next_cost =
                    cal_cost(config, dp[pos + 1].literal_count, dp[pos + 1].match_count);
                if (lit_cost < next_cost) {
                    dp[pos + 1].literal_count = lit_lit;
                    dp[pos + 1].match_count = lit_mat;
                    dp[pos + 1].pre_pos = static_cast<int>(pos);
                    dp[pos + 1].triple = Triple(0, 0, input[pos]);
                }
            }
        }

        const bool lit_chosen =
            dp[pos + 1].pre_pos == static_cast<int>(pos) && dp[pos + 1].triple.offset == 0 &&
            dp[pos + 1].triple.length == 0;
        step.candidates.push_back(DPCandidate{Triple(0, 0, input[pos]), lit_chosen});

        const size_t search_len =
            (pos > config.window.search_size) ? config.window.search_size : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > config.window.look_size) ? config.window.look_size : remain;

        if (look_len >= config.window.min_match_len) {
            RelativeByteSlice<VectorByteInput> search_view{view, search_start};
            RelativeByteSlice<VectorByteInput> look_view{view, pos};

            std::vector<Triple> match_results;
            if (config.dp.match_engine == models::MatchEngine::KMP) {
                match_results = LZMatcher::kmpSearch(
                    search_view, search_len, look_view, look_len, dp_top,
                    config.window.min_match_len);
            } else {
                match_results = LZMatcher::hashChainSearch(
                    search_view, search_len, look_view, look_len, dp_top,
                    config.window.min_match_len);
            }

            for (const Triple& kr : match_results) {
                if (kr.offset == 0) {
                    continue;
                }
                const size_t target = pos + kr.length;
                if (target > in_len || target >= dp.size()) {
                    continue;
                }

                const size_t mat_lit = dp[pos].literal_count;
                const size_t mat_mat = dp[pos].match_count + 1;
                const size_t mat_cost = cal_cost(config, mat_lit, mat_mat);

                bool is_chosen = false;
                if (dp[target].pre_pos == -2) {
                    dp[target] = models::DPNode(mat_lit, mat_mat, static_cast<int>(pos),
                                              Triple(kr.offset, kr.length, 0));
                    is_chosen = true;
                } else {
                    const size_t tgt_cost =
                        cal_cost(config, dp[target].literal_count, dp[target].match_count);
                    if (mat_cost < tgt_cost) {
                        dp[target].literal_count = mat_lit;
                        dp[target].match_count = mat_mat;
                        dp[target].pre_pos = static_cast<int>(pos);
                        dp[target].triple = Triple(kr.offset, kr.length, 0);
                        is_chosen = true;
                    }
                }
                step.candidates.push_back(DPCandidate{Triple(kr.offset, kr.length, 0), is_chosen});
            }
        }

        viz.steps.push_back(std::move(step));
    }

    int cur_pos = 0;
    viz.optimal_path = lzdp.dpbacktrack(dp, cur_pos, 0);

    viz.dp_array.reserve(in_len + 1);
    for (size_t i = 0; i <= in_len; ++i) {
        DPState st;
        st.position = i;
        if (dp[i].pre_pos != -2) {
            st.reachable = true;
            st.literal_count = dp[i].literal_count;
            st.match_count = dp[i].match_count;
            st.token_count = st.literal_count + st.match_count;
            st.cost = cal_cost(config, st.literal_count, st.match_count);
            st.choice = dp[i].triple;
            st.predecessor = dp[i].pre_pos >= 0 ? static_cast<size_t>(dp[i].pre_pos)
                                                : static_cast<size_t>(-1);
        }
        viz.dp_array.push_back(st);
    }

    return viz;
}

}  // namespace compressor::algorithm
