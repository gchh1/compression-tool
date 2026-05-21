add_library(
    core_new
    STATIC
    LZDPcompressor.cpp
    DPflatecompressor.cpp
    LZSScompressor.cpp
    Deflatecompressor.cpp
    GuiCompressors.cpp
    ExternalCodecs.cpp
    StreamingCancel.cpp
)

target_include_directories(
    core_new
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    core_new
    PUBLIC
    algorithm_new
    z
)

target_compile_features(core_new PUBLIC cxx_std_20)
