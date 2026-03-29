CXX = g++
CXXFLAGS = -O3 -Wall -shared -std=c++14 -fPIC

# Automatically detect Python paths and extensions
PYTHON_INCLUDES = $(shell python -m pybind11 --includes)
PYTHON_LIBS_DIR = $(shell python -c "import sys; print(sys.base_prefix + '/libs')")
PYTHON_LIB_NAME = $(shell python -c "import sys; print(f'python{sys.version_info.major}{sys.version_info.minor}')")
EXT_SUFFIX = $(shell python -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")

INCLUDES = $(PYTHON_INCLUDES) -I./src/include
LDFLAGS = -L"$(PYTHON_LIBS_DIR)" -l$(PYTHON_LIB_NAME)

TARGET = bin/lz77$(EXT_SUFFIX)
SRCS = src/cores/lz77_core.cpp src/bindings/lz77_pybind.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	@if not exist bin mkdir bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET) $(LDFLAGS)

test: $(TARGET)
	python src/test_lz77.py
	python src/test_site_compression.py

build_exe: $(TARGET)
	pip install pyinstaller
	pyinstaller --distpath bin --workpath build --specpath build --onefile --add-data "../bin/lz77$(EXT_SUFFIX);." src/test_site_compression.py
	pyinstaller --distpath bin --workpath build --specpath build --onefile --add-data "../bin/lz77$(EXT_SUFFIX);." src/test_lz77.py
	@if not exist Package mkdir Package
	@if not exist Package\bin mkdir Package\bin
	copy bin\test_site_compression.exe Package\bin\test_site_compression.exe
	copy bin\test_lz77.exe Package\bin\test_lz77.exe
	@if not exist Package\resources mkdir Package\resources
	xcopy /E /I /Y resources Package\resources
	@if not exist Package\compressed mkdir Package\compressed
	@if not exist Package\decompressed mkdir Package\decompressed

clean:
	del /Q $(subst /,\,$(TARGET)) 2>nul || rm -f $(TARGET)
	rmdir /S /Q build Package 2>nul || echo.
