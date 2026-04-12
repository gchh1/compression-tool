# 编译为 Wasm 模块（注意这里不再是 STATIC 库，而是可执行目标）
add_executable(compressor_wasm wasm_bindings.cpp)

# 链接你之前写好的 core 和 algorithm 模块
target_link_libraries(compressor_wasm PRIVATE compress_core algorithm_lib)

# 🌟 核心 Wasm 编译参数设定
set_target_properties(compressor_wasm PROPERTIES
    # 允许内存动态增长（非常重要，压缩大文件时需要）
    LINK_FLAGS "--bind -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME='CompressorModule'"
)

# 指定输出后缀为 .js (Emscripten 会自动生成配套的 .wasm 文件)
set_target_properties(compressor_wasm PROPERTIES SUFFIX ".js")