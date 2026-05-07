add_library(
    algorithm
    STATIC
    Delta.cpp
    AlgorithmBase.cpp
    Deflate.cpp

    # Huffman.cpp (pre-existing compile errors)
    HuffmanTree.cpp

    LZMine.cpp
    LZCrazy.cpp
    CrazyFlate.cpp
    LZSS.cpp
    MyFlate.cpp
    Inflate.cpp

    # LZ77.cpp
)

target_include_directories(
    algorithm
    PUBLIC
    include
    ${UTILS_DIR}/include
)