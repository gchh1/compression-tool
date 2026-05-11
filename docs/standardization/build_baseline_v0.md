# 构建基线 v0

> 目的：定义“最低可复现”的构建、打包、冒烟命令，作为每阶段验收底线。

## 1. 环境假设

- OS：Windows 10/11
- CMake >= 3.24
- Python 3.12（与 `core_engine.cp312-win_amd64.pyd` 对齐）
- 编译器：MSVC 或等效 C++20 工具链

## 2. CMake 构建（本地）

```powershell
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON=ON
cmake --build build --parallel
```

说明：

- 顶层入口：`CMakeLists.txt`
- 运行时产物目录：`${CMAKE_BINARY_DIR}/bin`
- Python 绑定路径：`build/src/bindings/pybind/`

## 3. GUI 打包（当前脚本）

使用 `run.bat`：

- 先尝试构建 `core_engine`；
- 再调用 `pyinstaller --onefile --windowed ... src/gui/main.py`；
- 输出到 `Package/bin/WebCompress.exe`。

关键依赖文件（由 `run.bat` 明确指定）：

- `build/src/bindings/pybind/core_engine.cp312-win_amd64.pyd`
- `libgcc_s_seh-1.dll`
- `libstdc++-6.dll`
- `libwinpthread-1.dll`

## 4. 冒烟路径（阶段验收最低要求）

1. 可成功启动 `WebCompress`。
2. GUI 主流程可执行：
   - 添加文件；
   - 完成一次压缩；
   - 完成一次解压；
   - 演示/热力图入口可打开（至少一条路径）。
3. 核心 CLI/测试至少执行一组回归（后续在 CI 里固化）。

## 5. 当前已知薄弱点（待阶段 4 治理）

- 打包脚本内路径假设较强（对 build 目录结构耦合高）。
- `Package/`、`bin/` 与仓库追踪关系不清晰，易造成“产物混入源码提交”。
- 缺少标准化的“干净环境一键构建”文档与 CI 对齐命令。

## 6. 构建验证记录

| 日期 | 命令 | 结果 |
|------|------|------|
| 2026-05-11 | `cmake --build build --parallel 8`（既有 `build/` 配置，MinGW） | 成功，含 `ade`、`core_engine`、`test_*`、`train_ade_model`、`generate_ade_dataset` 等目标 |
