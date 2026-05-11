# GUI 模块架构分析与重组建议

> 基于 `src/gui/` 目录下全部 Python 源码的静态分析  
> 分析日期：2026-05-11

> **与仓库同步**：**§一**为现行 `src/gui/` 目录树（仅 `.py` 源文件，不含 `__pycache__`）。**§2.1–2.2** 表与图为**重组前**包结构的职责与依赖示意，用于对照 §3 的拆分理由与已落实项；**现行**模块边界以 §一为准。压缩演示对话框落位见 **§3.4.2**。

---

## 一、现行目录结构（`src/gui/`）

```
src/gui/
├── __init__.py
├── __main__.py                  # 入口：python -m gui
├── main.py                      # run_gui() / run_cli()
├── models.py                    # FileRecord, AlgorithmType, 常量等
│
├── config/                      # 主题与 JSON 配置
│   ├── __init__.py
│   ├── settings.py              # 持久化配置
│   └── theme.py                 # Theme / ThemeManager
│
├── engine/                      # C++ core_engine 封装与协议
│   ├── __init__.py
│   ├── bridge.py
│   ├── compressor.py            # CompressionEngine
│   ├── file_protocol.py         # .wcx 等
│   └── token_parser.py          # LZSS/LZDP/Deflate Token
│
├── ade/                         # ADE Python：决策、特征、训练、探索
│   ├── __init__.py
│   ├── engine.py                # DecisionEngine 等
│   ├── features.py
│   ├── training.py
│   ├── explorer.py
│   ├── params.py
│   └── types.py
│
├── algorithms/                  # 纯 Python 可选算法
│   ├── __init__.py
│   └── transformer_compressor.py
│
├── ui/                          # PyQt6：主窗格、视图、面板、对话框
│   ├── __init__.py
│   ├── main_window.py
│   ├── worker.py
│   ├── table.py
│   ├── helpers.py               # 样式标签、编码预览等
│   ├── visualization_windows.py  # 兼容用聚合 re-export（见 §3.4.2）
│   ├── common/
│   │   └── __init__.py
│   ├── views/
│   │   ├── __init__.py
│   │   ├── dashboard_view.py
│   │   ├── compression_view.py
│   │   ├── analysis_view.py
│   │   ├── comparison_view.py
│   │   ├── network_view.py
│   │   └── visualizers/
│   │       ├── __init__.py
│   │       ├── block.py
│   │       ├── huffman.py
│   │       └── token_heatmap.py
│   ├── panels/
│   │   ├── __init__.py
│   │   ├── resource_tree.py
│   │   ├── property_panel.py
│   │   └── info_panel.py
│   └── dialogs/
│       ├── __init__.py
│       ├── heatmap_dialog.py
│       ├── huffman_dialog.py
│       ├── lz_demo_dialog.py
│       ├── block_heatmap_dialog.py
│       ├── network_sim_dialog.py
│       ├── flate_demo_dialog.py
│       └── comparison_dialog.py
│
├── windows/                     # HTML 报告 / 独立窗口生成
│   ├── __init__.py
│   ├── heatmap.py
│   ├── token_heatmap.py
│   ├── comparison.py
│   ├── network_sim.py
│   └── webpage_heatmap.py
│
├── utils/                       # 日志、资源路径、文件扫描
│   ├── __init__.py
│   ├── logging.py
│   ├── resources.py
│   └── file_helper.py
│
└── io/                          # 仅占位包（扫描逻辑在 utils/file_helper）
    └── __init__.py
```

> **说明**：仓库中若仍存在 `core/`、`domain/` 等目录下的 **仅 `__pycache__`** 或历史残留，不应再新增业务代码；权威源以本节所列 `.py` 为准。

---

## 二、架构分析（重组前动因 + 现行对照）

### 2.1 模块职责总览（重组前对照）

