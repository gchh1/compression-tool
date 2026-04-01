@echo off
cd /d "%~dp0"
python -m pip install PySide6 >nul
python src\ui\pyqt_app.py
