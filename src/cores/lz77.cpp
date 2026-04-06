#include "lz77.hpp"

// #include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <queue>
#include <stdexcept>
#include <vector>


using core::u8;
using core::u16;
using core::u32;
using core::u64;


namespace core {
namespace algorithm {

// KMP 辅助函数
std::vector<size_t> LZ77::kmp_next(std::vector<u8>::const_iterator pattern, size_t len){
    std::vector<size_t> next(len, 0);
    if (len == 0) return next;
    size_t j=0;
    for (size_t i=1;i < len;i++){
        while(pattern[i]!=pattern[j]&&j!=0){
            j = next[j-1];
        }
        if (pattern[i]==pattern[j]){
            j++;
        }
        next[i]=j;
    }
    return next;
}



struct maxheap{// 大顶堆比较函数
    bool operator()(const LZ77::lz77Triple* a, const LZ77::lz77Triple* b){
        return a->length < b->length;
    }
    bool operator()(const LZ77::lz77Triple& a, const LZ77::lz77Triple& b){
        return a.length < b.length;
    }
};
std::vector<LZ77::lz77Triple> LZ77::kmpMatch(
    std::vector<u8>::const_iterator search_window,size_t search_window_size,
    std::vector<u8>::const_iterator lookahead_window,size_t lookahead_window_size,
    size_t range
){
    if (search_window_size>max_search_size){
        search_window_size = max_search_size;
        std::printf("log:search_window_size is truncated to %zu\n",max_search_size);
    }
    if (lookahead_window_size>max_look_size){
        lookahead_window_size = max_look_size;
        std::printf("log:lookahead_window_size is truncated to %zu\n",max_look_size);
    }
    std::priority_queue<lz77Triple,std::vector<lz77Triple>,maxheap> pq;
    size_t maxlen = 0;// 记录最大匹配长度
    size_t mark_idx = 0;

    if (lookahead_window_size == 0) {
        return std::vector<lz77Triple>{LZ77::lz77Triple(0, 0, 0)};
    }

    std::vector<size_t> next = kmp_next(lookahead_window,lookahead_window_size);
    size_t j=0;
    // 处理存在满足理想条件的匹配
    for(size_t i = 0; i < search_window_size; i++){
        while (search_window[i]!=lookahead_window[j]&&j!=0){
            j = next[j-1];
        }
        if (search_window[i]==lookahead_window[j]){j++;}
        if (j==lookahead_window_size-1){//第一次超过就会输出三元组
            if (range>1){
                pq.push(lz77Triple(search_window_size - i + j - 1, j, lookahead_window[j]));
            }else if (range==1){
                std::vector<lz77Triple> arrs;
                arrs.push_back(lz77Triple(search_window_size - i + j - 1, j, lookahead_window[j]));
                return arrs;
            }
        }else if (j>=4){
            if (range>1){
                pq.push(lz77Triple(search_window_size - i + j - 1, j, lookahead_window[j]));
            }else if (range==1&&j>=maxlen){
                maxlen=j;
                mark_idx=i;
            }
        }
        
    }
    if (range==1){
        std::vector<lz77Triple> arrs;
        if (maxlen>=4){
            arrs.push_back(lz77Triple(search_window_size - mark_idx + maxlen - 1, maxlen, lookahead_window[maxlen]));
        }else{
            arrs.push_back(lz77Triple(0, 0, lookahead_window[0]));
        }
        return arrs;
    }
    std::vector<lz77Triple> arrs(range);    
    for (size_t i=0;!pq.empty()&&i<range;i++){
        arrs[i] = pq.top();
        pq.pop();
    }
    if (arrs.size() < range){
        arrs.push_back(lz77Triple(0, 0, lookahead_window[0]));
    }
    return arrs;
}
LZ77::lz77Triple LZ77::random(std::vector<lz77Triple> arrs) {//用于nongreedy策略的辅助函数
    size_t n = arrs.size();
    std::vector<size_t> probs = std::vector<size_t>(n+1,0);
    for (size_t i=1;i<=n;i++){
        probs[i] = probs[i-1] + arrs[i-1].length;
    }
    size_t prob = rand() % std::max(probs[n], static_cast<size_t>(1));// 避免0
    for (size_t i=1;i<=n;i++){
        if (prob < probs[i]){
            return arrs[i-1];
        }
        prob -= probs[i];
    }
    std::printf("log:random in lz77 error\n");
    return lz77Triple(0, 0, '\0');
}

template<typename T>
class ROLListNode{//Reverse_Order_Linked_List
    friend class LZ77;
private:
    size_t num;