| 模块 | 职责 | 行数估算 | 依赖方向 |
|------|------|---------|---------|
| `domain/` | 数据模型、枚举、常量 | ~330 | 无（被所有模块依赖） |
| `core/` | 引擎封装、配置、主题、协议、解析、决策 | ~1800 | 依赖 `domain/` |
| `io/` | 文件扫描与加载 | ~70 | 依赖 `domain/` |
| `ade/` | 特征提取、训练数据、探索策略 | ~900 | 依赖 `domain/`, `core/` |
| `algorithms/` | 纯 Python 算法 | ~300 | 依赖 `domain/` |
| `widgets/` | PyQt6 界面组件 | ~2500 | 依赖 `domain/`, `core/`, `ade/` |
| `windows/` | 独立窗口（HTML 生成） | ~800 | 依赖 `domain/`, `core/` |
| `web/` | (重复) | ~170 | 与 `windows/network_sim.py` 重复 |
| `utils/` | (空) | 0 | - |

> 注：上表描述**迁移前**包划分及典型问题来源。**现行**对应关系：`domain/`→`models.py`；`core/`→`engine/` + `config/` + `ade/engine.py` 等；`widgets/`→`ui/`；`io/file_helper`→`utils/file_helper.py`；`web/` 已删除重复项。

### 2.2 依赖关系图（重组前示意）

```
domain/models.py  ←──  core/  (所有文件)
     ↑                    │
     │                    ├── app_config.py ←── theme.py
     │                    ├── compression_engine.py ←── app_config.py
     │                    ├── decision_engine.py ←── compression_engine.py
     │                    ├── file_protocol.py
     │                    └── token_parser.py ←── compression_engine.py
     │                    │
     ├── io/file_helper.py
     │
     ├── ade/
     │   ├── feature_extractor.py
     │   ├── training_store.py ←── feature_extractor.py
     │   └── explorer.py ←── training_store.py
     │
     ├── algorithms/transformer_compressor.py
     │
     ├── widgets/  (所有文件)
     │   ├── main_window.py ←── core/, ade/
     │   ├── views/* ←── panels/*
     │   ├── panels/* ←── core/theme.py
     │   └── common/* ←── core/theme.py
     │
     └── windows/* ←── core/, domain/
```

### 2.3 现有问题（重组前动因；与 §一对照可见已解决项）

1. **`core/` 过重** — 同时承载引擎封装、配置、协议、解析、决策，职责过多（**已缓解**：`engine/` + `config/` + `ade/`）
2. **`decision_engine.py` 混合了决策逻辑 + ParameterRegressor** — 后者应独立（**已缓解**：`ade/engine.py`、`ade/params.py` 等）
3. **`widgets/main_window.py` 过重** — 包含 CompressionWorker、FileTableWidget、主窗口逻辑，建议拆分（**已缓解**：`ui/worker.py`、`ui/table.py`）
4. **`web/network_sim.py` 与 `windows/network_sim.py` 完全重复** — 代码重复，应合并（**已解决**：以 `windows/` 为准）
5. **`widgets/visualization_windows.py` 命名冲突** — 与 `windows/` 包名混淆（**已缓解**：逻辑迁至 `ui/dialogs/`，`ui/visualization_windows.py` 提供聚合兼容 re-export）
6. **`utils/` 为空** — 未发挥作用（**已缓解**：`gui/utils/` 承接 `file_helper`、`logging`、`resources`）
7. **辅助文件命名不规范** — `huffman_visualizer.py` / `block_visualizer.py` / `encoding_preview.py` / `label_utils.py` 使用了 `_visualizer`、`_preview`、`_utils` 四种不同后缀（**已缓解**：`views/visualizers/*.py`、`ui/helpers.py`）
8. **文件粒度过细** — `label_utils.py`（55行）和 `encoding_preview.py`（92行）规模太小，单独成文件增加导航成本（**已缓解**：合并入 `ui/helpers.py`）
9. **`huffman_visualizer.py` / `block_visualizer.py` 错放目录** — 本质是 `analysis_view.py` 的可视化组件，却放在 `panels/` 下（**已缓解**：迁至 `ui/views/visualizers/`）
10. **`io/` 包过轻** — 仅一个 73 行的 `file_helper.py`，不应独占一个包，应归入 `utils/`（**已缓解**：`utils/file_helper.py`；`io/` 仅占位）

---

## 三、重组建议

### 3.1 目标原则

- **单一职责**：每个模块/文件只做一件事
- **显式依赖**：减少循环依赖，依赖方向清晰
- **消除重复**：合并重复代码
- **命名清晰**：包名反映实际用途

### 3.2 建议目录结构

