@echo off
setlocal EnableExtensions

<<<<<<< HEAD
set PROJECT_DIR=d:\yhc\compression-tool
cd /d %PROJECT_DIR%
set PYTHONPATH=src
=======
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
cd /d "%PROJECT_DIR%"
set "PYTHONPATH=src"
set "PKG_DIR=%PROJECT_DIR%\Package\bin"
set "CMAKE_BUILD_DIR=%PROJECT_DIR%\build_py"
if not exist "%CMAKE_BUILD_DIR%\CMakeCache.txt" set "CMAKE_BUILD_DIR=%PROJECT_DIR%\build_debug"
if not exist "%CMAKE_BUILD_DIR%\CMakeCache.txt" set "CMAKE_BUILD_DIR=%PROJECT_DIR%\build"
set "CORE_ENGINE_DIR=%CMAKE_BUILD_DIR%\src\bindings\pybind"
set "CORE_ENGINE_PYD=%CORE_ENGINE_DIR%\core_engine.cp312-win_amd64.pyd"
set "PYI_WORK=%PROJECT_DIR%\build\PyInstaller"
set "PYI_SPEC=%PROJECT_DIR%\build"
set "PYI_CACHE=%LOCALAPPDATA%\pyinstaller"

if not exist "%PKG_DIR%" mkdir "%PKG_DIR%"

echo Cleaning PyInstaller work cache (prevents stale packaged GUI)...
if exist "%PYI_WORK%" (
  rmdir /s /q "%PYI_WORK%" 2>nul
)
if exist "%PYI_CACHE%" (
  rmdir /s /q "%PYI_CACHE%" 2>nul
)

echo Building core_engine (skips if no CMake build dir)...
if exist "%CMAKE_BUILD_DIR%\CMakeCache.txt" (
  echo   Using CMake build dir: "%CMAKE_BUILD_DIR%"
  echo   Forcing relink of core_engine.pyd ...
  if exist "%CORE_ENGINE_PYD%" (
    del /f /q "%CORE_ENGINE_PYD%" 2>nul
  )
  cmake --build "%CMAKE_BUILD_DIR%" --target core_engine --parallel
  if errorlevel 1 (
    echo WARNING: core_engine build failed; PyInstaller may copy an old .pyd
  ) else (
    if not exist "%CORE_ENGINE_PYD%" (
      echo ERROR: core_engine.pyd was not generated after build!
      pause
      exit /b 1
    )
  )
) else (
  echo NOTE: No CMakeCache.txt found in build_py, build_debug, or build. Configure CMake first, e.g.:
  echo   cmake -S . -B build_py -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
)

if not exist "%CORE_ENGINE_PYD%" (
  echo ERROR: Missing core_engine.pyd: "%CORE_ENGINE_PYD%"
  pause
  exit /b 1
)
>>>>>>> d1ada8b9c3d24a7c6e01a5aa2d830596eb7df542

echo Building WebCompress...
pyinstaller --noconfirm --onefile --windowed --name "WebCompress" --icon "%PROJECT_DIR%\resources\icon\WebCompressor.ico" --distpath "%PKG_DIR%" --workpath "%PYI_WORK%" --specpath "%PYI_SPEC%" ^
  --exclude-module PySide6 ^
  --exclude-module torch ^
  --exclude-module tensorflow ^
<<<<<<< HEAD
  --add-data "%PROJECT_DIR%\build_pybind\src\bindings\pybind\core_engine.cp312-win_amd64.pyd;core_engine" ^
  "src\gui\main.py"
=======
  --add-data "%PROJECT_DIR%\assets\ade\default_model.bin;ade" ^
  --add-data "%PROJECT_DIR%\assets\ade\training_data_v3.json;ade" ^
  --add-data "%CORE_ENGINE_PYD%;core_engine" ^
  --add-data "%CORE_ENGINE_DIR%\libgcc_s_seh-1.dll;core_engine" ^
  --add-data "%CORE_ENGINE_DIR%\libstdc++-6.dll;core_engine" ^
  --add-data "%CORE_ENGINE_DIR%\libwinpthread-1.dll;core_engine" ^
  "%PROJECT_DIR%\src\gui\main.py"
>>>>>>> d1ada8b9c3d24a7c6e01a5aa2d830596eb7df542

if %errorlevel% neq 0 (
    echo Build failed with error code %errorlevel%
    pause
    exit /b %errorlevel%
)
start "" "%PKG_DIR%\WebCompress.exe"
exit /b 0
