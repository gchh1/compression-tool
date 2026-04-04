#!/usr/bin/env python3
"""
自动检测并复制 MinGW DLL 依赖到 bin 目录的脚本
用于确保 .exe 和 .pyd 文件能够正常运行
"""

import os
import sys
import subprocess
import io
from pathlib import Path

# 设置输出编码
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')


def get_dll_dependencies_windows(exe_path: Path) -> set:
    """
    使用 objdump 或 dumpbin 检测可执行文件的 DLL 依赖
    
    Args:
        exe_path: 可执行文件路径
        
    Returns:
        依赖的 DLL 文件名集合
    """
    dependencies = set()
    
    # 方法 1: 使用 objdump (MinGW)
    try:
        result = subprocess.run(
            ["objdump", "-p", str(exe_path)],
            capture_output=True,
            text=True,
            timeout=10
        )
        if result.returncode == 0:
            for line in result.stdout.split('\n'):
                if 'DLL Name' in line:
                    dll_name = line.split('DLL Name:')[1].strip()
                    if dll_name:
                        dependencies.add(dll_name)
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    
    # 方法 2: 使用 dumpbin (Visual Studio) - 如果可用
    if not dependencies:
        try:
            result = subprocess.run(
                ["dumpbin", "/dependents", str(exe_path)],
                capture_output=True,
                text=True,
                timeout=10
            )
            if result.returncode == 0:
                for line in result.stdout.split('\n'):
                    if '.dll' in line.lower():
                        dll_name = line.strip()
                        if dll_name and not dll_name.startswith('['):
                            dependencies.add(dll_name)
        except (subprocess.TimeoutExpired, FileNotFoundError):
            pass
    
    return dependencies


def get_mingw_dlls(mingw_bin: Path, dependencies: set) -> list:
    """
    从 MinGW bin 目录中筛选出需要的 DLL
    
    Args:
        mingw_bin: MinGW bin 目录路径
        dependencies: 需要的 DLL 名称集合
        
    Returns:
        (源路径, 目标路径) 元组列表
    """
    dll_pairs = []
    
    # 常见的 MinGW 运行时 DLL
    common_mingw_dlls = [
        "libgcc_s_seh-1.dll",
        "libstdc++-6.dll", 
        "libwinpthread-1.dll",
        "libatomic-1.dll",
        "libgomp-1.dll",
        "libquadmath-0.dll",
    ]
    
    for dll_name in dependencies:
        # 检查是否在 MinGW bin 目录中存在
        src = mingw_bin / dll_name
        if src.exists():
            dll_pairs.append((src, None))  # None 表示使用原名称
        else:
            # 检查常见的 MinGW DLL
            for common_dll in common_mingw_dlls:
                if common_dll in dll_name or dll_name in common_dll:
                    src = mingw_bin / common_dll
                    if src.exists():
                        dll_pairs.append((src, None))
                        break
    
    return dll_pairs


def copy_dlls_to_bin(dll_pairs: list, bin_dir: Path, mingw_bin: Path) -> int:
    """
    复制 DLL 到 bin 目录
    
    Args:
        dll_pairs: (源路径, 目标路径) 元组列表
        bin_dir: 目标 bin 目录
        mingw_bin: MinGW bin 目录（用于找不到时的回退）
        
    Returns:
        复制的 DLL 数量
    """
    copied = 0
    
    for src, dst_name in dll_pairs:
        if not src.exists():
            continue
            
        # 如果没有指定目标名称，使用源文件名
        if dst_name is None:
            dst_name = src.name
            
        dst = bin_dir / dst_name
        
        # 检查是否已存在且相同
        if dst.exists():
            if src.stat().st_size == dst.stat().st_size:
                continue  # 跳过已存在的相同文件
            
        try:
            import shutil
            shutil.copy2(src, dst)
            print(f"✓ 复制: {src.name}")
            copied += 1
        except Exception as e:
            print(f"✗ 复制失败 {src.name}: {e}")
    
    return copied


def main():
    """主函数"""
    # 配置路径
    project_root = Path(__file__).resolve().parent.parent
    bin_dir = project_root / "bin"
    mingw_bin = Path("D:/AAA_C/AAA_MinGW/mingw64/bin")
    
    # 确保目录存在
    bin_dir.mkdir(parents=True, exist_ok=True)
    
    if not mingw_bin.exists():
        print(f"错误: MinGW bin 目录不存在: {mingw_bin}")
        print("请设置正确的 MinGW 路径或手动复制 DLL")
        return 1
    
    # 查找 bin 目录中的可执行文件
    exe_files = list(bin_dir.glob("*.exe")) + list(bin_dir.glob("*.pyd"))
    
    if not exe_files:
        print("警告: bin 目录中没有找到 .exe 或 .pyd 文件")
        print("请先编译项目")
        return 1
    
    print(f"找到 {len(exe_files)} 个可执行文件/模块")
    print(f"MinGW bin 目录: {mingw_bin}")
    print("-" * 50)
    
    # 收集所有依赖
    all_dependencies = set()
    for exe in exe_files:
        print(f"\n检测: {exe.name}")
        deps = get_dll_dependencies_windows(exe)
        print(f"  依赖: {len(deps)} 个 DLL")
        all_dependencies.update(deps)
    
    print(f"\n{'='*50}")
    print(f"总共需要 {len(all_dependencies)} 个唯一的 DLL")
    
    # 获取需要复制的 DLL
    dll_pairs = get_mingw_dlls(mingw_bin, all_dependencies)
    
    if not dll_pairs:
        print("\n警告: 没有找到需要复制的 DLL")
        print("尝试使用 objdump 检测...")
        
        # 尝试手动检测
        for exe in exe_files:
            print(f"\n手动检测: {exe.name}")
            try:
                result = subprocess.run(
                    ["objdump", "-p", str(exe)],
                    capture_output=True,
                    text=True
                )
                if result.returncode == 0:
                    for line in result.stdout.split('\n'):
                        if 'DLL Name' in line:
                            dll = line.split('DLL Name:')[1].strip()
                            if dll:
                                src = mingw_bin / dll
                                if src.exists():
                                    dll_pairs.append((src, None))
                                    print(f"  找到: {dll}")
            except Exception as e:
                print(f"  错误: {e}")
    
    # 复制 DLL
    print(f"\n{'='*50}")
    print("开始复制 DLL...")
    copied = copy_dlls_to_bin(dll_pairs, bin_dir, mingw_bin)
    
    print(f"\n{'='*50}")
    print(f"完成: 复制了 {copied} 个 DLL")
    
    # 列出复制的 DLL
    print("\nbin 目录中的 DLL:")
    for dll in sorted(bin_dir.glob("*.dll")):
        size = dll.stat().st_size
        print(f"  {dll.name} ({size} bytes)")
    
    return 0 if copied > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
