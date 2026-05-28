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
    OpenH264Codec.cpp
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

# ── OpenH264 ──
FetchContent_Declare(
    openh264
    URL https://github.com/cisco/openh264/archive/refs/tags/v2.4.1.tar.gz
)

FetchContent_GetProperties(openh264)
if(NOT openh264_POPULATED)
    FetchContent_Populate(openh264)
endif()

# Compile warnings to suppress in all OpenH264 modules
set(_OPENH264_WARNINGS
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-missing-field-initializers
)

# Separate static libraries for each OpenH264 module.
# The encoder and decoder share file names (e.g. deblocking.h), so we must NOT
# mix their include paths in a single compile command. Each module gets only
# its own headers.

# ── openh264_common ──
file(GLOB_RECURSE _COMMON_SRC
    "${openh264_SOURCE_DIR}/codec/common/src/*.cpp"
    "${openh264_SOURCE_DIR}/codec/common/src/*.c"
)
list(FILTER _COMMON_SRC EXCLUDE REGEX "/(main|test).*\\.(cpp|c)$")
add_library(openh264_common STATIC ${_COMMON_SRC})
target_include_directories(openh264_common PUBLIC
    "${openh264_SOURCE_DIR}/codec/api/wels"
)
target_include_directories(openh264_common PRIVATE
    "${openh264_SOURCE_DIR}/codec/common/inc"
)
target_compile_options(openh264_common PRIVATE ${_OPENH264_WARNINGS})

# ── openh264_encoder ──
file(GLOB_RECURSE _ENCODER_SRC
    "${openh264_SOURCE_DIR}/codec/encoder/core/src/*.cpp"
    "${openh264_SOURCE_DIR}/codec/encoder/plus/src/*.cpp"
)
list(FILTER _ENCODER_SRC EXCLUDE REGEX "/(main|test).*\\.(cpp|c)$")
add_library(openh264_encoder STATIC ${_ENCODER_SRC})
target_include_directories(openh264_encoder PRIVATE
    "${openh264_SOURCE_DIR}/codec/api/wels"
    "${openh264_SOURCE_DIR}/codec/common/inc"
    "${openh264_SOURCE_DIR}/codec/encoder/core/inc"
    "${openh264_SOURCE_DIR}/codec/encoder/plus/inc"
    "${openh264_SOURCE_DIR}/codec/processing/interface"
)
target_link_libraries(openh264_encoder PUBLIC openh264_common)
target_compile_options(openh264_encoder PRIVATE ${_OPENH264_WARNINGS})

# ── openh264_decoder ──
file(GLOB_RECURSE _DECODER_SRC
    "${openh264_SOURCE_DIR}/codec/decoder/core/src/*.cpp"
    "${openh264_SOURCE_DIR}/codec/decoder/plus/src/*.cpp"
)
list(FILTER _DECODER_SRC EXCLUDE REGEX "/(main|test).*\\.(cpp|c)$")
add_library(openh264_decoder STATIC ${_DECODER_SRC})
target_include_directories(openh264_decoder PRIVATE
    "${openh264_SOURCE_DIR}/codec/api/wels"
    "${openh264_SOURCE_DIR}/codec/common/inc"
    "${openh264_SOURCE_DIR}/codec/decoder/core/inc"
    "${openh264_SOURCE_DIR}/codec/decoder/plus/inc"
)
target_link_libraries(openh264_decoder PUBLIC openh264_common)
target_compile_options(openh264_decoder PRIVATE ${_OPENH264_WARNINGS})

# ── openh264_processing ──
file(GLOB_RECURSE _PROC_SRC
    "${openh264_SOURCE_DIR}/codec/processing/src/*.cpp"
    "${openh264_SOURCE_DIR}/codec/processing/src/*.c"
)
list(FILTER _PROC_SRC EXCLUDE REGEX "/(main|test).*\\.(cpp|c)$")
list(FILTER _PROC_SRC EXCLUDE REGEX "/console/")
list(FILTER _PROC_SRC EXCLUDE REGEX "/(loongarch|mips|arm|arm64)/")
add_library(openh264_processing STATIC ${_PROC_SRC})
target_include_directories(openh264_processing PRIVATE
    "${openh264_SOURCE_DIR}/codec/api/wels"
    "${openh264_SOURCE_DIR}/codec/common/inc"
    "${openh264_SOURCE_DIR}/codec/processing/interface"
    "${openh264_SOURCE_DIR}/codec/processing/src/common"
)
target_link_libraries(openh264_processing PUBLIC openh264_common)
target_compile_options(openh264_processing PRIVATE ${_OPENH264_WARNINGS})

# ── openh264 (meta target) ──
add_library(openh264 INTERFACE)
target_link_libraries(openh264 INTERFACE
    openh264_common
    openh264_encoder
    openh264_decoder
    openh264_processing
)
target_include_directories(openh264 INTERFACE
    "${openh264_SOURCE_DIR}/codec/api/wels"
)

# Clean up helper variables
unset(_OPENH264_WARNINGS)
unset(_COMMON_SRC)
unset(_ENCODER_SRC)
unset(_DECODER_SRC)
unset(_PROC_SRC)

target_link_libraries(
    algorithm_new
    PUBLIC
    brotlienc
    brotlidec
    brotlicommon
    openh264
)

target_compile_features(algorithm_new PUBLIC cxx_std_20)