```
src/gui/
├── __init__.py
├── __main__.py
├── main.py
│
├── models.py                    # [原 domain/models.py] 数据模型、枚举、常量
│                               # FileRecord, FolderRecord, AlgorithmType, ALGORITHM_PARAMS
│
├── engine/                      # [原 core/ 拆分] 引擎层
│   ├── __init__.py
│   ├── bridge.py                # C++ core_engine 加载与封装
│   ├── compressor.py            # CompressionEngine (配置管理 + 压缩调度)
│   ├── file_protocol.py         # .wcx 格式打包/解包 (不变)
│   └── token_parser.py          # Token 解析器 (不变)
│
├── config/                      # [原 core/ 拆分] 配置管理
│   ├── __init__.py
│   ├── settings.py              # app_config.py (JSON 持久化)
│   └── theme.py                 # 主题系统 (不变)
│
├── ade/                         # [原 core/ + ade/ 合并] 算法决策引擎
│   ├── __init__.py
│   ├── engine.py                # DecisionEngine (原 decision_engine.py 决策部分)
│   ├── features.py              # 20 维特征提取 (原 feature_extractor.py)
│   ├── params.py                # ParameterRegressor (原 decision_engine.py 参数回归部分)
│   ├── training.py              # 训练数据收集 (原 training_store.py)
│   └── explorer.py              # SilentExplorer (不变)
│
├── algorithms/                  # 纯 Python 算法 (不变)
│   ├── __init__.py
│   └── transformer_compressor.py
│
├── ui/                          # [原 widgets/] 界面组件
│   ├── __init__.py
│   ├── main_window.py           # 主窗口 (仅窗口布局)
│   ├── worker.py                # CompressionWorker (从 main_window.py 拆分)
│   ├── table.py                 # FileTableWidget (从 main_window.py 拆分)
│   │
│   ├── views/                   # 页面视图（现行文件：`dashboard_view.py` 等 `*_view.py`）
│   │   ├── __init__.py
│   │   ├── dashboard_view.py
│   │   ├── compression_view.py
│   │   ├── analysis_view.py
│   │   ├── comparison_view.py
│   │   ├── network_view.py
│   │   │
│   │   └── visualizers/         # [从 panels/ 移入] 视图层可视化组件
│   │       ├── __init__.py
│   │       ├── huffman.py       # HuffmanTreeWidget
│   │       └── block.py         # BlockProfilerWidget
│   │
│   ├── panels/                  # 面板组件 (仅保留布局面板)
│   │   ├── __init__.py
│   │   ├── resource_tree.py
│   │   ├── property_panel.py
│   │   └── info_panel.py
│   │
│   ├── dialogs/                 # 模态对话框（已从 visualization_windows 等拆分）
│   │   ├── __init__.py
│   │   ├── heatmap_dialog.py    # HeatmapDialog
│   │   ├── huffman_dialog.py    # HuffmanTreeDialog
│   │   ├── lz_demo_dialog.py    # LZDP / LZSS 步进演示
│   │   ├── block_heatmap_dialog.py
│   │   ├── network_sim_dialog.py
│   │   ├── flate_demo_dialog.py # Flate 两层 + Huffman 建树动画
│   │   ├── comparison_dialog.py
│   │   ├── algo_config.py       # （规划中）算法配置对话框
│   │   └── about.py             # （规划中）
│   │
│   └── helpers.py               # [合并] 视图层小工具函数
│                               # create_styled_label() + format_lzdp_preview()
│
├── windows/                     # [原 windows/] 独立窗口（HTML 生成）
│   ├── __init__.py
│   ├── heatmap.py               # 压缩热力图
│   ├── token_heatmap.py         # Token 级热力图
│   ├── comparison.py            # 算法对比
│   ├── network_sim.py           # 网络传输模拟（合并 web/ 版本）
│   └── webpage_heatmap.py       # 网页资源热力图
│
└── utils/                       # 通用工具
    ├── __init__.py
    ├── logging.py               # 日志设置 (从 main.py 拆分)
    ├── resources.py             # 资源路径解析 (从 main.py 拆分)
    └── file_helper.py           # 文件扫描与加载 (原 io/file_helper.py)
```

### 3.3 关键变更说明

