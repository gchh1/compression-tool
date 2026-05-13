add_library(
    algorithm
    STATIC
    Delta.cpp
    AlgorithmBase.cpp
    Brotli.cpp
    Deflate.cpp

    # Huffman.cpp (pre-existing compile errors)
    HuffmanTree.cpp
    HuffmanTree3HM.cpp

    LZDP.cpp
    LZSS.cpp
    DPFlate.cpp
    Inflate.cpp
    Zstd.cpp

    # LZ77.cpp
)

target_include_directories(
    algorithm
    PUBLIC
    include
    ${UTILS_DIR}/include
)