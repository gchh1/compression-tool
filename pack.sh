#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_DIR"
export PYTHONPATH="src"

SO_FILE=$(ls "$PROJECT_DIR/build/src/bindings/pybind/core_engine"*.so 2>/dev/null | head -1)

if [ -z "$SO_FILE" ]; then
    echo "ERROR: core_engine .so not found. Build C++ first:"
    echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON=ON"
    echo "  cmake --build build --target core_engine --parallel"
    exit 1
fi

echo "=== core_engine: $SO_FILE ==="

PKG_DIR="$PROJECT_DIR/Package/bin"

echo "=== Cleaning Package/bin ==="
rm -rf "$PKG_DIR"

echo "=== Building WebCompress.app ==="
pyinstaller --noconfirm --onedir --windowed \
    --name "WebCompress" \
    --distpath "$PKG_DIR" \
    --workpath "$PROJECT_DIR/build/PyInstaller" \
    --specpath "$PROJECT_DIR/build" \
    --exclude-module PySide6 \
    --exclude-module torch \
    --exclude-module tensorflow \
    --add-data "$PROJECT_DIR/assets/ade/default_model.bin:ade" \
    --add-data "$PROJECT_DIR/assets/ade/training_data_v3.json:ade" \
    --add-data "$SO_FILE:core_engine" \
    "$PROJECT_DIR/src/gui/main.py"

echo ""
echo "=== Done: $PKG_DIR/WebCompress.app ==="