#### 3.3.1 `models.py` — 领域模型保留单文件

`domain/models.py` 保持单文件，不拆分：
- 所有内容均为数据结构和变量定义（`FileRecord`、`FolderRecord`、`AlgorithmType`、`ALGORITHM_PARAMS` 等），属于同一类规范
- 332 行的规模在合理范围内，拆分反而增加跨文件导航成本
- 仅将文件从 `domain/` 包提升到 `gui/` 根目录，缩短 import 路径

**理由**：数据模型、枚举、常量三者逻辑内聚，都是"类型定义"范畴，拆分不符合"同一规范保留在一起"的原则。

#### 3.3.2 `engine/` + `config/` — 核心层拆分

将 `core/` 拆分为两个子包：
- `engine/`：与 C++ 引擎直接交互的部分（bridge、compressor、protocol、parser）
- `config/`：配置持久化和主题管理

**理由**：`core/` 当前承载 6 个文件约 1800 行，涵盖引擎封装、配置、协议、解析四大职责，拆分后每个子包职责单一。

#### 3.3.3 `ade/` — 决策系统整合（原 `core/` + `ade/`）

将 `ade/` 与 `core/decision_engine.py` 合并为统一的 `ade/` 包：
- `engine.py`：`DecisionEngine`（决策调度）
- `features.py`：`FeatureExtractor` + `BaseFeatures`（特征提取）
- `params.py`：`ParameterRegressor`（参数回归，从 decision_engine.py 拆分）
- `training.py`：`TrainingSampleV3` + `TrainingDataStore`（训练数据）
- `explorer.py`：`SilentExplorer`（探索策略）

**理由**：
- `DecisionEngine` 本身就是 ADE 的核心组件，不应独立成包
- `decision_engine.py` 混合了决策逻辑和 ParameterRegressor，后者应独立
- ADE 相关代码分散在 `core/` 和 `ade/` 两个包中，逻辑上应合并
- 决策系统是一个完整的子系统，统一归入 `ade/` 更合理

#### 3.3.4 `ui/` — 界面层重组

将 `widgets/` 重命名为 `ui/`，并拆分 `main_window.py`：
- `main_window.py`：仅保留主窗口布局和信号连接
- `worker.py`：`CompressionWorker`（压缩工作线程）
- `table.py`：`FileTableWidget`（文件表格组件）
- `dialogs/`：新增对话框目录，从 `visualization_windows.py` 拆分
- `views/visualizers/`：从 `panels/` 移入可视化组件，文件名简化为 `huffman.py` / `block.py`
- `helpers.py`：合并 `label_utils.py`（55行）和 `encoding_preview.py`（92行）为单一文件，避免过小文件拆分

**理由**：
- `main_window.py` 当前约 800+ 行，混合了主窗口、工作线程、表格组件三大职责
- `huffman_visualizer.py` 和 `block_visualizer.py` 本质上是 `analysis_view.py` 的可视化组件，而非独立面板
- `label_utils.py`（55行）和 `encoding_preview.py`（92行）规模太小，单独成文件反而增加导航成本，合并为 `helpers.py` 更合理
- 文件名统一使用单名词简写：`huffman.py` / `block.py`，消除 `_visualizer` / `_tree` / `_profiler` 等不一致后缀

#### 3.3.5 `windows/` — 独立窗口（保留原名）

`windows/` 保留原名，不做重命名：
- 这些 HTML 生成器本质上是"独立窗口"，只是受限于技术能力才用 HTML 渲染，不是传统意义上的"报告"
- 合并 `web/network_sim.py` 到 `windows/network_sim.py`，消除重复

**理由**：
- `windows/` 包名准确反映了"独立窗口"的语义，HTML 只是实现手段
- `web/network_sim.py` 与 `windows/network_sim.py` 曾完全重复（合并前 `web/` 侧 import 与主分支不一致的情况已随 `web/` 删除消除；现行见 `gui/windows/network_sim.py`）

#### 3.3.6 `utils/` — 通用工具包

将分散的辅助工具归入 `utils/`：
- `logging.py`：日志设置函数（从 `main.py` 拆分）
- `resources.py`：图标路径解析等资源工具（从 `main.py` 拆分）
- `file_helper.py`：文件扫描与加载（原 `io/file_helper.py`，仅73行，本质是辅助工具，不应独占 `io/` 包）

