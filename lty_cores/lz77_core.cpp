#include "lz77_core.h"
// #include <cmath>

namespace{// 作用域，名称和函数只在这个文件中可见
    // using std::max;

    
    typedef uint16_t u16;
    typedef uint8_t u8;
    typedef lz77_length_t lgth;// 长度单位定义，限制数组长度大小

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
    virtual lz77Triple match(const u8*, lgth, const u8*, lgth) = 0;
};
class BruteForce:public lz77MatchFunc{
    public:
    lz77Triple match(const u8* search_window, lgth search_window_len, 
        const u8* lookahead_window, lgth lookahead_window_len) override{
        /*
        当前索引 - offset 即匹配串第一个字符的位置
        匹配串长度 - length 即匹配串长度
        下一个字符 - next_byte 即下一个字符
        overlapping match: offset 允许小于 length
        本身两个window指针指向一个数组，所以overlap逻辑自洽
        */  
        u8* buffer = new u8[lookahead_window_len];// 缓冲区，用于存储匹配串
        lgth maxlen = 0;// 最大匹配长度
        lz77Triple best_match(0, 0, '\0');
        for(lgth i = 0; i < search_window_len; i++){
            lgth j = 0;
            while(search_window[i + j] == lookahead_window[j] && j!=lookahead_window_len-1){
                j++;
            }
            if (j > maxlen){
                maxlen = j;
                best_match=lz77Triple(search_window_len - i, j, lookahead_window[j]);
            }

        }
        return best_match;
    }
};

LZ77_API class lz77{
    public:
    virtual void Compress(const u8*)=0;
    // virtual void Decompress()=0;
};
LZ77_API void lz77Compress(// 输入8位字节流，输出8位字节流
    const u8* in_buf,
    lgth in_len,
    u16 searching_size,
    u16 lookahead_size,
    u8** out_buf,//
    lgth* out_len
){// 未测试
    if (out_buf == nullptr || out_len == nullptr) {
        return;
    }
    *out_buf = nullptr;
    *out_len = 0;
    if (in_buf == nullptr || in_len == 0 || lookahead_size == 0) {
        return;
    }

    BruteForce matcher;
    const size_t max_triples = static_cast<size_t>(in_len);
    const size_t triple_size = sizeof(lz77Triple);
    u8* scratch = new u8[max_triples * triple_size];
    lz77Triple* triples = reinterpret_cast<lz77Triple*>(scratch);

    size_t write_count = 0;
    size_t pos = 0;
    while (pos < static_cast<size_t>(in_len)) {
        const size_t search_len = (pos > searching_size) ? static_cast<size_t>(searching_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = static_cast<size_t>(in_len) - pos;
        size_t look_len = (remain > lookahead_size) ? static_cast<size_t>(lookahead_size) : remain;

        if (look_len == 0) {
            break;
        }

        lz77Triple t = matcher.match(
            in_buf + search_start, static_cast<lgth>(search_len),
            in_buf + pos, static_cast<lgth>(look_len)
        );

        if (t.offset > search_len) {
            t.offset = static_cast<lz77_length_t>(search_len);
        }
        if (t.length >= look_len) {
            t.length = static_cast<lz77_length_t>(look_len - 1);
        }

        const size_t next_index = pos + static_cast<size_t>(t.length);
        t.next_byte = in_buf[next_index];

        triples[write_count++] = t;
        pos += static_cast<size_t>(t.length) + 1;
    }

    const size_t total_bytes = write_count * triple_size;
    u8* encoded = new u8[total_bytes];
    for (size_t i = 0; i < total_bytes; ++i) {
        encoded[i] = scratch[i];
    }
    delete[] scratch;

    *out_buf = encoded;
    *out_len = static_cast<lgth>(total_bytes);
}