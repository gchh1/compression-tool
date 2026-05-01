add_library(
    algorithm
    STATIC
    Delta.cpp
    AlgorithmBase.cpp
    Deflate.cpp

    # Huffman.cpp
    HuffmanTree.cpp

    Inflate.cpp
    # LZ77.cpp
    # LZSS.cpp
)

target_include_directories(
    algorithm
    PUBLIC
    include
)

target_link_libraries(
    algorithm
    PUBLIC
    utils
)
