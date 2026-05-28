add_library(
    api_new
    STATIC
    WCXProtocol.cpp
    api.cpp
)

target_include_directories(
    api_new
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core_new/include
    ${CMAKE_SOURCE_DIR}/src/ade/include
)

target_link_libraries(
    api_new
    PUBLIC
    core_new
    algorithm_new
)

target_compile_features(api_new PUBLIC cxx_std_20)
