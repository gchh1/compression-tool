@echo off

set PROJECT_DIR=d:\AAA_C\compression-tool
cd /d %PROJECT_DIR%
set PYTHONPATH=src

echo Building WebCompress...
pyinstaller --noconfirm --onefile --windowed --name "WebCompress" --distpath "Package\bin" --workpath "build\PyInstaller" --specpath "build" ^
  --exclude-module PySide6 ^
  --exclude-module torch ^
  --exclude-module tensorflow ^
  --add-data "%PROJECT_DIR%\build_pybind\src\bindings\pybind\core_engine.cp312-win_amd64.pyd;core_engine" ^
  --add-data "%PROJECT_DIR%\build_pybind\src\bindings\pybind\libgcc_s_seh-1.dll;core_engine" ^
  --add-data "%PROJECT_DIR%\build_pybind\src\bindings\pybind\libstdc++-6.dll;core_engine" ^
  --add-data "%PROJECT_DIR%\build_pybind\src\bindings\pybind\libwinpthread-1.dll;core_engine" ^
  "src\gui\main.py"

if %errorlevel% neq 0 (
    echo Build failed with error code %errorlevel%
    pause
)
start "" "%~dp0Package\bin\WebCompress.exe"
exit /b 0
