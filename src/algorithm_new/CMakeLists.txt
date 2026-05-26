add_library(
    algorithm_new
    STATIC
    AudioCodec.cpp
    AudioCodecAac.cpp
    Brotli.cpp
    Deflate.cpp
    Dpflate.cpp
    ImageCompressor.cpp
    LZDP.cpp
    LZSS.cpp
    VideoCodec.cpp
    Visualization.cpp
    Zstd.cpp
)

target_include_directories(
    algorithm_new
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core_new/include
    ${CMAKE_SOURCE_DIR}/third_party
)

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
set(_brotli_saved_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(google_brotli)
set(BUILD_SHARED_LIBS "${_brotli_saved_shared}" CACHE BOOL "" FORCE)
unset(_brotli_saved_shared)
unset(_BROTLI_LOCAL)

target_link_libraries(
    algorithm_new
    PUBLIC
    brotlienc
    brotlidec
    brotlicommon
)

target_compile_features(algorithm_new PUBLIC cxx_std_20)
