# ========================== Pybind Bindings ==========================

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_package(pybind11 REQUIRED)

pybind11_add_module(core_engine pybind_module.cpp)

target_include_directories(core_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/api/include
)

target_link_libraries(core_engine PRIVATE api)
