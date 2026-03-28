# 将 dummy_compressor.cpp 编译为静态库
add_library(
    compressor_core 
    ${CORE_DIR}/src/dummy_compressor.cpp)

# 公开 include 目录，让外部知道去哪里找 compressor.hpp
target_include_directories(
    compressor_core 
    PUBLIC
    "${CORE_DIR}/include"
)