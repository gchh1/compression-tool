@echo off
cd /d "%~dp0"
echo Installing PySide6...
python -m pip install PySide6 -i https://pypi.tuna.tsinghua.edu.cn/simple
if errorlevel 1 (
    echo Failed to install PySide6
    pause
    exit /b 1
)
echo Starting PyQt application...
python src\ui\pyqt_app.py
if errorlevel 1 (
    echo Application failed to start
    pause
)
