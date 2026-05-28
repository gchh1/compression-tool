# WebCompress GUI 应用程序开发报告

> 文档范围：`src/gui/` 模块（PyQt6 桌面客户端）  
> 关联文档：[gui_architecture.md](./gui_architecture.md)、[algorithm_new_gui_bindings.md](./algorithm_new_gui_bindings.md)  
> 更新日期：2026-05-22

---

## 一、项目概述

### 1.1 开发背景与目标

WebCompress 是一套面向 Web 资源与通用二进制数据的**实验性压缩工具链**，核心编解码由 C++（`algorithm_new`、`api_new`、`core_engine`）实现，并封装为 WCX 容器格式。GUI 应用程序的目标是在**不暴露命令行复杂度**的前提下，为研发与测试人员提供：

- 可视化的文件/文件夹批量压缩与解压；
- 多种算法（LZSS、LZDP、DPFlate、Deflate、Gzip、Brotli、Zstd 及媒体编解码等）的参数调优与对比；
- Token / Huffman / 区块热力图等**算法内部状态**的可视化教学与调试；
- ADE（Adaptive Decision Engine）决策引擎的配置、训练数据与静默探索集成。

GUI 不承担核心压缩算法实现，而是作为**人机交互层**与**编排层**，将用户操作转化为对 `CompressionEngine` 的调用，并管理任务状态、日志与结果展示。

### 1.2 GUI 在压缩系统中的定位

```
┌─────────────────────────────────────────────────────────────┐
│  WebCompress GUI (PyQt6, src/gui/)                          │
│  · 文件列表 / 拖放 / 进度 / 可视化 / 主题 / ADE 配置         │
└──────────────────────────┬──────────────────────────────────┘
                           │ CompressionEngine, file_protocol
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  gui/engine/  bridge.py → core_engine / core_engine_new     │
│  (pybind: pipeline_compress, compressFile, LZDPCompressor…) │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  C++: api_new + algorithm_new + core_new (GuiCompressors)   │
│  WCX 封装、流式/内存压缩、解压                                 │
└─────────────────────────────────────────────────────────────┘
```

| 层次 | 职责 |
|------|------|
| **GUI** | 交互、任务队列、参数持久化、可视化、日志 |
| **engine** | Python 侧压缩引擎门面、WCX 协议、Token 解析 |
| **core_engine** | C++ 扩展模块，GIL 释放后的长时间压缩 |
| **algorithm_new** | 实际 LZ/DP/Huffman/流式管线 |

### 1.3 选择 PyQt6 的原因

| 考量 | 说明 |
|------|------|
| **跨平台桌面** | Windows 为主要目标，PyQt6 对 Win32 打包（PyInstaller）成熟 |
| **丰富控件** | `QTableWidget`、`QThread`、拖放、样式表（QSS）满足表格与主题需求 |
| **自定义绘制** | Token 热力图、Huffman 树等需 `QPainter` 自绘，Qt 图形栈完备 |
| **信号/槽** | 与 `QThread` 进度信号天然契合，避免界面线程阻塞 |
| **与 Python 生态一致** | ADE 训练、配置 JSON、日志均为 Python，与 C++ 仅通过 pybind 边界交互 |

### 1.4 主要功能模块

| 模块 | 路径 | 功能摘要 |
|------|------|----------|
| **文件管理** | `ui/main_window.py`、`ui/table.py` | 拖放/对话框导入、文件夹展开、勾选、状态列 |
| **压缩算法选择** | `AlgorithmSelector`、`models.AlgorithmType` | 工具栏下拉，含 Auto / 媒体 / 实验算法 |
| **算法配置** | `main_window` 内 `AlgorithmConfigDialog` | 按 `ALGORITHM_PARAMS` 动态生成 SpinBox |
| **可视化** | `ui/views/visualizers/`、`ui/dialogs/` | Token 热力图、Huffman 树、区块热力图、LZ/Flate 演示 |
| **主题配置** | `config/theme.py` | `ThemeManager` 单例、亮/暗色、QSS 生成 |
| **批量压缩** | `ui/worker.py` | `CompressionWorker` / `ComparisonWorker` |
| **解压与导出** | `main_window`、 `engine/file_protocol.py` | WCX 解压、导出目录、网络仿真 |
| **ADE** | `ade/` | 自动选算法、特征、训练样本、静默探索 |
| **网页字典** | `engine/web_dict.py` | 可选 HTML/CSS/JS 短语预处理 |
| **日志** | `utils/logging.py`、`utils/interaction_log.py` | `gui.log`、UI 交互轨迹 |

