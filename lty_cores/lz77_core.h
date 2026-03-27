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

LZ77_API int lz77Compress(
    const uint8_t *input,
    size_t input_len,
    uint16_t search_size,
    uint16_t lookahead_size,
    uint8_t **out_buf,
    size_t *out_len
);

struct Lz77Triple {
    uint16_t offset;
    uint8_t length;
    uint8_t next_byte;
};

// LZ77_API int lz77_decompress(

// );

// LZ77_API void lz77_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
