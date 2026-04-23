add_library(
    algorithm
    STATIC
    Delta.cpp
    Deflate.cpp
    Huffman.cpp
    HuffmanTree.cpp
    LZ77.cpp
    LZSS.cpp
)

target_include_directories(
    algorithm
    PUBLIC
    include
    ${UTILS_DIR}/include
)