---

## 二、架构设计

### 2.1 MVC 在 GUI 中的应用

项目采用**松散的 MVC**，未使用严格框架，但职责划分清晰：

| 角色 | 实现 | 说明 |
|------|------|------|
| **Model** | `models.py` | `FileRecord`、`FolderRecord`、`AlgorithmType`、`CompressionStatus`、`ALGORITHM_PARAMS` |
| **View** | `ui/`（`main_window`、`table`、dialogs、visualizers） | QWidget 展示与用户输入 |
| **Controller** | `main_window.py` 槽函数 + `worker.py` | 响应菜单/工具栏/表格信号，启停线程 |

数据流示例（压缩）：

1. View：`MainWindow._on_compress()` 收集勾选行与 `AlgorithmSelector`；
2. Controller：构造 `CompressionWorker(tasks, algorithm)` 并连接 `progress` / `finished_row`；
3. Worker：调用 `CompressionEngine.smart_compress` 或 `smart_compress_file`，更新 `FileRecord`；
4. View：`FileTableWidget` 通过 `finished_row` 刷新状态与压缩率列。

### 2.2 主窗口结构与布局

`MainWindow`（`src/gui/ui/main_window.py`）核心组成：

```
MainWindow (QMainWindow)
├── QMenuBar          文件 / 帮助 / 高级（算法配置、ADE、主题）
├── QToolBar          添加、压缩、解压、导出、算法下拉、对比…
├── FileTableWidget   中央表格（7 列：勾选、名、大小、类型、状态、算法、率）
├── QStatusBar
│   └── StatusBarWidget   文案 + QProgressBar(0–100)
└── 若干 QDialog        配置、主题、可视化、对比报告（按需弹出）
```

- 窗口默认尺寸：900×700（`WINDOW_WIDTH` / `WINDOW_HEIGHT`）。
- 启用 `setAcceptDrops(True)`，在 `dragEnterEvent` / `dropEvent` 中接受本地文件 URL。
- 图标通过 `gui/utils/resources.py` 的 `resolve_icon_path()` 解析，兼容开发目录与 PyInstaller 打包路径。

### 2.3 模块化目录职责

```
src/gui/
├── main.py              # run_gui() / run_cli() 入口
├── models.py            # 领域模型与算法参数元数据
├── config/              # settings.json、ThemeManager
├── engine/              # C++ 桥接、压缩门面、WCX、Token、web_dict
├── ui/                  # PyQt6 界面
│   ├── main_window.py   # 主窗口与多数对话框
│   ├── worker.py        # 后台压缩/对比线程
│   ├── table.py         # 文件表格
│   ├── helpers.py       # 样式辅助
│   ├── panels/          # 信息侧栏组件
│   ├── dialogs/         # 热力图、Huffman、网络仿真等
│   └── views/visualizers/  # 自绘可视化控件
├── ade/                 # 决策引擎 Python 层
├── algorithms/          # Transformer 等纯 Python 算法
├── windows/             # HTML 报告生成（对比、热力图窗口）
└── utils/               # 日志、工作区路径、资源、交互日志
```

| 目录 | 职责边界 |
|------|----------|
| **ui** | 仅 PyQt 与用户事件，不直接 `import` C++ |
| **engine** | 所有 `core_engine` 调用、`CompressionEngine` 单例逻辑 |
| **config** | 磁盘 JSON（`Package/config/webcompress_settings.json`）与主题 |
| **ade** | 与压缩并行、可选的算法/参数推荐 |
| **utils** | 无业务状态的工具函数 |

