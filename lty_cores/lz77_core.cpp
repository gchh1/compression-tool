#include "lz77_core.h"
// #include <cmath>

namespace{// 作用域，名称和函数只在这个文件中可见
    // using std::max;

    
    typedef uint16_t u16;
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
    const lwd* kmp_next(const u8* pattern, lwd len){
        lwd* next = new lwd[len];//数字范围是lwd类型，长度也是lwd类型
        lwd j=0;next[0]=0;
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
        lwd maxlen = 4;// 记录最大匹配长度，最低要求是4：三元组的大小是2+1+1
        swd mark_idx = 0;

        const lwd* next = kmp_next(lookahead_window,lookahead_window_size);
        lwd j=0;
        // 处理存在满足理想条件的匹配
        for(swd i = 0; i < search_window_size; i++){
            while (search_window[i]!=lookahead_window[j]&&j!=0){
                j = next[j-1];
            }
            if (search_window[i]==lookahead_window[j]){j++;}
            if (j==lookahead_window_size-1){//第一次超过就会输出三元组
                return lz77Triple(search_window_size-i,j,lookahead_window[j]);
            }
            // 此时j的大小就是长度
            if (j>=maxlen){
                maxlen=j,mark_idx=i;
            }
        }
        //处理没有合适的匹配，暂时输出单字符token，后面字面游程交给另一个函数后处理
        if (mark_idx==0){//没有产生任何匹配
            return lz77Triple(0,0,search_window[0]);
        }
        return lz77Triple(search_window_size-mark_idx,maxlen,lookahead_window[maxlen]);
    }
};

// LZ77_API class lz77{
//     public:
//     virtual void Compress(const u8*)=0;
//     // virtual void Decompress()=0;
// };
LZ77_API void lz77Compress(// 输入8位字节流，输出8位字节流
    const u8* in_buf,
    swd in_len,
    u16 searching_size,
    u16 lookahead_size,
    u8** out_buf,//
    lwd out_len
){// 未测试

    if (in_buf == nullptr || in_len == 0 || lookahead_size == 0) {
        return;
    }

    BruteForce bf;
    const size_t max_triples = static_cast<size_t>(in_len);// 最大三元组数
    const size_t triple_size = sizeof(lz77Triple);// 实际大小
    lz77Triple* triples = new lz77Triple[max_triples];

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

        lz77Triple t = bf.match(
            in_buf + search_start, static_cast<lwd>(search_len),
            in_buf + pos, static_cast<lwd>(look_len)
        );

        if (t.offset > search_len) {
            t.offset = static_cast<swd>(search_len);
        }
        if (t.length >= look_len) {
            t.length = static_cast<swd>(look_len - 1);
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
    out_len = static_cast<lwd>(total_bytes);
}