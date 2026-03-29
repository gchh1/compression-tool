#include "lz77_core.h"
// #include <cmath>
#include <vector>
#include <stdexcept>
#include <cstring>

namespace{// 作用域，名称和函数只在这个文件中可见
    // using std::max;

    
    typedef uint16_t u16;
    typedef uint32_t u32;
    typedef uint8_t u8;

    inline u8* u16_to_u8_le(u16* src){// le:little endian 小端存储
        u8* dst = new u8[2];
        dst[0] = static_cast<u8>(*src & 0xFF);
        dst[1] = static_cast<u8>((*src >> 8) & 0xFF);
        return dst;
    }
    inline u16* u8_to_u16_le(u8* src){// le:little endian 小端存储
        return new u16(src[0] | (src[1] << 8));
    }
    inline u8* u16_to_u8_be(u16* src){// be:big endian 大端存储
        u8* dst = new u8[2];
        dst[0] = static_cast<u8>((*src >> 8) & 0xFF);
        dst[1] = static_cast<u8>(*src & 0xFF);
        return dst;
    }
    inline u16* u8_to_u16_be(u8* src){// be:big endian 大端存储
        return new u16((src[0] << 8 )| src[1]);
    }
}

class lz77MatchFunc{// 多态match函数类便于扩展
    public:
    virtual lz77Triple match(const u8*, swd, const u8*, lwd) = 0;
};
class BruteForce:public lz77MatchFunc{
    private:
    std::vector<lwd> kmp_next(const u8* pattern, lwd len){
        std::vector<lwd> next(len, 0);
        if (len == 0) return next;
        lwd j=0;
        for (lwd i=1;i < len;i++){
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
    public:
    lz77Triple match(const u8* search_window, swd search_window_size, 
        const u8* lookahead_window, lwd lookahead_window_size) override{
        /*
        当前索引 - offset 即匹配串第一个字符的位置
        匹配串长度 - length 即匹配串长度
        下一个字符 - next_byte 即下一个字符
        overlapping match: offset 允许小于 length
        本身两个window指针指向一个数组，所以overlap逻辑自洽
        */  
        u8* buffer = new u8[lookahead_window_size];// 缓冲区，用于存储匹配串
        lwd maxlen = 0;// 记录最大匹配长度
        swd mark_idx = 0;

        if (lookahead_window_size == 0) return lz77Triple(0, 0, '\0');

        std::vector<lwd> next = kmp_next(lookahead_window,lookahead_window_size);
        lwd j=0;
        // 处理存在满足理想条件的匹配
        for(swd i = 0; i < search_window_size; i++){
            while (search_window[i]!=lookahead_window[j]&&j!=0){
                j = next[j-1];
            }
            if (search_window[i]==lookahead_window[j]){j++;}
            if (j==lookahead_window_size-1){//第一次超过就会输出三元组
                return lz77Triple(search_window_size - i + j - 1, j, lookahead_window[j]);
            }
            // 此时j的大小就是长度
            if (j >= 4 && j >= maxlen){
                maxlen=j;
                mark_idx=i;
            }
        }
        
        //处理没有合适的匹配，暂时输出单字符token，后面字面游程交给另一个函数后处理
        if (maxlen < 4){//没有产生任何满足最低长度要求的匹配
            return lz77Triple(0, 0, lookahead_window[0]);
        }
        
        // Ensure we don't read out of bounds for the next byte
        u8 next_byte = '\0';
        if (maxlen < lookahead_window_size) {
            next_byte = lookahead_window[maxlen];
        }
        
        return lz77Triple(search_window_size - mark_idx + maxlen - 1, maxlen, next_byte);
    }
};

// LZ77_API class lz77{
//     public:
//     virtual void Compress(const u8*)=0;
//     // virtual void Decompress()=0;
// };
LZ77_API void lz77Compress(// 输入8位字节流，输出8位字节流
    const u8* in_buf,
    u16 search_size,    
    size_t in_len,
    u16 lookahead_size,
    size_t* out_len,
    u8** out_buf// 二级指针承担指针引用的角色对指针进行修改
){
    if (in_buf == nullptr || in_len == 0 || lookahead_size == 0) {
        if (out_buf) *out_buf = nullptr;
        if (out_len) *out_len = 0;
        return;
    }

    BruteForce bf;
    std::vector<u8> encoded;
    encoded.reserve(in_len); // Pre-allocate to avoid frequent reallocations

    size_t pos = 0;
    std::vector<u8> char_buffer;

    auto flush_literals = [&]() {//lambda expression
        size_t run_len = char_buffer.size();
        size_t start = 0;
        lwd size = static_cast<lwd>(0xFF);// 255
        for (size_t i = 1; i < LZ77_LOOKAHEAD_WINDOW/8; i++){
            size = size << 8;
            size = size | 0xFF;
        }
        while (run_len > 0) {
            size_t chunk = (run_len > size) ? size : run_len;
            
            // Write Literal Header (offset = 0)
            if (LZ77_SEARCH_WINDOW == 16) {
                encoded.push_back(0);
                encoded.push_back(0);
            } else {
                encoded.push_back(0);
            }
            
            // Write length (chunk)
            if (LZ77_LOOKAHEAD_WINDOW == 16) {
                encoded.push_back(static_cast<u8>(chunk & 0xFF));
                encoded.push_back(static_cast<u8>((chunk >> 8) & 0xFF));
            } else {
                encoded.push_back(static_cast<u8>(chunk));
            }
            
            // Write actual literals
            for (size_t i = 0; i < chunk; ++i) {
                encoded.push_back(char_buffer[start + i]);
            }
            
            start += chunk;
            run_len -= chunk;
        }
        char_buffer.clear();
    };

    while (pos < in_len) {
        const size_t search_len = (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        size_t look_len = (remain > lookahead_size) ? static_cast<size_t>(lookahead_size) : remain;

        if (look_len == 0) break;

        lz77Triple t = bf.match(
            in_buf + search_start, static_cast<swd>(search_len),
            in_buf + pos, static_cast<lwd>(look_len)
        );

        if (t.offset == 0) {
            char_buffer.push_back(t.next_byte);
        } else {
            flush_literals();

            // Write Match Offset
            if (LZ77_SEARCH_WINDOW == 16) {
                encoded.push_back(static_cast<u8>(t.offset & 0xFF));
                encoded.push_back(static_cast<u8>((t.offset >> 8) & 0xFF));
            } else {
                encoded.push_back(static_cast<u8>(t.offset));
            }
            
            // Write Match Length
            if (LZ77_LOOKAHEAD_WINDOW == 16) {
                encoded.push_back(static_cast<u8>(t.length & 0xFF));
                encoded.push_back(static_cast<u8>((t.length >> 8) & 0xFF));
            } else {
                encoded.push_back(static_cast<u8>(t.length));
            }
            
            // Write next_byte
            encoded.push_back(t.next_byte);
        }
        
        pos += static_cast<size_t>(t.length) + 1;
    }

    flush_literals();

    if (encoded.empty()) {
        if (out_buf) *out_buf = nullptr;
        if (out_len) *out_len = 0;
        return;
    }

    u8* result = new u8[encoded.size()];
    std::memcpy(result, encoded.data(), encoded.size());//vector 转到c风格数组
    
    if (out_buf) *out_buf = result;
    else delete[] result;
    if (out_len) *out_len = encoded.size();
}
LZ77_API void lz77Decompress(
    const u8* in_buf,
    size_t in_len,
    u8* out_buf,
    size_t* out_len
){
    if (in_buf == nullptr || in_len == 0 || out_buf == nullptr || out_len == nullptr) {
        return;
    }
    
    size_t in_pos = 0;
    size_t out_pos = 0;
    
    while (in_pos < in_len) {
        // 读取 offset
        u32 offset = 0;
        if (LZ77_SEARCH_WINDOW == 16) {
            if (in_pos + 1 >= in_len) break;
            offset = in_buf[in_pos] | (in_buf[in_pos + 1] << 8); // 小端还原
            in_pos += 2;
        } else {
            if (in_pos >= in_len) break;
            offset = in_buf[in_pos];
            in_pos += 1;
        }
        
        // 读取 length
        u32 length = 0;
        if (LZ77_LOOKAHEAD_WINDOW == 16) {
            if (in_pos + 1 >= in_len) break;
            length = in_buf[in_pos] | (in_buf[in_pos + 1] << 8); // 小端还原
            in_pos += 2;
        } else {
            if (in_pos >= in_len) break;
            length = in_buf[in_pos];
            in_pos += 1;
        }
        
        if (offset == 0) {
            // 这是一个字面量游程 (Literal run)
            // length 存储了后面有多少个单字符
            for (u32 i = 0; i < length; i++) {
                if (in_pos >= in_len) break;
                out_buf[out_pos++] = in_buf[in_pos++];
            }
        } else {
            // 这是一个匹配 (Match)
            // 此时还要读一个 next_byte
            u8 next_byte = 0;
            if (in_pos < in_len) {
                next_byte = in_buf[in_pos++];
            }
            
            // 拷贝历史数据
            // 注意：支持 overlap，即 offset 可以小于 length，因此必须逐字节拷贝
            size_t copy_start = out_pos - offset;
            for (u32 i = 0; i < length; i++) {
                out_buf[out_pos++] = out_buf[copy_start + i];
            }
            
            // 写入 next_byte
            out_buf[out_pos++] = next_byte;
        }
    }
    
    *out_len = out_pos;
}