### 2.4 关键类设计

#### `MainWindow`（`ui/main_window.py`）

- 应用主控制器：菜单、工具栏、拖放、压缩/解压/导出、打开各类可视化对话框。
- 持有 `_worker`、`_comparison_worker`，防止重复启动压缩线程。
- `refresh_theme()` 在主题变更后刷新 QSS 与表格。

#### `FileTableWidget`（`ui/table.py`）

- 继承 `QTableWidget`，列：勾选、文件名、大小、类型、状态、算法、压缩率。
- `Record_Role` 绑定 `FileRecord` / `FolderRecord` 对象。
- 右键菜单发射信号：`request_demo`、`request_heatmap`、`request_comparison`、`request_network` 等。
- 行选择与勾选列联动（`_sync_checkboxes_from_selection`）。

#### `CompressionWorker`（`ui/worker.py`）

- 继承 `QThread`，信号：`progress(int, str)`、`finished_row(object)`、`error(object)`。
- 支持 `FileRecord` 与 `FolderRecord`（递归子文件）。
- 分支逻辑：
  - **流式**：`engine.should_use_streaming()` → `smart_compress_file` 写 WCX 到 workspace；
  - **内存**：`load_raw_data` → `smart_compress`（可选 web_dict、`force_memory_codec`）；
  - **Auto**：`DecisionEngine.decide()` 选算法与参数；
  - **媒体**：JPEG/PNG/FLAC/H.264 等走 `ffmpeg` 或专用路径。
- `cancel()` 设置标志并调用 C++ `set_streaming_compress_cancel_requested`。

#### `ComparisonWorker`（`ui/worker.py`）

- 对单文件多算法跑压缩，用于对比报告（不阻塞 UI，分块更新进度）。

#### `AlgorithmSelector`（`ui/main_window.py`）

- `QComboBox` + `currentData()` 映射 `AlgorithmType`。
- 列表含 LZDP、DPFlate、LZSS、Deflate、Gzip、Brotli、Zstd、媒体编解码、Transformer、Auto。

#### `CompressionEngine`（`engine/compressor.py`）

- GUI 侧压缩**唯一推荐入口**：`smart_compress`、`smart_compress_file`、`smart_decompress`。
- 内部 `_create_compressor()` 根据 `ALGORITHM_PARAMS` 调用 `LZDPCompressor` 等 pybind 类。
- 流式阈值、分块大小从 `config/settings.py` 读取。

#### `ThemeManager`（`config/theme.py`）

- 单例，持有 `Theme` dataclass（背景/文字/边框/图表色）。
- `apply()`、`main_window_sheet()`、`table_sheet()`、`full_dialog_sheet()` 生成 QSS。

---

## 三、核心功能实现

### 3.1 文件拖放与批量处理

**拖放实现**（`MainWindow`）：

```python
def dragEnterEvent(self, event):
    if event.mimeData().hasUrls():
        event.acceptProposedAction()

def dropEvent(self, event):
    paths = [url.toLocalFile() for url in event.mimeData().urls() if url.isLocalFile()]
    self._add_paths(paths, source="drop")
```

**文件列表**：`FileTableWidget.add_record()` 插入行，展示格式化大小（`formatted_size`）、资源类型、压缩状态枚举。

**批量压缩流程**：

1. 用户勾选行 → `_on_compress()`；
2. 构建 `tasks: list[tuple[row_index, Record]]`；
3. 启动 `CompressionWorker`，逐条 `single_compress()`；
4. 成功：`compressed_data` 或 `compressed_path`（流式 WCX）；
5. 膨胀检测：`compressed_size >= original_size` 时可能标记 `AlgorithmType.NONE` 原样存储；
6. 可选写入 ADE 训练样本（`ade/training.py`）。

文件夹：`FolderRecord.ensure_files_loaded()` 后对其下每个 `FileRecord` 递归压缩。

### 3.2 多线程压缩