### 3.4 文件拆分原则

针对当前代码中辅助性文件命名不规范、粒度过细的问题，制定以下拆分原则：

| 原则 | 说明 | 示例 |
|------|------|------|
| **≥150行独立成文件** | 低于此阈值的代码应合并到同层级的通用文件中 | `label_utils.py`（55行）→ 合并入 `helpers.py` |
| **类定义独立成文件** | 包含完整 PyQt6 组件类（≥100行）的可独立 | `HuffmanTreeWidget`（~200行）→ `huffman.py` |
| **纯函数工具合并** | 多个小工具函数合并为一个文件，按功能域分组 | `create_styled_label()` + `format_lzdp_preview()` → `helpers.py` |
| **文件名用单名词** | 避免 `_utils`、`_helper`、`_preview`、`_visualizer` 等冗余后缀 | `huffman.py` 而非 `huffman_visualizer.py` |
| **按使用者归置** | 工具代码归入离其使用者最近的层级 | `huffman.py` 归入 `views/visualizers/` 而非 `panels/` |

#### 3.4.1 当前辅助文件评估

| 文件 | 行数 | 性质 | 决策 |
|------|------|------|------|
| `widgets/common/label_utils.py` | 55 | 纯函数 | ❌ 合并 → `ui/helpers.py` |
| `widgets/algo_config/encoding_preview.py` | 92 | 纯函数 | ❌ 合并 → `ui/helpers.py` |
| `widgets/panels/huffman_visualizer.py` | ~200 | PyQt6 组件类 | ✅ 独立 → `ui/views/visualizers/huffman.py` |
| `widgets/panels/block_visualizer.py` | ~200 | PyQt6 组件类 | ✅ 独立 → `ui/views/visualizers/block.py` |
| `widgets/visualization_windows.py` | ~400 | 多组件混合 | ✅ 已拆 → 见 **§3.4.2**（`ui/dialogs/*` + `token_heatmap.py` + `huffman.py` + `visualization_windows` 兼容 re-export） |

#### 3.4.2 已实现：`ui/dialogs/` 与热力图 / Huffman 组件落位

以下与当前 `src/gui/ui/` 一致；新代码请 **优先** `from gui.ui.dialogs.<模块> import …`。`visualization_windows.py` 仍提供 **全量** 兼容 re-export（含 `HeatmapDialog` / `HuffmanTreeDialog`），便于旧脚本一行迁移。

| 路径 | 主要符号 | 说明 |
|------|----------|------|
| `ui/dialogs/heatmap_dialog.py` | `HeatmapDialog` | Token 文本热力图对话框 |
| `ui/dialogs/huffman_dialog.py` | `HuffmanTreeDialog` | Huffman 树 + 频率 + 码表对话框 |
| `ui/dialogs/lz_demo_dialog.py` | `DPArrayBar`, `LZDPDPSliderWidget`, `LZDPDPDialog`, `LZSliderWidget`, `LZSliderDialog` | LZ / LZDP 步进演示 |
| `ui/dialogs/block_heatmap_dialog.py` | `BlockHeatmapCanvas`, `BlockHeatmapDialog` | 按块压缩率热力图 |
| `ui/dialogs/network_sim_dialog.py` | `NetworkSimDialog` | 网络传输模拟（Qt 对话框） |
| `ui/dialogs/flate_demo_dialog.py` | `FlateDemoDialog`, `HuffmanBuildAnimator`（及内部 `_BuildStep`、`_ZoomableTreeCanvas` 等） | DEFLATE 两层演示 |
| `ui/dialogs/comparison_dialog.py` | `ComparisonDialog` | 多算法压缩率对比 |
| `ui/views/visualizers/token_heatmap.py` | `TokenHeatmapWidget`, `TokenInfoPanel`, `ratio_to_qcolor` | Token 高亮与配色 |
| `ui/views/visualizers/huffman.py` | `HuffmanTreeWidget`, `HuffmanTreePanel`, `HuffmanTreeCanvas`, `HuffmanFreqChart`, `code_length_to_color` | 块内码长树 + 独立画布/频图 |
| `ui/visualization_windows.py` | 上表全部对话框类的 **re-export**（聚合 import） | 兼容旧 import；新代码优先子模块 |

