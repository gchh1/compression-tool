include(FetchContent)
set(_BROTLI_LOCAL "${CMAKE_SOURCE_DIR}/third_party/brotli")
if(EXISTS "${_BROTLI_LOCAL}/CMakeLists.txt")
    message(STATUS "Using vendored libbrotli: ${_BROTLI_LOCAL}")
    FetchContent_Declare(google_brotli SOURCE_DIR "${_BROTLI_LOCAL}")
else()
    message(STATUS "Fetching libbrotli from GitHub (requires network)")
    FetchContent_Declare(
        google_brotli
        GIT_REPOSITORY https://github.com/google/brotli.git
        GIT_TAG v1.1.0
        GIT_SHALLOW TRUE
    )
endif()
set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BROTLI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Static libbrotli so core_engine does not need libbrotlienc.dll beside the .pyd.
set(_brotli_saved_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(google_brotli)
set(BUILD_SHARED_LIBS "${_brotli_saved_shared}" CACHE BOOL "" FORCE)
unset(_brotli_saved_shared)
unset(_BROTLI_LOCAL)

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
    DPFlateBin64kDebug.cpp
    LZDPStreamDebug.cpp
    DPFlateTrace.cpp
    Inflate.cpp
    Inflate3HM.cpp
    Zstd.cpp

    # LZ77.cpp
)

target_include_directories(
    algorithm
    PUBLIC
    include
    ${UTILS_DIR}/include
)

target_link_libraries(
    algorithm
    PUBLIC
    brotlienc
    brotlidec
    brotlicommon
)