| 机制 | 说明 |
|------|------|
| **QThread** | `CompressionWorker.run()` 在子线程执行压缩 |
| **信号** | `progress.emit(percent, message)` 更新 `StatusBarWidget` |
| **取消** | UI 置 `_is_cancelled` + C++ 协作式取消（流式读盘间隙检查） |
| **GIL** | pybind 压缩入口使用 `py::gil_scoped_release()`，避免长时间占用 GIL |
| **禁止重入** | `_on_compress` 检查 `_worker.isRunning()` |

内存路径对大文件会 `load_raw_data()` 读入全文；超过 `streaming.threshold_mb`（默认可配置为 1MB）且存在路径时走**流式写盘**，降低 Python 堆峰值。

### 3.3 算法配置界面

- 元数据驱动：`models.ALGORITHM_PARAMS` 为每种 `AlgorithmType` 定义 `AlgorithmParamDef`（key、label、default、min、max、choices）。
- `AlgorithmConfigDialog` 动态创建 `QSpinBox` / 组合框，保存至 `CompressionEngine.set_config()` 并写入 `webcompress_settings.json`。
- Deflate 仅暴露窗口/Huffman 相关项（过滤 `match_engine` 等不适用字段）。
- LZDP 使用 `dp_top`；DPFlate 使用 `dp_sub_match_max`，与 C++ `LzdpWholeFileParams` / `DpflatePipelineParams` 对齐。

### 3.4 可视化功能

| 功能 | 文件 | 说明 |
|------|------|------|
| **压缩率对比** | `windows/comparison.py`、`main_window` 对比对话框 | 多算法柱状/表格 HTML 报告 |
| **Token 热力图** | `views/visualizers/token_heatmap.py`、`windows/token_heatmap.py` | 解析 LZ/DP token 在原文上的位置密度 |
| **Huffman 树** | `views/visualizers/huffman.py`、`dialogs/huffman_dialog.py` | 自绘树形结构、码表 |
| **区块热力图** | `dialogs/block_heatmap_dialog.py`、`dialogs/heatmap_dialog.py` | 分块压缩率着色 |
| **LZ 演示** | `dialogs/lz_demo_dialog.py` | DP 网格、逐步匹配演示 |
| **Flate 演示** | `dialogs/flate_demo_dialog.py` | 队列与 token 可视化 |
| **网页热力图** | `windows/webpage_heatmap.py` | 针对 HTML 结构的块视图 |

可视化数据来自 `engine/token_parser.py` 与 `file_protocol.compression_blob_for_visualization()`，对 WCX 载荷解包后解析。

### 3.5 主题系统

- `Theme`：dataclass 定义 20+ 颜色 token（`bg_primary`、`accent`、`chart_1`…）。
- 预设：亮色默认、暗色 `_dark_theme()`（当前 `DEFAULT_THEME` 为暗色）。
- `ThemeConfigDialog`：颜色选择器、实时预览、导入/导出 JSON。
- `ThemeManager.apply()` 后调用 `MainWindow.refresh_theme()` 全局刷新 QSS。
- 图表与自绘控件通过 `ThemeManager.color()` / `hex()` 取色，保证与表格一致。

---

## 四、技术难点与解决方案

### 4.1 界面响应性

**问题**：单文件 DP/LZ 压缩可达数秒至数分钟，若在主线程调用 C++ 会冻结界面。

**方案**：

- 所有压缩/对比在 `QThread` 中执行；
- 长阶段通过 `progress` 信号更新状态栏文案（如「正在内存压缩…」「流式压缩（分块写盘）…」）；
- C++ 侧 `set_streaming_compress_cancel_requested` + Worker `cancel()` 实现可中断流式任务。

### 4.2 大数据量表格

**问题**：数千行文件时表格刷新变慢。

**方案**：

- 表格仅在有记录变更时更新单行（`finished_row`）；
- 扩展选择时使用行级刷新而非全表重建；
- 文件夹懒加载：`ensure_files_loaded()` 按需扫描子项。

