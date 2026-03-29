add_library(
    algorithm_lib 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LZ77.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LZSS.cpp)

target_include_directories(
    algorithm_lib
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