---

## 四、文件映射表

| 当前路径 | 建议路径 | 变更类型 |
|---------|---------|---------|
| `domain/models.py` | `models.py` | 提升到根目录 |
| `core/app_config.py` | `config/settings.py` | 移动 |
| `core/theme.py` | `config/theme.py` | 移动 |
| `core/compression_engine.py` | `engine/bridge.py` + `engine/compressor.py` | 拆分 |
| `core/file_protocol.py` | `engine/file_protocol.py` | 移动 |
| `core/token_parser.py` | `engine/token_parser.py` | 移动 |
| `core/decision_engine.py` | `ade/engine.py` + `ade/params.py` | 拆分+移动 |
| `ade/feature_extractor.py` | `ade/features.py` | 移动 |
| `ade/training_store.py` | `ade/training.py` | 移动 |
| `ade/explorer.py` | `ade/explorer.py` | 保留 |
| `widgets/main_window.py` | `ui/main_window.py` + `ui/worker.py` + `ui/table.py` | 拆分+重命名 |
| `widgets/visualization_windows.py` | `ui/dialogs/heatmap_dialog.py`、`huffman_dialog.py`、`lz_demo_dialog.py`、`block_heatmap_dialog.py`、`network_sim_dialog.py`、`flate_demo_dialog.py`、`comparison_dialog.py`；`ui/views/visualizers/token_heatmap.py`、`huffman.py`；`ui/visualization_windows.py`（聚合 re-export） | 拆分+移动（已实现） |
| `widgets/panels/huffman_visualizer.py` | `ui/views/visualizers/huffman.py` | 移动+重命名 |
| `widgets/panels/block_visualizer.py` | `ui/views/visualizers/block.py` | 移动+重命名 |
| `widgets/views/` | `ui/views/` | 重命名 |
| `widgets/panels/` | `ui/panels/` | 重命名（仅保留布局面板） |
| `widgets/common/label_utils.py` | `ui/helpers.py`（合并） | 合并 |
| `widgets/algo_config/encoding_preview.py` | `ui/helpers.py`（合并） | 合并 |
| `windows/` | `windows/` | 保留（合并 web/） |
| `web/network_sim.py` | 删除（合并到 `windows/network_sim.py`） | 删除 |
| `io/file_helper.py` | `utils/file_helper.py` | 移动 |
| `utils/` | `utils/logging.py` + `utils/resources.py` + `utils/file_helper.py` | 填充 |
| `widgets/legacy/main_window_legacy_backup.py` | 删除（重构完成后的历史备份，不再需要） | 删除 |

---

## 五、重组后模块依赖关系

```
models.py  ←──  engine/  ←──  config/
   ↑            ↑              ↑
   ├── utils/   ├── ade/       └── ui/
   ├── algorithms/                  │
   └── windows/                     ├── views/
                                    ├── panels/
                                    ├── dialogs/
                                    └── helpers.py/
```

- `models.py`：零依赖，被所有模块依赖
- `engine/`：仅依赖 `models.py`
- `config/`：仅依赖 `models.py`
- `ade/`：依赖 `models.py`, `engine/`, `config/`
- `utils/`：依赖 `models.py`
- `algorithms/`：依赖 `models.py`
- `windows/`：依赖 `models.py`, `engine/`
- `ui/`：依赖 `models.py`, `engine/`, `config/`, `ade/`, `utils/`

**无循环依赖**，依赖方向自底向上清晰。

### 5.1 重组后 `ui/` 内部依赖关系

```
ui/
├── main_window.py  ←──  worker.py, table.py, views/*
├── worker.py       ←──  engine/, ade/
├── table.py        ←──  models/
│
├── views/          ←──  panels/, views/visualizers/, dialogs/, helpers.py
│   ├── analysis_view.py ←──  visualizers/huffman.py, visualizers/block.py
│   ├── dashboard_view.py
│   ├── compression_view.py
│   ├── comparison_view.py
│   └── network_view.py
│
├── views/visualizers/  ←──  models/, engine/ (仅被 views/ 引用)
├── panels/             ←──  models/, config/theme.py (仅被 views/ 引用)
├── dialogs/            ←──  models/, engine/
└── helpers.py          ←──  config/theme.py (仅被 views/ 引用)
```

