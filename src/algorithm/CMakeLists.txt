add_library(
    algorithm
    STATIC
    Delta.cpp
    AlgorithmBase.cpp
    Deflate.cpp

    # Huffman.cpp (pre-existing compile errors)
    HuffmanTree.cpp

    Inflate.cpp
    LZMine.cpp
    LZSS.cpp

    # Inflate.cpp (pre-existing compile errors)

    # LZ77.cpp
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
