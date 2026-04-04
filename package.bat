@echo off
cd /d "%~dp0"

echo ========================================
echo Creating portable package
echo ========================================
echo.

rem Clean up existing Package directory
if exist Package (
  echo Cleaning up existing Package directory...
  rmdir /S /Q Package
)

if not exist Package mkdir Package
if not exist Package\bin mkdir Package\bin
if not exist Package\compressed mkdir Package\compressed
if not exist Package\decompressed mkdir Package\decompressed

echo Copying executables...
copy /Y bin\test_lz77.exe Package\bin\
copy /Y bin\test_site_compression.exe Package\bin\

echo Copying DLLs...
copy /Y bin\libgcc_s_seh-1.dll Package\bin\
copy /Y bin\libstdc++-6.dll Package\bin\

echo Copying resources...
if not exist Package\resources mkdir Package\resources
if exist resources\test1.txt copy /Y resources\test1.txt Package\resources\
if exist resources\test1.txt.lz77 copy /Y resources\test1.txt.lz77 Package\resources\
if exist resources\demo_site.lz77 copy /Y resources\demo_site.lz77 Package\resources\
if exist resources\demo_site (
  xcopy /E /I /Y resources\demo_site Package\resources\demo_site\
)

echo.
echo ========================================
echo Package created at Package\
echo ========================================
echo.
echo Package contents:
dir Package /s /b
echo.
echo To run from package:
echo   cd Package
echo   bin\test_lz77.exe
echo   bin\test_site_compression.exe
