@echo off

set PROJECT_DIR=d:\AAA_C\compression-tool
cd /d %PROJECT_DIR%
set PYTHONPATH=src

echo Building WebCompress...
pyinstaller --noconfirm --onefile --windowed --name "WebCompress" --distpath "Package\bin" --workpath "build\PyInstaller" --specpath "build" "src\gui\main.py"

if %errorlevel% neq 0 (
    echo Build failed with error code %errorlevel%
    pause
)
start "" "%~dp0Package\bin\WebCompress.exe"
exit /b 0