---

## 六、实施建议

### 6.1 实施顺序

1. **Phase 1** — 基础设施重组（无功能变更）
   - 提升 `domain/models.py` → `models.py`（根目录）
   - 创建 `config/` 包，移动 `app_config.py` 和 `theme.py`
   - 创建 `engine/` 包，移动 `file_protocol.py` 和 `token_parser.py`
   - 移动 `io/file_helper.py` → `utils/file_helper.py`
   - 更新所有 import 路径

2. **Phase 2** — ADE 决策系统整合
   - 创建 `ade/` 包（整合原 `core/` 和 `ade/`）
   - 从 `decision_engine.py` 拆分 `ParameterRegressor` 到 `ade/params.py`
   - 移动 `ade/` 下文件到统一 `ade/` 包
   - 合并重复的 `web/network_sim.py`

3. **Phase 3** — UI 层重组
   - 重命名 `widgets/` → `ui/`
   - 拆分 `main_window.py`
   - 合并 `label_utils.py` + `encoding_preview.py` → `ui/helpers.py`
   - 移动 `huffman_visualizer.py` → `ui/views/visualizers/huffman.py`
   - 移动 `block_visualizer.py` → `ui/views/visualizers/block.py`
   - 合并 `web/network_sim.py` → `windows/network_sim.py`
   - 拆分 `visualization_windows.py` → `ui/dialogs/*.py` + `token_heatmap.py` 等（**已完成**，见 §3.4.2）

4. **Phase 4** — 清理
   - 删除 `web/` 目录（`network_sim.py` 已合并到 `windows/`）
   - 删除 `io/` 目录（`file_helper.py` 已移至 `utils/`）
   - 删除 `domain/` 目录（`models.py` 已提升到根目录）
   - 删除 `widgets/common/` 目录（`label_utils.py` 已合并到 `ui/helpers.py`）
   - 删除 `widgets/algo_config/` 目录（`encoding_preview.py` 已合并到 `ui/helpers.py`）
   - 删除 `widgets/legacy/` 目录（备份文件处理见下文）
   - 更新所有 `__init__.py` 导出
   - 更新 `__main__.py` 和 `main.py` 的 import

### 6.2 风险提示

- **import 路径变更**：所有引用 `gui.domain`、`gui.core`、历史 `gui.widgets` 的外部代码需同步为 `gui.models`、`gui.engine` / `gui.config`、`gui.ui` 等当前包名
- **git 历史丢失**：文件移动后 git 历史可能断裂，建议使用 `git mv` 而非直接复制
- **并行开发冲突**：重组期间其他分支的代码合并可能产生冲突
- **测试覆盖**：重组后需验证所有 import 路径正确，建议先确保测试覆盖

---

## 七、附录：文件详细职责（当前 → 建议路径对照）

