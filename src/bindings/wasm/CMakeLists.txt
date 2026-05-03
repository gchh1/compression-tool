# ==================== WASM Bindings ======================
add_executable(compressor_wasm wasm_bindings.cpp)

target_include_directories(compressor_wasm PRIVATE
    ${CMAKE_SOURCE_DIR}/src/api/include
)

target_link_libraries(compressor_wasm PRIVATE api)

# Emscripten linker flags
set_target_properties(compressor_wasm PROPERTIES
    LINK_FLAGS "--bind -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME='CompressorModule'"
)

set_target_properties(compressor_wasm PROPERTIES SUFFIX ".js")
