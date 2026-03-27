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


#ifndef LZ77_LENGTH_BITS
#define LZ77_LENGTH_BITS 16
#endif

#if LZ77_LENGTH_BITS == 8
typedef uint8_t lz77_length_t;
// #define LZ77_LENGTH_MAX UINT8_MAX
#elif LZ77_LENGTH_BITS == 16
typedef uint16_t lz77_length_t;
// #define LZ77_LENGTH_MAX UINT16_MAX
#elif LZ77_LENGTH_BITS == 32
typedef uint32_t lz77_length_t;
// #define LZ77_LENGTH_MAX UINT32_MAX
#else
#error "LZ77_LENGTH_BITS must be one of: 8, 16, 32"
#endif




LZ77_API int lz77Compress(
    const uint8_t *input,
    size_t input_len,
    uint16_t search_size,
    uint16_t lookahead_size,
    uint8_t **out_buf,
    size_t *out_len
    
);

struct lz77Triple {// (lgth, lgth, u8)
    lz77_length_t offset;
    lz77_length_t length;
    uint8_t next_byte;
    lz77Triple(uint16_t a,lz77_length_t b,uint8_t c):offset(a),length(b),next_byte(c){}
};

// LZ77_API int lz77_decompress(

// );

// LZ77_API void lz77_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