| 当前路径 | 建议路径 | 行数 | 核心类/函数 | 职责 |
|---------|---------|------|------------|------|
| `main.py` | `main.py` | 127 | `run_gui()`, `run_cli()` | 应用入口，GUI/CLI 模式切换 |
| `__main__.py` | `__main__.py` | 12 | - | `python -m gui` 入口 |
| `domain/models.py` | `models.py` | 332 | `FileRecord`, `FolderRecord`, `AlgorithmType` | 数据模型、枚举、常量 |
| `core/app_config.py` | `config/settings.py` | 145 | `load_config()`, `save_config()`, `apply_theme()` | JSON 配置持久化 |
| `core/compression_engine.py` | `engine/bridge.py` + `engine/compressor.py` | ~250 | `CompressionEngine` | C++ 引擎加载、压缩调度 |
| `core/decision_engine.py` | `ade/engine.py` + `ade/params.py` | ~800 | `DecisionEngine`, `ParameterRegressor` | 算法决策 + 参数回归 |
| `core/file_protocol.py` | `engine/file_protocol.py` | ~250 | `CompressedFileHeader`, `pack_compressed_file()` | .wcx 格式打包/解包 |
| `core/theme.py` | `config/theme.py` | ~200 | `Theme`, `ThemeManager` | 主题定义与切换 |
| `core/token_parser.py` | `engine/token_parser.py` | ~500 | `LZSSTokenParser`, `LZDPTokenParser`, `DeflateTokenParser` | Token 解析 |
| `io/file_helper.py` | `utils/file_helper.py` | 73 | `scan_directory()`, `load_batch()` | 文件扫描与加载 |
| `ade/feature_extractor.py` | `ade/features.py` | ~500 | `FeatureExtractor`, `BaseFeatures` | 20 维特征提取 |
| `ade/training_store.py` | `ade/training.py` | ~250 | `TrainingSampleV3`, `TrainingDataStore` | 训练数据收集 |
| `ade/explorer.py` | `ade/explorer.py` | ~200 | `SilentExplorer` | AC-UCB 探索策略 |
| `algorithms/transformer_compressor.py` | `algorithms/transformer_compressor.py` | ~300 | `TransformerCompressor` | Transformer 压缩 |
| `widgets/main_window.py` | `ui/main_window.py` + `ui/worker.py` + `ui/table.py` | ~800 | `MainWindow`, `CompressionWorker`, `FileTableWidget` | 主窗口 + 工作线程 + 表格 |
| `widgets/visualization_windows.py` | `ui/dialogs/`（多文件，§3.4.2）<br>`ui/views/visualizers/token_heatmap.py`<br>`ui/views/visualizers/huffman.py`<br>`ui/visualization_windows.py`（聚合 re-export） | ~400（原单文件） | 热力图、Huffman、LZ/Flate 演示、区块热力图、网络模拟、对比等 | 可视化组件（已拆分） |
| `widgets/views/dashboard_view.py` | `ui/views/dashboard_view.py` | ~200 | `DashboardView` | 概览仪表盘 |
| `widgets/views/compression_view.py` | `ui/views/compression_view.py` | 140 | `CompressPage` | 压缩页面 |
| `widgets/views/analysis_view.py` | `ui/views/analysis_view.py` | 180 | `AnalysisView` | 压缩分析 |
| `widgets/views/comparison_view.py` | `ui/views/comparison_view.py` | ~200 | `ComparisonView`, `ComparisonWorker` | 工具对比 |
| `widgets/views/network_view.py` | `ui/views/network_view.py` | 149 | `NetworkView` | 网络模拟 |
| `widgets/panels/resource_tree.py` | `ui/panels/resource_tree.py` | ~200 | `ResourceTree` | 资源树 |
| `widgets/panels/property_panel.py` | `ui/panels/property_panel.py` | 155 | `PropertyPanel` | 属性面板 |
| `widgets/panels/info_panel.py` | `ui/panels/info_panel.py` | 142 | `InfoPanel` | 信息面板 |
| `widgets/panels/huffman_visualizer.py` | `ui/views/visualizers/huffman.py` | ~200 | `HuffmanTreeWidget` | Huffman 树可视化 |
| `widgets/panels/block_visualizer.py` | `ui/views/visualizers/block.py` | ~200 | `BlockProfilerWidget` | Block 柱状图 |
| `widgets/common/label_utils.py` | `ui/helpers.py`（合并） | 55 | `create_styled_label()` | 标签工具 |
| `widgets/algo_config/encoding_preview.py` | `ui/helpers.py`（合并） | 92 | `format_lzdp_preview()` | 编码预览 |
| `windows/heatmap.py` | `windows/heatmap.py` | 162 | `generate_heatmap()` | 热力图 HTML |
| `windows/token_heatmap.py` | `windows/token_heatmap.py` | ~300 | `generate_token_heatmap()` | Token 热力图 HTML |
| `windows/comparison.py` | `windows/comparison.py` | 105 | `generate_comparison()` | 对比 HTML |
| `windows/network_sim.py` | `windows/network_sim.py` | 168 | `generate_network_sim()` | 网络模拟 HTML |
| `windows/webpage_heatmap.py` | `windows/webpage_heatmap.py` | ~200 | `generate_webpage_heatmap()` | 网页热力图 HTML |
| `web/network_sim.py` | ❌ 删除（合并到 `windows/network_sim.py`） | 168 | `generate_network_sim()` | (重复) 网络模拟 HTML |
| `widgets/legacy/main_window_legacy_backup.py` | ❌ 删除（历史备份） | - | - | 重构前的旧版备份 |