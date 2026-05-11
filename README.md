# compression-tool

桌面端 **WebCompress** 压缩工具：PyQt6 GUI + C++ 核心（`core_engine`）+ 可选 ADE 决策管线。

---

## 文档与交接

| 文档 | 内容 |
|------|------|
| **[docs/handoff_work_summary.md](docs/handoff_work_summary.md)** | **工作交接总览**：历史改动摘要、GUI 架构迁移对照、维护注意点 |
| [docs/gui_architecture.md](docs/gui_architecture.md) | `src/gui/` 现行目录树、模块边界、对话框落位 |
| [docs/standardization/README.md](docs/standardization/README.md) | **标准化文档索引**（构建基线、路径映射、风险表等） |
| [docs/standardization/build-baseline.md](docs/standardization/build-baseline.md) | 可复现 CMake / 构建基线 |
| [src/README.md](src/README.md) | `src/` 下各子系统说明 |

---

## 仓库布局（概要）

与 **`docs/gui_architecture.md`**（GUI 细部）及 **`src/README.md`**（C++/子系统）一致；以下为仓库根与 `src/` 顶层真实目录。

```
compression-tool/
├── CMakeLists.txt
├── run.bat                     # Windows：构建 core_engine + PyInstaller 打包 WebCompress
├── docs/                       # 设计、规范、交接；含 gui_architecture.md
├── tests/
├── third_party/
├── assets/                     # ADE 默认模型与训练数据等
├── resources/                  # 图标、演示站点与测试样例等
├── Package/                    # 发布产物（运行时生成；勿与源码混淆）
├── build/                      # CMake / PyInstaller 输出（默认 .gitignore）
└── src/
    ├── algorithm/              # LZSS、LZDP、DPFlate、Deflate、Huffman、Zstd、Brotli 等
    ├── core/                   # ICompressor 实现、AlgorithmFactory
    ├── processor/              # Pipeline、ChunkedStreamAdapter、流式压缩链路
    ├── archiver/               # Pack 写读、归档格式
    ├── api/                    # 文件/缓冲压缩 C API（非 GUI）
    ├── ade/                    # C++ ADE：特征、决策引擎、工具链等
    ├── utils/                  # Bit I/O、DataChunk、MemoryPool 等共用基础设施
    ├── bindings/
    │   └── pybind/             # pybind11 → core_engine（.pyd）
    └── gui/                    # PyQt6 应用；**完整树见 docs/gui_architecture.md §一**
        ├── main.py             # run_gui / run_cli；入口亦见 __main__.py
        ├── models.py
        ├── config/             # theme.py, settings.py
        ├── engine/             # compressor, bridge, token_parser, file_protocol
        ├── ade/                # Python ADE：engine, features, training, explorer …
        ├── algorithms/         # 可选纯 Python 算法（如 Transformer）
        ├── ui/                 # main_window, worker, table, helpers；views/ panels/ dialogs/
        ├── windows/            # HTML 报告（heatmap、comparison、network_sim …）
        ├── utils/              # logging, resources, file_helper
        └── io/                 # 占位包；目录扫描见 utils/file_helper
```

**GUI 子目录的权威清单**（各 `.py` 文件与对话框列表）以 **`docs/gui_architecture.md` §一** 为准，避免与实现脱节。

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

## `run.bat` 是做什么的？（Windows 一键脚本）

仓库根目录没有 Unix 下的 `./run` 可执行文件；**Windows 上对应入口是 `run.bat`**（资源管理器中双击，或在项目根执行 `.\run.bat` / `run.bat`）。

脚本会依次：

1. **进入项目根目录**，并设置 `PYTHONPATH=src`（与手动开发运行一致）。
2. **清空** `build\PyInstaller\` 工作缓存，避免沿用上次打包的旧文件。
3. **若已存在 CMake 构建目录**（`build\CMakeCache.txt`）：执行 `cmake --build build --target core_engine`，把最新的 `core_engine` 扩展编进 `build\src\bindings\pybind\`；若没有配置过 `build/`，只打印提示，仍会继续尝试打包（可能用上一次的 `.pyd`，存在版本不一致风险）。
4. 调用 **PyInstaller** 将 `src/gui/main.py` 打成**单文件、无控制台**的 **`Package/bin/WebCompress.exe`**，并把 ADE 默认模型/训练数据、`core_engine` 的 `.pyd` 及依赖 DLL 一并打进包内（具体列表见 `run.bat` 内 `--add-data`）。
5. 打包成功则 **自动启动** `Package\bin\WebCompress.exe`。

**使用前请确认**：已按上文完成至少一次 CMake 配置与 `core_engine` 编译；本机已安装 **PyInstaller**；`run.bat` 里写死的 Python 扩展名（如 `cp312-win_amd64`）与当前 Python 版本一致，否则需改脚本或重建对应解释器环境。

---

## 构建 C++ 与 Python 扩展

首次需配置 CMake 生成目录（示例）：

```powershell
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON=ON
cmake --build build --target core_engine
```

生成的 `core_engine` 扩展位于 `build/src/bindings/pybind/`（具体文件名随 Python 版本与平台变化）。修改 `src/algorithm` 或 `src/core` 后**必须重新编译**再运行 GUI 或重新执行 `run.bat` 打包。

Windows 一键打包 GUI：在项目根目录执行 **`run.bat`**；作用说明见上一节 **`run.bat` 是做什么的**。

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
