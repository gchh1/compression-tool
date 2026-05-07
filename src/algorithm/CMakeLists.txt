add_library(
    algorithm
    STATIC
    Delta.cpp
    AlgorithmBase.cpp
    Brotli.cpp
    Deflate.cpp

    # Huffman.cpp (pre-existing compile errors)
    HuffmanTree.cpp

    LZMine.cpp
    LZSS.cpp
    MyFlate.cpp
    Inflate.cpp
    StreamingAdapter.cpp

    # LZ77.cpp
)

target_include_directories(
    algorithm
    PUBLIC
    include
    ${UTILS_DIR}/include
)