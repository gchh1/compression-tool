#include "lz77_core.h"
#include 

namespace lz77{// 作用域，名称和函数只在这个文件中可见
    typedef uint16_t u16;
    typedef uint8_t u8;

    inline u8* u16_to_u8_le(u16 src){// le:little endian 小端存储
        u8* dst = (u8*)malloc(2);
        dst[0] = static_cast<u8>(src & 0xFF);
        dst[1] = static_cast<u8>((src >> 8) & 0xFF);
        return dst;
    }
    inline u8* u8_to_u16_le(u8* src){// le:little endian 小端存储
        u16 dst = static_cast<u16>(src[0] | (src[1] << 8));
        return dst;
    }
    inline u8* u16_to_u8_be(u16 src){// be:big endian 大端存储
        u8* dst = (u8*)malloc(2);
        dst[0] = static_cast<u8>((src >> 8) & 0xFF);
        dst[1] = static_cast<u8>(src & 0xFF);
        return dst;
    }
    inline u8* u8_to_u16_be(u8* src){// be:big endian 大端存储
        u16 dst = static_cast<u16>(src[0] | (src[1] << 8));
        return dst;
    }
}

class lz77MatchFunc{
    public:
    virtual lz77Triple match(const uint8_t*) = 0;
}

LZ77_API int lz77Compress(
    const uint8_t *input,
    size_t input_len,
    uint16_t searching_size,
    uint16_t lookahead_size,
    uint8_t **out_buf,
    size_t *out_len
){

}