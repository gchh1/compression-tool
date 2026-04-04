#!/usr/bin/env python3
"""
使用 PyInstaller 构建可执行文件，并自动包含所需的 DLL 依赖
"""

import os
import sys
import subprocess
import shutil
import io
from pathlib import Path

# 设置输出编码
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')


def get_project_root() -> Path:
    """获取项目根目录"""
    return Path(__file__).resolve().parent.parent


def get_bin_dir() -> Path:
    """获取 bin 目录"""
    return get_project_root() / "bin"


def get_mingw_bin() -> Path:
    """获取 MinGW bin 目录"""
    return Path("D:/AAA_C/AAA_MinGW/mingw64/bin")


def build_exe_with_pyinstaller(source_py: Path, exe_name: str) -> bool:
    """
    使用 PyInstaller 构建可执行文件
    
    Args:
        source_py: Python 源文件
        exe_name: 输出的可执行文件名（不带 .exe）
        
    Returns:
        是否成功
    """
    project_root = get_project_root()
    bin_dir = get_bin_dir()
    
    # 构建 PyInstaller 命令
    # --add-data: 包含 lz77 模块
    # --add-binary: 包含 DLL 依赖
    lz77_module = bin_dir / "lz77.cp312-win_amd64.pyd"
    
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--onefile",
        "--name", exe_name,
        "--distpath", str(bin_dir),
    ]
    
    # 添加 lz77 模块
    if lz77_module.exists():
        cmd.extend([
            "--add-data", f"{lz77_module};."
        ])
    
    # 添加 DLL 依赖
    mingw_bin = get_mingw_bin()
    if mingw_bin.exists():
        dlls_to_include = [
            "libgcc_s_seh-1.dll",
            "libstdc++-6.dll",
            "libwinpthread-1.dll",
        ]
        for dll in dlls_to_include:
            src = mingw_bin / dll
            if src.exists():
                cmd.extend([
                    "--add-binary", f"{src};."
                ])
    
    # 添加源文件
    cmd.append(str(source_py))
    
    print(f"构建命令: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, cwd=str(project_root), check=True)
        print(f"✓ 成功构建: {exe_name}.exe")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ 构建失败: {e}")
        return False


def main():
    """主函数"""
    project_root = get_project_root()
    bin_dir = get_bin_dir()
    
    # 确保目录存在
    bin_dir.mkdir(parents=True, exist_ok=True)
    
    # 源文件列表
    sources = [
        (project_root / "src" / "test_lz77.py", "test_lz77"),
        (project_root / "src" / "test_site_compression.py", "test_site_compression"),
    ]
    
    print("=" * 60)
    print("PyInstaller 可执行文件构建工具")
    print("=" * 60)
    
    success_count = 0
    total_count = len(sources)
    
    for source_py, exe_name in sources:
        if not source_py.exists():
            print(f"跳过: {source_py} 不存在")
            continue
            
        print(f"\n{'='*60}")
        print(f"构建: {exe_name}")
        print(f"{'='*60}")
        
        if build_exe_with_pyinstaller(source_py, exe_name):
            success_count += 1
    
    print(f"\n{'='*60}")
    print(f"完成: {success_count}/{total_count} 个可执行文件构建成功")
    print(f"{'='*60}")
    
    # 列出构建的文件
    print("\nbin 目录内容:")
    for item in sorted(bin_dir.iterdir()):
        size = item.stat().st_size if item.is_file() else 0
        print(f"  {item.name} ({size} bytes)")
    
    return 0 if success_count > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
