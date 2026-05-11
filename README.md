# compression-tool

桌面端 **WebCompress** 压缩工具：PyQt6 GUI + C++ 核心（`core_engine`）+ 可选 ADE 决策管线。

---

## 文档与交接

| 文档 | 内容 |
|------|------|
| **[docs/handoff_work_summary.md](docs/handoff_work_summary.md)** | **工作交接总览**：历史改动摘要、GUI 架构迁移对照、维护注意点 |
| [docs/gui_architecture.md](docs/gui_architecture.md) | `src/gui/` 现行目录树、模块边界、对话框落位 |
| [docs/standardization/build_baseline_v0.md](docs/standardization/build_baseline_v0.md) | 可复现 CMake / 构建基线 |
| [src/README.md](src/README.md) | `src/` 下各子系统说明 |

---

## 仓库布局（概要）

```
compression-tool/
├── CMakeLists.txt
├── run.bat                     # Windows：构建 core_engine + PyInstaller 打包 WebCompress
├── docs/                       # 设计、规范、交接与 GUI 架构说明
├── tests/
├── third_party/
├── assets/                     # ADE 默认模型与训练数据等
├── Package/                    # 发布产物目录（运行时生成；勿与源码混淆）
├── build/                      # CMake / PyInstaller 输出（默认 gitignore）
└── src/
    ├── algorithm/              # LZSS、LZDP、DPFlate、Deflate 等基础算法
    ├── core/                   # ICompressor 实现、AlgorithmFactory
    ├── bindings/pybind/        # pybind11 → core_engine（.pyd）
    ├── api/                    # C API 层（如启用）
    ├── processor/、archiver/、utils/、ade/
    └── gui/                    # Python GUI（见 docs/gui_architecture.md）
```

旧版 README 中「`core/src`」式布局已废弃；**以 `src/` 下实际目录为准**。

---

## 运行 GUI

```powershell
cd compression-tool
$env:PYTHONPATH = "src"
python -m gui
```

或直接运行入口（已内置将 `src/` 加入 `sys.path`）：

```powershell
python src/gui/main.py
```

未捕获异常会写入日志（打包无控制台时尤其重要）；日志目录见 `.gitignore` 与 `gui.utils.logging` 配置。

---

## 构建 C++ 与 Python 扩展

首次需配置 CMake 生成目录（示例）：

```powershell
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON=ON
cmake --build build --target core_engine
```

生成的 `core_engine` 扩展位于 `build/src/bindings/pybind/`（具体文件名随 Python 版本与平台变化）。修改 `src/algorithm` 或 `src/core` 后**必须重新编译**再运行 GUI 或重新执行 `run.bat` 打包。

Windows 一键打包 GUI：在项目根目录执行 **`run.bat`**（依赖已配置的 `build/` 与 PyInstaller）。

---

## GUI 与核心参数

- 算法参数与默认值：`src/gui/models.py`（`ALGORITHM_PARAMS`、`get_default_config`）。
- 引擎将配置键映射到 C++：`set_<key>`，例如 `use_flag_encoding` → `set_use_flag_encoding`。
- **LZSS**：`use_flag_encoding` 切换两套互斥的压缩比特流布局；解压须与压缩使用相同设置。
- **DPFlate**：输出仍为 Deflate 兼容流；该开关主要影响 DP 阶段的代价启发式，体积变化因数据而异。
- **LZDP**：编码方案在算法层已有完整实现（见 `src/algorithm/LZDP.cpp`）。

---

## 历史笔记（归档）

早期版本曾包含浏览器端绑定（已移除）。BitReader / BitWriter 等底层 API 说明见各头文件与 `src/utils`、`src/algorithm` 内实现。

更完整的时间线与文件级改动说明见 **[docs/handoff_work_summary.md](docs/handoff_work_summary.md)**。
