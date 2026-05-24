add_library(ade INTERFACE)
target_include_directories(ade INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/runtime ${CMAKE_CURRENT_SOURCE_DIR}/models)
target_compile_features(ade INTERFACE cxx_std_20)