（进一步优化可考虑 `QTableView` + `QAbstractTableModel`，当前为 `QTableWidget` 直接项管理。）

### 4.3 跨平台兼容性

| 点 | 处理 |
|----|------|
| **路径** | 统一 `pathlib.Path`，`url.toLocalFile()` 处理拖放 |
| **core_engine 加载** | `bridge.py` 搜索 `build_py`、`Package/bin/core_engine`、PyInstaller `_MEIPASS` |
| **日志目录** | 开发时用 `Package/logs/gui.log`，打包后用可执行文件旁 `logs/` |
| **图标** | `resolve_icon_path()` 多候选路径 |

### 4.4 异常处理

- `main.py` 设置 `sys.excepthook`，未捕获异常写入 `gui.log`；
- Worker 内 `try/except`：`error.emit(record)`，主窗口 `QMessageBox` 提示；
- 解压失败：校验 WCX 头、`original_size` 与解压明文长度（`gui_payload_size_mismatch`）；
- C++ 异常经 pybind 转为 Python `RuntimeError`，捕获后标记 `CompressionStatus.FAILED`。

### 4.5 日志系统

| 日志 | 路径/Logger | 内容 |
|------|-------------|------|
| **GUI 主日志** | `Package/logs/gui.log`，`logging` root | 压缩、解压、引擎、ADE |
| **UI 交互** | `utils/interaction_log.py`，`log_ui` / `log_ui_flush` | 按钮、拖放、对话框打开 |
| **解压专用** | `engine/decompress_log.py` | 分支策略、payload 大小 |
| **探索** | `ade/explore_log.py` | 静默探索跳过原因 |

`RotatingFileHandler`：单文件最大 10MB，保留 5 个备份。

---

## 五、用户体验设计

### 5.1 交互流程

```
导入文件/文件夹（拖放或 Ctrl+F/D）
    → 表格展示（特征可选提取）
    → 选择算法（工具栏下拉或 Auto）
    → [可选] 高级 → 算法配置 / ADE
    → 点击压缩 → 进度条 + 状态文案
    → 完成：压缩率列更新 / 失败：错误信息
    → [可选] 导出 WCX、解压验证、可视化、算法对比
```

### 5.2 状态反馈

- **状态栏**：`StatusBarWidget` 文案 + 百分比进度条；
- **表格列**：`CompressionStatus`（等待/压缩中/完成/失败）；
- **压缩率列**：`compressed/original`，原样存储时特殊标记（warning 色）；
- **行级信号**：`row_started` 可扩展为行高亮（当前以状态列为主）。

### 5.3 快捷操作

| 类型 | 示例 |
|------|------|
| **快捷键** | Ctrl+F 添加文件、Ctrl+D 文件夹、Ctrl+Shift+C 算法配置 |
| **工具栏** | 压缩、解压、导出、对比 |
| **右键菜单** | 热力图、演示、网络仿真、决策详情 |
| **表格** | 勾选列与多选联动 |

### 5.4 可配置性

- **算法参数**：`webcompress_settings.json` → `algorithms.lzdp` 等节点；
- **流式**：`streaming.threshold_mb`、`chunk_size_kb`、按算法覆盖；
- **ADE**：`ade.explorer`、`silent_explore_enabled`、重训练阈值；
- **主题**：`config/settings.py` 持久化自定义色值；
- **网页字典**：`compression.use_web_resource_dict`。

---

## 六、测试与优化

### 6.1 功能测试

| 类别 | 方式 |
|------|------|
| **C++ 单元/集成** | `tests/test_deflate.cpp`、`test_lzdp_imdb_gui_cfg.cpp` 等 |
| **GUI 手动** | 各算法压缩 → 导出 WCX → 解压比对大小与哈希 |
| **算法矩阵** | `tests/test_algorithm_comparison.cpp` 流式 vs 内存 |
| **Python 脚本** | `tests/python/test_streaming_memory_roundtrip.py` |

