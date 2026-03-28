#ifndef LZ77_CORE_H
#define LZ77_CORE_H

#include <stddef.h>
#include <stdint.h>


#ifdef _WIN32
#define LZ77_API __declspec(dllexport)
#else
#define LZ77_API
#endif


#ifdef __cplusplus
extern "C" { // 
#endif


#ifndef LZ77_SEARCH_WINDOW
#define LZ77_SEARCH_WINDOW 16
#endif
#if LZ77_SEARCH_WINDOW == 8
typedef uint8_t swd;
#elif LZ77_SEARCH_WINDOW == 16
typedef uint16_t swd;
#elif LZ77_SEARCH_WINDOW == 32
typedef uint32_t swd;
#else
#error "LZ77_SEARCH_WINDOW must be one of: 8, 16, 32"
#endif


#ifndef LZ77_LOOKAHEAD_WINDOW
#define LZ77_LOOKAHEAD_WINDOW 8
#endif
#if LZ77_LOOKAHEAD_WINDOW == 8
typedef uint8_t lwd;
#elif LZ77_LOOKAHEAD_WINDOW == 16
typedef uint16_t lwd;
#elif LZ77_LOOKAHEAD_WINDOW == 32
typedef uint32_t lwd;
#else
#error "LZ77_LOOKAHEAD_WINDOW must be one of: 8, 16, 32"
#endif



LZ77_API int lz77Compress(
    const uint8_t *input,
    size_t input_len,// 这个长度不会存到压缩文件，直接size_t
    uint16_t search_size,
    uint16_t lookahead_size,
    uint8_t **out_buf,
    size_t *out_len
    
);


struct lz77Triple {
    swd offset;
    lwd length;
    uint8_t next_byte;
    lz77Triple(uint16_t a,swd b,uint8_t c):offset(a),length(b),next_byte(c){}
    lz77Triple():offset(0),length(0),next_byte('\0'){}
};

// LZ77_API int lz77_decompress(

// );

// LZ77_API void lz77_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
