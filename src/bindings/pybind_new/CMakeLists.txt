find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_package(pybind11 REQUIRED)

pybind11_add_module(
    core_engine_new
    pybind_module_new.cpp
    ../pybind/pybind_ade.cpp
)

target_include_directories(core_engine_new PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core_new/include
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
    ${CMAKE_SOURCE_DIR}/src/api_new/include
    ${CMAKE_SOURCE_DIR}/src/ade/include
    ${CMAKE_SOURCE_DIR}/src/ade/features
    ${CMAKE_SOURCE_DIR}/src/ade/runtime
    ${CMAKE_SOURCE_DIR}/src/ade/models
)

target_link_libraries(core_engine_new PRIVATE api_new core_new algorithm_new ade)

if(MINGW)
    target_compile_options(core_engine_new PRIVATE -Wa,-mbig-obj)
    get_filename_component(_MINGW_DLL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_mingw_runtime_dll IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        add_custom_command(
            TARGET core_engine_new
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_MINGW_DLL_DIR}/${_mingw_runtime_dll}"
                "$<TARGET_FILE_DIR:core_engine_new>/${_mingw_runtime_dll}"
            COMMENT "Copy ${_mingw_runtime_dll} beside core_engine_new"
        )
    endforeach()
endif()