GUI 依赖 `core_engine` 可用（`CompressionEngine.available`）；不可用时入口提示并拒绝压缩。

### 6.2 性能测试

- **大文件**：>1MB 走 `smart_compress_file`，chunk 默认 512KB（可配置）；
- **多文件**：Worker 顺序处理，避免并行多线程同时占满 CPU/磁盘；
- **对比模式**：`ComparisonWorker` 按块 emit 进度，避免长时间无响应。

### 6.3 边界测试

| 场景 | 行为 |
|------|------|
| **空文件** | 压缩结果可能极短或走膨胀回退 |
| **超大文件** | 流式 + 取消；内存路径受 RAM 限制 |
| **已压缩 .wcx** | 识别 `ResourceType.COMPRESSED`，解压后展示 |
| **媒体文件** | 扩展名映射到 JPEG/H.264 等，与通用 LZ 算法隔离 |
| **取消** | 流式协作取消；内存路径依赖 GIL 释放后尽快结束 |

### 6.4 内存管理

| 策略 | 说明 |
|------|------|
| **流式 WCX** | `compressed_path` 指向 workspace，`compressed_data=None` |
| **内存 WCX** | `bytes` 存于 `compressed_data`，大文件慎用 |
| **压缩后释放** | `record.raw_data` 在适当时机可仅保留快照用于可视化 |
| **训练样本** | ADE `TrainingDataStore` 增量写 JSONL，非全量驻留 |

---

## 七、总结与展望

### 7.1 项目收获

- **PyQt6 实践**：信号/槽、QThread、QSS 主题、自绘控件、拖放与模态对话框协作；
- **分层架构**：UI 与 `CompressionEngine`、C++ 边界清晰，便于替换 `core_engine` / `core_engine_new`；
- **元数据驱动配置**：`ALGORITHM_PARAMS` 减少新增算法时的 UI 重复代码；
- **问题定位**：`gui.log` + 解压分支日志 + C++ stderr 联动，可复盘压缩路径（内存/流式/参数）。

### 7.2 不足之处与已知问题

| 问题 | 说明 |
|------|------|
| **参数接线** | 部分 `GuiCompressors` setter 曾为空实现，导致 GUI 调参不生效（需与 pyd 同步重编） |
| **算法栈迁移** | `algorithm_new` 与旧版 `algorithm` 行为差异，Deflate/DPFlate 排名可能与历史版本不一致 |
| **单线程批量** | 多文件顺序压缩，未利用多核并行 |
| **表格扩展性** | 万级行时 `QTableWidget` 可能卡顿 |
| **core_engine 部署** | 打包需随附 `libgcc/libstdc++` 与 pyd，路径敏感 |
| **国际化** | 界面以中文为主，无 i18n 框架 |

### 7.3 改进方向

1. **可视化**：更多算法（Brotli/Zstd）内部阶段展示；压缩前后字节分布直方图。  
2. **格式**：支持目录 WCX、批量 CLI 与 GUI 配置共享。  
3. **性能**：大文件默认流式；可选多文件并行 Worker 池；表格虚拟化。  
4. **引导**：首次运行向导、参数说明 tooltip 链接设计文档。  
5. **国际化**：Qt Linguist / 字符串外置。  
6. **质量**：GUI 自动化测试（pytest-qt）、压缩黄金文件回归与 CI 联动。  
7. **参数一致性**：内存路径与 `pipeline_compress_file` 共用同一套 `*_from_params` 快照，避免流式/内存语义分裂。

---

## 附录：入口与运行

```bash
# 开发环境（需已编译 core_engine / core_engine_new）
cd src
python -m gui

# CLI 批量（无界面）
python -m gui /path/to/directory

# 日志位置
Package/logs/gui.log
```

配置文件：`Package/config/webcompress_settings.json`  
工作区（流式 WCX 临时文件）：`Package/workspace/`（由 `utils/workspace.py` 管理）

---

*本报告依据 `src/gui/` 当前源码整理，若模块路径有重构请以 [gui_architecture.md](./gui_architecture.md) 为准。*
