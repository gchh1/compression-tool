@echo off
cd /d "%~dp0"

if not exist build mkdir build
cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
cd ..

rem ensure_exe (default CMake ALL) already ran build_exe — exe should be in bin\
if exist bin\test_lz77.exe (
  bin\test_lz77.exe
  if exist bin\test_site_compression.exe (
    bin\test_site_compression.exe
  ) else (
    echo ERROR: bin\test_site_compression.exe not found.
    exit /b 1
  )
) else (
  echo ERROR: bin\test_lz77.exe not found. Check PyInstaller output above.
  exit /b 1
)
