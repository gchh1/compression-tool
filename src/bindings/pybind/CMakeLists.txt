# ==================== WASM Bindings ======================
# # 编译为 Wasm 模块（注意这里不再是 STATIC 库，而是可执行目标）
# add_executable(compressor_wasm wasm_bindings.cpp)

# # 链接你之前写好的 core 和 algorithm 模块
# target_link_libraries(compressor_wasm PRIVATE compress_core algorithm_lib)

# # 🌟 核心 Wasm 编译参数设定
# set_target_properties(compressor_wasm PROPERTIES
#     # 允许内存动态增长（非常重要，压缩大文件时需要）
#     LINK_FLAGS "--bind -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME='CompressorModule'"
# )

# # 指定输出后缀为 .js (Emscripten 会自动生成配套的 .wasm 文件)
# set_target_properties(compressor_wasm PROPERTIES SUFFIX ".js")

# ========================== Pybind Bindings ==========================

# 🌟 0. 极其暴力的物理锁定：直接把 venv 里的 Python 路径拍在它脸上！
set(Python3_EXECUTABLE "${CMAKE_SOURCE_DIR}/venv/bin/python")
set(Python3_FIND_VIRTUALENV FIRST) # 告诉 CMake 优先认虚拟环境

# 1. 强制 CMake 按照我们上面的设定去找 Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# 2. 去问这个被锁定的 Python："你的 pybind11 在哪？"
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 3. 完美找到 pybind11
find_package(pybind11 REQUIRED)

# 4. 编译为 Python 扩展模块 (.so / .pyd)
pybind11_add_module(core_engine pybind_module.cpp)

# 5. 链接核心库
target_link_libraries(core_engine PRIVATE compress_core algorithm_lib)