    T data;
    ROLListNode<T>* pre;
public:
    //T必须要有默认构造函数
    ROLListNode(size_t num = 0,T data = T(), ROLListNode<T>* pre = nullptr):num(num), data(data), pre(pre) {}
};
std::vector<u8> LZ77::Compress_ultra(
    const std::vector<u8>& input,
    size_t search_size,    
    size_t lookahead_size,
    size_t range,
    lz77MatchType match_type
){
    if (search_size>max_search_size){
        search_size = max_search_size;
        std::printf("log:search_size is truncated to %zu\n",max_search_size);
    }
    if (lookahead_size>max_look_size){
        lookahead_size = max_look_size;
        std::printf("log:lookahead_size is truncated to %zu\n",max_look_size);
    }

    size_t in_len = input.size();
    if (in_len == 0||lookahead_size == 0) {
        return std::vector<u8> {};
    }
    // // 根据策略选择函数指针
    // MatchFunc match_func;
    // if (match_type == lz77MatchType::KMPNEXT){
    //     match_func = rangekmpMatch;
    // } else {
    //     throw std::runtime_error("Unsupported match type in lz77Compress");
    // }
    size_t pos=0;
    using Node = ROLListNode<lz77Triple>;
    std::vector<Node*> dp(in_len);// dp[i] 表示最后一个tripple以 input[i] 结尾的最优解(tripple最少)
    dp[0] = new Node(1,lz77Triple(0, 0, input[0]),nullptr);
    
    for (size_t pos=1;pos<in_len;pos++){//由前往后dp
        const size_t search_len = (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len = (remain > lookahead_size) ? static_cast<size_t>(lookahead_size) : remain;
        if(look_len == 0 || dp[pos-1] == nullptr){
            continue;
        }

        std::vector<lz77Triple> match = kmpMatch(input.begin()+search_start,search_len,input.begin()+pos,look_len,range);
        for (lz77Triple m:match){//先得到长的匹配后读到短的
            size_t len = std::max(m.length, static_cast<size_t>(1));// 最小匹配长度为1
            
            if (dp[pos+len] == nullptr){
                dp[pos+len] = new Node(dp[pos-1]->num+1,m,dp[pos-1]);
            }
            if (dp[pos-1]->num+1<dp[pos+len]->num){
                dp[pos+len]->num = dp[pos-1]->num+1;
                dp[pos+len]->data = m;
                dp[pos+len]->pre = dp[pos-1];
            }
            
        }
    }
    std::vector<lz77Triple> triples;
    Node* cur = dp[in_len-1];
    while (cur != nullptr){
        triples.push_back(cur->data);
        cur = cur->pre;
    }
    std::vector<u8> encoded;
    for (size_t i=triples.size()-1;i>=0;i--){
        lz77Triple t = triples[i];
        core::concat(encoded,core::to_u8(t.offset,SEARCH_BYTELENGTH));
        core::concat(encoded,core::to_u8(t.length,LOOKAHEAD_BYTELENGTH));
        encoded.push_back(t.next_byte);
    }
    for (size_t i = 0; i < in_len; i++) {
        if (dp[i] != nullptr) delete dp[i];
    }

    return literalrun(encoded);
}
std::vector<u8> LZ77::Compress(// 输入 8 位字节流，输出 8 位字节流
    const std::vector<u8>& input,
    size_t search_size,    
    size_t lookahead_size,
    lz77MatchType match_type
){
    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return std::vector<u8> {};
    }
  
    if (match_type != lz77MatchType::KMPNEXT) {
        throw std::runtime_error("Unsupported match type in lz77Compress");
    }

    std::vector<u8> encoded;
    encoded.reserve(in_len); // Pre-allocate to avoid frequent reallocations

    size_t pos = 0;

    while (pos < in_len) {
        const size_t search_len = (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        size_t look_len = (remain > lookahead_size) ? static_cast<size_t>(lookahead_size) : remain;

        if (look_len == 0) break;
        //可能包含 overlap 匹配
        lz77Triple t = kmpMatch(
            input.begin() + search_start, static_cast<size_t>(search_len),
            input.begin() + pos, static_cast<size_t>(look_len),
            1
        )[0];

        // Write Match Offset
        core::concat(encoded, core::to_u8(t.offset,SEARCH_BYTELENGTH));
        // Write Match Length
        core::concat(encoded, core::to_u8(t.length,LOOKAHEAD_BYTELENGTH));
        // Write next_byte
        encoded.push_back(t.next_byte);

        pos += static_cast<size_t>(t.length) + 1;
    }

    if (encoded.empty()) {
        return std::vector<u8> {};
    }
    return literalrun(encoded);
}
std::vector<u8> LZ77::literalrun(const std::vector<u8>& input){

    size_t in_len = input.size();
    std::vector<u8> encoded;
    encoded.reserve(input.size() * 2);
    std::vector<u8> buffer;

    auto flush = [&]() {
        if (buffer.empty()) return;
        size_t buf_size = buffer.size();
        size_t buf_start = 0;
        while(buf_size > 0){
            size_t chunk_size = (buf_size > max_look_size) ? max_look_size : buf_size;
            // Write offset = 0
            core::concat(encoded, std::vector<u8>(SEARCH_BYTELENGTH, 0));
            // Write length = chunk_size
            core::concat(encoded, core::to_u8(chunk_size, LOOKAHEAD_BYTELENGTH));
            // Write the literal bytes
            core::concat(encoded, buffer.begin() + buf_start, buffer.begin() + buf_start + chunk_size);
            buf_start += chunk_size;
            buf_size -= chunk_size;
        }
        buffer.clear();
    };

    for (size_t i=0;i<in_len;){
        //read offset
        size_t offset = core::from_u8<size_t>(input.begin() + i, SEARCH_BYTELENGTH);
        i += SEARCH_BYTELENGTH;
        //read length
        size_t length = core::from_u8<size_t>(input.begin() + i, LOOKAHEAD_BYTELENGTH);
        i += LOOKAHEAD_BYTELENGTH;
        //read next_byte
        u8 next_byte = input[i];
        i++;
        if (offset == 0){
            // 这是一个字面量游程 (Literal run)
            // 只收集 next_byte，忽略 length 字段
            buffer.push_back(next_byte);
        }else{
            flush();
            core::concat(encoded,core::to_u8(offset,SEARCH_BYTELENGTH));
            core::concat(encoded,core::to_u8(length,LOOKAHEAD_BYTELENGTH));
            encoded.push_back(next_byte);
            // 跳过 match 后面的 length 个字节（如果有的话）
            // 但实际上 match token 后面不应该有额外的字节
            // 这里的 i += length 是错误的，已经注释掉
        }
    }
    flush();
    return encoded;
}


std::vector<u8> LZ77::Decompress(
    const std::vector<u8>& input
){
    if (input.size() == 0) {
        return std::vector<u8> {};
    }
    
    size_t in_pos = 0;
    size_t out_pos = 0;
    size_t in_len = input.size();
    std::vector<u8> out;
    out.reserve(in_len);// 预分配内存，不可以vector<u8> out(in_len);

    while (in_pos < in_len) {
        // 读取 offset,自动识别数据类型
        size_t offset = static_cast<size_t>(core::from_u8<size_t>(input.begin() + in_pos, SEARCH_BYTELENGTH));
        in_pos += SEARCH_BYTELENGTH;
        // 读取 length
        size_t length = static_cast<size_t>(core::from_u8<size_t>(input.begin() + in_pos, LOOKAHEAD_BYTELENGTH));
        in_pos += LOOKAHEAD_BYTELENGTH;
        
        if (offset == 0) {
            // 这是一个字面量游程 (Literal run)
            // length 存储了后面有多少个单字符token
            for (size_t i = 0; i < length; i++) {
                if (in_pos >= in_len) break;
                out.push_back(input[in_pos++]);
                out_pos++;
            }

        } else {// 匹配 (Match)
            // 此时还要读一个 next_byte
            u8 next_byte = 0;
            if (in_pos < in_len) {
                next_byte = input[in_pos++];
            }
            size_t copy_start = 0;
            if (out_pos>= offset){
                copy_start = out_pos - offset;
            }else{
                //报错
                throw std::runtime_error("Offset in lz77 decompression out of range");
            }

            if (offset <= length) {//overlap必须逐字节拷贝
                for (size_t i = 0; i < length; i++) {
                    out.push_back(out[copy_start + i]);
                    out_pos++;
                }
            }else{
                out.insert(out.end(), out.begin() + copy_start, out.begin() + copy_start + length);
                out_pos += length;
            }
            
            // 写入 next_byte
            out.push_back(next_byte);
            out_pos++;
        }
    }
    
    return out;
}

} // namespace algorithm
} // namespace core
