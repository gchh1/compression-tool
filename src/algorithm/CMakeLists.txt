add_library(
    algorithm
    STATIC
    Delta.cpp
    AlgorithmBase.cpp
    Brotli.cpp
    Deflate.cpp

    HuffmanTree.cpp
    HuffmanTree3HM.cpp

    LZDP.cpp
    LZSS.cpp
    DPFlate.cpp
    Inflate.cpp
    Inflate3HM.cpp
    Zstd.cpp
)

target_include_directories(
    algorithm
    PUBLIC
    include
    ${UTILS_DIR}/include
    ${VIZ_DIR}/include
)