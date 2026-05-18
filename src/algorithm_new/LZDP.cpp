

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "LZDP.hpp"

#include "ByteView.hpp"
#include "Streaming.hpp"




namespace compressor::algorithm {

template <typename Input, typename DPNodes>
void LZDP::dpforward(
    const Input& input,
    DPNodes& dp,
    size_t begin,
    size_t end) 
{
    if (end == 0) {
        end = begin + input.window_size();
    }

    for (size_t pos = begin; pos < end; ++pos) {

        const size_t search_len = (pos > config_.window.search_size) ? config_.window.search_size : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = end - pos;
        const size_t look_len = (remain > config_.window.look_size) ? config_.window.look_size : remain;

        RelativeByteSlice<Input> search_view{input, search_start};
        RelativeByteSlice<Input> look_view{input, pos};

        std::vector<Triple> match_results;
        if (config_.dp.match_engine == models::MatchEngine::KMP) {
            match_results = LZMatcher::kmpSearch(
                search_view,
                search_len,
                look_view,
                look_len,
                config_.dp.dp_top,
                config_.window.min_match_len);

        } else if (config_.dp.match_engine == models::MatchEngine::HashChain) {
            match_results = LZMatcher::hashChainSearch(
                search_view,
                search_len,
                look_view,
                look_len,
                config_.dp.dp_top,
                config_.window.min_match_len);
        }

        for (const Triple& match : match_results) {
            const size_t target = pos + match.length;
            if (target >= end) {
                break;
            }
            // 全文件的第一个dpnode必须在函数调用之前初始化
            if (dp[target].pre_pos == 0) {
                //判断字面量还是匹配
                if (match.offset == 0) {
                    dp[target] = models::DPNode(dp[pos].literal_count + 1, dp[pos].match_count, pos-1,match);
                } else {
                    dp[target] = models::DPNode(dp[pos].literal_count, dp[pos].match_count + 1, pos-1,match);
                }
            } else {
                if (match.offset == 0){
                    size_t cur_cost = cal_cost(dp[pos].literal_count+1, dp[pos].match_count);
                    size_t tgt_cost = cal_cost(dp[target].literal_count, dp[target].match_count);
                    if (cur_cost < tgt_cost) {
                        dp[target].literal_count = dp[pos].literal_count + 1;
                        dp[target].match_count = dp[pos].match_count;
                        dp[target].pre_pos = pos-1;
                        dp[target].triple = match;
                    }
                }else{
                    size_t cur_cost = cal_cost(dp[pos].literal_count, dp[pos].match_count+1);
                    size_t tgt_cost = cal_cost(dp[target].literal_count, dp[target].match_count);
                    if (cur_cost < tgt_cost) {
                        dp[target].literal_count = dp[pos].literal_count;
                        dp[target].match_count = dp[pos].match_count + 1;
                        dp[target].pre_pos = pos-1;
                        dp[target].triple = match;
                    }
                }
            }
        }
    }


}

template <typename DpNodes>
std::vector<Triple> LZDP::dpbacktrack(
    DpNodes& dp,//流式模式实际大小大于 end-begin+1
    int& cur_pos,
    size_t begin)
{   
    std::vector<Triple> triples;
    // bool is_streaming = false;
    if (cur_pos == 0){
        cur_pos = static_cast<int>(dp.size());
        // is_streaming = true;
    }

    models::DPNode cur = dp[cur_pos-1];

    //对于非流式模式，cur_pos可能小于0（第一个dpnode的prepos = -1）
    while(cur_pos>=static_cast<int>(begin)){
        triples.push_back(cur.triple);
        cur = dp[cur.pre_pos];
        cur_pos = cur.pre_pos;
    }
    std::reverse(triples.begin(), triples.end());
    return triples;
}

// std::vector<Triple> LZDP::compress_nonstreaming(const std::vector<uint8_t>& input)
// {   
//     size_t inlen = input.size();
//     std::vector<models::DPNode*> dp(inlen, nullptr);
//     dp[0] = new models::DPNode(0,0,-1,Triple(0,1,input[0]));
//     dpforward<std::vector<uint8_t>, std::vector<models::DPNode*>>(input, dp);
//     int cur_pos = 0;//只是为了填充dpbacktrack的参数，实际值无意义
//     return dpbacktrack<std::vector<models::DPNode*>>(dp, cur_pos);
// }











template void LZDP::dpforward<VectorByteInput, std::vector<models::DPNode>>(
    const VectorByteInput&, std::vector<models::DPNode>&, size_t, size_t);

template void LZDP::dpforward<VbByteInput, std::vector<models::DPNode>>(
    const VbByteInput&, std::vector<models::DPNode>&, size_t, size_t);

template std::vector<Triple> LZDP::dpbacktrack<std::vector<models::DPNode>>(
    std::vector<models::DPNode>&, int&, size_t);

}  // namespace compressor::algorithm
