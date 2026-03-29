cmake_minimum_required(VERSION 3.12)
project(compression-tool VERSION 1.0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find Python and Pybind11
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
# If pybind11 is not found globally, you might need to install it: pip install pybind11
# Or configure the CMAKE_PREFIX_PATH to point to your Python site-packages/pybind11/share/cmake/pybind11
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --includes
    OUTPUT_VARIABLE PYBIND11_INCLUDES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Simple way when pybind11 config is not in cmake path
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
list(APPEND CMAKE_PREFIX_PATH "${pybind11_DIR}")
find_package(pybind11 CONFIG REQUIRED)

# Include directories
include_directories(src/include)

# Define sources
set(SRCS 
    src/cores/lz77_core.cpp 
    src/bindings/lz77_pybind.cpp
)

# Define target module
pybind11_add_module(lz77 ${SRCS})

# Set output directory to bin/
set_target_properties(lz77 PROPERTIES 
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    PREFIX "" # Ensure no 'lib' prefix on Windows
)

# Custom target for Python tests
add_custom_target(test_python
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/test_lz77.py
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/test_site_compression.py
    DEPENDS lz77
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Python tests..."
)

# Custom target for PyInstaller executable build
add_custom_target(build_exe
    COMMAND ${Python3_EXECUTABLE} -m pip install pyinstaller
    # Build test_lz77.exe
    COMMAND ${Python3_EXECUTABLE} -m PyInstaller --distpath ${CMAKE_SOURCE_DIR}/bin --workpath ${CMAKE_SOURCE_DIR}/build_tmp --specpath ${CMAKE_SOURCE_DIR}/build_tmp --onefile --add-data "../bin/$<TARGET_FILE_NAME:lz77>;." ${CMAKE_SOURCE_DIR}/src/test_lz77.py
    # Build test_site_compression.exe
    COMMAND ${Python3_EXECUTABLE} -m PyInstaller --distpath ${CMAKE_SOURCE_DIR}/bin --workpath ${CMAKE_SOURCE_DIR}/build_tmp --specpath ${CMAKE_SOURCE_DIR}/build_tmp --onefile --add-data "../bin/$<TARGET_FILE_NAME:lz77>;." ${CMAKE_SOURCE_DIR}/src/test_site_compression.py
    # Setup Package structure
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/Package/bin
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin/test_lz77.exe ${CMAKE_SOURCE_DIR}/Package/bin/test_lz77.exe
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin/test_site_compression.exe ${CMAKE_SOURCE_DIR}/Package/bin/test_site_compression.exe
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/Package/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources ${CMAKE_SOURCE_DIR}/Package/resources
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/Package/compressed
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/Package/decompressed
    DEPENDS lz77
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building executables and creating Package..."
)

# Custom target for deep clean
add_custom_target(deep_clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/Package
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/build_tmp
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/build
    COMMENT "Deep cleaning generated directories..."
)
