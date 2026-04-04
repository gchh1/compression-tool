@echo off
cd /d "%~dp0"

if not exist build mkdir build
cd build
cmake -G "MinGW Makefiles" ..
mingw32-make

rem Copy DLL dependencies to bin
cd ..
python scripts\copy_dlls.py

rem Build executables with PyInstaller (includes DLLs)
python scripts\build_exe.py

echo.
echo ========================================
echo Build complete!
echo ========================================
echo.
echo Executables in bin\:
dir bin\*.exe /b
echo.
echo DLLs in bin\:
dir bin\*.dll /b
echo.
echo To run:
echo   bin\test_lz77.exe
echo   bin\test_site_compression.exe
