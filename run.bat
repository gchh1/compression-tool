@echo off
setlocal EnableExtensions

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
cd /d "%PROJECT_DIR%"
set "PYTHONPATH=src"

echo Cleaning PyInstaller work cache (prevents stale packaged GUI)...
if exist "%PROJECT_DIR%\build\PyInstaller" (
  rmdir /s /q "%PROJECT_DIR%\build\PyInstaller" 2>nul
)

echo Building core_engine (skips if no CMake build dir)...
if exist "%PROJECT_DIR%\build\CMakeCache.txt" (
  cmake --build "%PROJECT_DIR%\build" --target core_engine --parallel
  if errorlevel 1 (
    echo WARNING: core_engine build failed; PyInstaller may copy an old .pyd
  )
) else (
  echo NOTE: No "%PROJECT_DIR%\build\CMakeCache.txt" — configure CMake first, e.g.:
  echo   cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
)

echo Building WebCompress...
pyinstaller --noconfirm --onefile --windowed --name "WebCompress" --icon "%PROJECT_DIR%\resources\icon\WebCompressor.ico" --distpath "%PROJECT_DIR%\Package\bin" --workpath "%PROJECT_DIR%\build\PyInstaller" --specpath "%PROJECT_DIR%\build" ^
  --exclude-module PySide6 ^
  --exclude-module torch ^
  --exclude-module tensorflow ^
  --add-data "%PROJECT_DIR%\build\src\bindings\pybind\core_engine.cp312-win_amd64.pyd;core_engine" ^
  --add-data "%PROJECT_DIR%\build\src\bindings\pybind\libgcc_s_seh-1.dll;core_engine" ^
  --add-data "%PROJECT_DIR%\build\src\bindings\pybind\libstdc++-6.dll;core_engine" ^
  --add-data "%PROJECT_DIR%\build\src\bindings\pybind\libwinpthread-1.dll;core_engine" ^
  "%PROJECT_DIR%\src\gui\main.py"

if %errorlevel% neq 0 (
    echo Build failed with error code %errorlevel%
    pause
    exit /b %errorlevel%
)
start "" "%PROJECT_DIR%\Package\bin\WebCompress.exe"
exit /b 0
