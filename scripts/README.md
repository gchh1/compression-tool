# Build and Deployment Scripts

## Scripts Overview

### 1. `scripts/copy_dlls.py`
自动检测并复制 MinGW DLL 依赖到 `bin` 目录。

**功能：**
- 检测 `bin` 目录中的 `.exe` 和 `.pyd` 文件
- 使用 `objdump` 分析 DLL 依赖
- 从 MinGW bin 目录复制所需的 DLL
- 支持重复运行（跳过已存在的相同文件）

**使用：**
```bash
python scripts/copy_dlls.py
```

### 2. `scripts/build_exe.py`
使用 PyInstaller 构建可执行文件，并自动包含 DLL 依赖。

**功能：**
- 自动调用 PyInstaller 构建可执行文件
- 通过 `--add-binary` 参数包含 MinGW DLL
- 支持多个源文件的批量构建

**使用：**
```bash
python scripts/build_exe.py
```

### 3. `build.bat`
完整的构建脚本，自动执行以下步骤：
1. 配置并编译 CMake 项目
2. 运行 `copy_dlls.py` 复制 DLL
3. 运行 `build_exe.py` 构建可执行文件

**使用：**
```bash
build.bat
```

### 4. `package.bat`
打包脚本，创建可移植的发布包。

**功能：**
- 复制可执行文件到 `Package/bin/`
- 复制 DLL 到 `Package/bin/`
- 复制资源文件到 `Package/`

**使用：**
```bash
package.bat
```

## CMake 集成

CMakeLists.txt 已配置自动运行这些脚本：

```cmake
add_custom_target(build_exe
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/copy_dlls.py
    COMMAND ${Python3_EXECUTABLE} -m PyInstaller ...
    DEPENDS lz77
)
```

## 完整工作流程

### 开发环境

```bash
# 1. 构建项目
build.bat

# 2. 运行测试
run.bat
```

### 生产环境

```bash
# 1. 构建项目
build.bat

# 2. 创建可移植包
package.bat

# 3. 分发 Package\ 目录
```

## 文件结构

```
compression-tool/
├── bin/                    # 编译输出
│   ├── lz77.cp312-win_amd64.pyd
│   ├── test_lz77.exe
│   ├── test_site_compression.exe
│   ├── libgcc_s_seh-1.dll  # MinGW 运行时
│   └── libstdc++-6.dll     # MinGW 运行时
├── Package/               # 可移植发布包
│   ├── bin/
│   │   ├── test_lz77.exe
│   │   ├── test_site_compression.exe
│   │   ├── libgcc_s_seh-1.dll
│   │   └── libstdc++-6.dll
│   └── resources/
├── scripts/
│   ├── copy_dlls.py
│   ├── build_exe.py
│   └── README.md
├── build.bat              # 构建脚本
└── package.bat            # 打包脚本
```

## 依赖说明

### MinGW 运行时 DLL
- `libgcc_s_seh-1.dll` - GCC 运行时
- `libstdc++-6.dll` - C++ 标准库
- `libwinpthread-1.dll` - POSIX 线程实现

这些 DLL 在构建时自动复制到 `bin/` 目录，并包含在可执行文件中。
