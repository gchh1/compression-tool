add_library(
    algorithm_new
    STATIC
    LZDP.cpp
    Deflate.cpp
    LZSS.cpp
    Dpflate.cpp
    Visualization.cpp
)

target_include_directories(
    algorithm_new
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core_new/include
)

target_compile_features(algorithm_new PUBLIC cxx_std_20)
