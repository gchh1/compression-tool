@echo off
cd /d "%~dp0"

echo ========================================
echo Testing portable package
echo ========================================
echo.

if not exist bin\test_lz77.exe (
  echo ERROR: bin\test_lz77.exe not found.
  echo Please run package.bat first.
  exit /b 1
)

echo Running test_lz77.exe...
bin\test_lz77.exe

if %ERRORLEVEL% EQU 0 (
  echo.
  echo ========================================
  echo All tests passed!
  echo ========================================
) else (
  echo.
  echo ========================================
  echo Tests failed!
  echo ========================================
  exit /b 1
)
