@echo off
setlocal EnableExtensions

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
cd /d "%PROJECT_DIR%"
set "PYTHONPATH=src"
set "PKG_DIR=%PROJECT_DIR%\Package\bin"
set "PYI_WORK=%PROJECT_DIR%\build\PyInstaller"
set "PYI_SPEC=%PROJECT_DIR%\build"

if not exist "%PKG_DIR%" mkdir "%PKG_DIR%"

echo Cleaning PyInstaller work cache (prevents stale packaged GUI)...
if exist "%PYI_WORK%" (
  rmdir /s /q "%PYI_WORK%" 2>nul
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
pyinstaller --noconfirm --onefile --windowed --name "WebCompress" --icon "%PROJECT_DIR%\resources\icon\WebCompressor.ico" --distpath "%PKG_DIR%" --workpath "%PYI_WORK%" --specpath "%PYI_SPEC%" ^
  --exclude-module PySide6 ^
  --exclude-module torch ^
  --exclude-module tensorflow ^
  --add-data "%PROJECT_DIR%\assets\ade\default_model.bin;ade" ^
  --add-data "%PROJECT_DIR%\assets\ade\training_data_v3.json;ade" ^
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
start "" "%PKG_DIR%\WebCompress.exe"
exit /b 0
