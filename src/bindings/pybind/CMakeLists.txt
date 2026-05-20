find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_package(pybind11 REQUIRED)

pybind11_add_module(core_engine pybind_module.cpp pybind_ade.cpp)

target_include_directories(core_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core_new/include
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
    ${CMAKE_SOURCE_DIR}/src/api_new/include
    ${CMAKE_SOURCE_DIR}/src/archiver/include
    ${CMAKE_SOURCE_DIR}/src/utils/include
    ${CMAKE_SOURCE_DIR}/src/ade/include
)

# algorithm_new + core_new + api_new (namespace compressor::api). ADE unchanged. Archiver is header-only.
target_link_libraries(core_engine PRIVATE api_new core_new algorithm_new ade utils)

target_compile_definitions(core_engine PRIVATE ADE_WITH_CORE)

if(MINGW)
    target_compile_options(core_engine PRIVATE -Wa,-mbig-obj)
    get_filename_component(_MINGW_DLL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_mingw_runtime_dll IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        add_custom_command(
            TARGET core_engine
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_MINGW_DLL_DIR}/${_mingw_runtime_dll}"
                "$<TARGET_FILE_DIR:core_engine>/${_mingw_runtime_dll}"
            COMMENT "Copy ${_MINGW_DLL_DIR}/${_mingw_runtime_dll} beside core_engine for packaging"
        )
    endforeach()
endif()
