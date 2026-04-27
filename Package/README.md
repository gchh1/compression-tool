# WebCompress Package Structure

## 目录结构

```
Package/
├── bin/            # 可执行程序和 DLL 动态链接库
│   └── WebCompress.exe
├── resources/      # 程序资源文件（如需要）
├── compressed/     # 压缩输出暂存目录（测试用）
└── decompressed/   # 解压输出暂存目录（测试用）
```

## 各目录说明

### bin/
存放主程序和依赖的动态库
- **WebCompress.exe** - 主程序（单文件打包，无外部依赖）
- **其他 DLL** - 如有 C++ 编译则包含 core_engine.pyd 等

### resources/
存放程序运行所需的资源文件
- 图片、图标等 UI 资源
- 配置文件
- 数据文件

### compressed/
测试用压缩输出目录
- 存放压缩后的文件
- 用于验证压缩效果

### decompressed/
测试用解压输出目录
- 存放解压后的文件
- 用于验证解压正确性

## 使用方法

1. 运行程序：
   ```
   ./bin/WebCompress.exe
   ```

2. 添加文件/文件夹进行压缩

3. 压缩输出到 `compressed/` 目录

4. 解压输出到 `decompressed/` 目录

## 版本信息

- **构建时间**: 2026-04-25
- **Python**: 3.12.4
- **PyQt6**: 6.8.2
- **PyInstaller**: 6.20.0

## 注意事项

- 本版本为单文件打包模式，无需额外 DLL
- 首次启动可能稍慢（需要解压到临时目录）
- 如无 Visual Studio 2022，C++ 核心模块会被跳过
