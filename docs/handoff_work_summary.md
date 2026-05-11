# 工作交接：compression-tool（截至 2026-05-11）

本文档汇总近期 GUI / 核心引擎相关改动、**文件架构迁移说明**，供后续维护与发布使用。权威 GUI 目录树仍以 `docs/gui_architecture.md` §一为准。

---

## 一、工作总览（按主题）

### 1. GUI 稳定性与可运行性

| 主题 | 说明 | 主要涉及路径 |
|------|------|----------------|
| `ThemeManager` 未导入 | `FileTableWidget` 初始化时报 `NameError`，已补全 `ThemeManager` 导入 | `src/gui/ui/table.py` |
| `os` 未导入 | 表格内路径类型判断使用 `os.path` 导致崩溃，已补 `import os` | `src/gui/ui/table.py` |
| 压缩率列不显示 | `update_row` 中误用不存在的 `_compressed_size`，改为按 `record.compressed_data` 长度计算 | `src/gui/ui/table.py` |
| 直接运行 `main.py` 找不到模块 | 在 `run_gui` 前将 `src/` 注入 `sys.path`，便于开发时直接执行 | `src/gui/main.py` |
| 打包后无控制台静默崩溃 | `run_gui` 中注册 `sys.excepthook`，将未捕获异常写入日志 | `src/gui/main.py` |
| 日志路径 | 开发与打包场景下日志由 `gui.utils.logging` 统一配置；打包后常见落盘于 `Package/logs/gui.log`（若存在该目录） | `src/gui/utils/logging.py`（若需对照） |

### 2. 主题与配置体验

- **默认主题**：由浅色改为**深色**默认（`ThemeManager.reset_to_default` 同步为深色）。
- **主题配置对话框**：分组展示颜色项、右侧**实时预览**（按钮/表格样式），应用后通过父窗口 `refresh_theme()` 刷新主界面。
- 相关实现：`src/gui/config/theme.py`、`src/gui/ui/main_window.py`（内含 `ThemeConfigDialog`）。

### 3. 编码方案（LZSS / LZDP / DPFlate）与预览

- **参数模型**：`src/gui/models.py` 中 `ALGORITHM_PARAMS` 为 LZSS、LZDP、DPFlate 配置了 `use_flag_encoding`（0/1 枚举文案与 LZDP 对齐）。
- **引擎下发**：`CompressionEngine._create_compressor` 对配置字典中每个键调用 `set_<key>`；C++ 侧通过 pybind 暴露 `set_use_flag_encoding` / `get_use_flag_encoding`。
- **预览文案**：`src/gui/ui/helpers.py` 中 `format_lzss_preview`、`format_dpflate_lz_reference_preview` 与 `format_lzdp_preview` 风格对齐（offset/length 位宽推导、两种模式下的 bit 说明）。
- **主窗刷新**：`main_window.py` 中编码预览刷新时传入 UI 当前 `use_flag_encoding`。

### 4. 核心算法与绑定（C++）

| 组件 | 行为 |
|------|------|
| **LZSS**（`src/algorithm/LZSS.*`） | `compress` / `decompress` 增加 `use_flag_encoding`。`true`：沿用「每 8 符号 1 字节 flag + 载荷」；`false`：无 flag 分组时的另一套 16-bit token 流（与解压路径一致）。 |
| **LZSSCompressor** | 将 `use_flag_encoding_` 传入 `algorithm::LZSS::compress/decompress`。 |
| **DPFlate**（`src/algorithm/DPFlate.*`） | 增加 `use_flag_encoding_`；在 `handleBuildTree` 的 DP 中按模式切换**字面量 / 匹配的代价权重**（启发式位宽，非 Deflate 实码流第二套格式）。 |
| **DPFlateCompressor** | 构造 `DPFlate` 后调用 `set_use_flag_encoding`。 |
| **pybind** | `LZSSCompressor`、`DPFlateCompressor` 绑定 `set_use_flag_encoding` / `get_use_flag_encoding`。 |

**注意**：DPFlate 对外比特流仍为 **Deflate 兼容 + Huffman**；编码方案开关主要影响 **DP 选路代价**，在部分数据上压缩体积差异可能不明显；LZSS 在两种模式下流格式不同，**解压必须使用与压缩相同的 `use_flag_encoding` 设置**（与配置持久化一致即可）。

### 5. 文档与仓库卫生

- `docs/gui_architecture.md`：与现行 `src/gui/` 结构对齐说明。
- `docs/standardization/build_baseline_v0.md`：含 CMake 构建验证记录（若存在该节）。
- 已清理空目录、备份 CMakeLists、临时脚本等（历史 commit 中可见）。

---

## 二、文件架构修改说明（GUI）

以下为**自旧结构迁移到现行结构**的映射关系；新代码请勿再写入已删除的旧包名。

| 迁移前（概念包） | 现行位置 | 职责 |
|------------------|----------|------|
| `domain/` | `src/gui/models.py` | `FileRecord`、`AlgorithmType`、`ALGORITHM_PARAMS`、默认配置等 |
| `core/`（引擎/配置混杂） | `src/gui/engine/`、`src/gui/config/` | `CompressionEngine`、`bridge`、主题与 `settings` |
| `widgets/` | `src/gui/ui/` | `main_window`、`table`、views、panels、dialogs、`helpers` |
| `io/`（扫描等） | `src/gui/utils/file_helper.py` 等 | 目录扫描、批量加载 |
| `web/`（重复） | 已移除；能力并入 `src/gui/windows/` | HTML 报告、独立窗口内容 |

**现行 GUI 顶层树**（仅列一级与关键子项）：

```
src/gui/
├── main.py              # run_gui / run_cli；sys.path、excepthook
├── models.py
├── config/              # settings, theme
├── engine/              # compressor, bridge, file_protocol, token_parser
├── ade/
├── algorithms/
├── ui/                  # main_window, table, helpers, views, panels, dialogs
├── windows/
├── utils/
└── io/                  # 占位或轻量入口
```

更细的 `.py` 文件列表与对话框落位见 **`docs/gui_architecture.md`**。

---

## 三、后续维护建议

1. **修改 LZSS 流格式**时：必须同时更新 `LZSS::compress/decompress`、`LZSSCompressor`、以及 GUI/ADE 若存在依赖原始布局的解析器（如 `token_parser.py`）。
2. **发布 Windows 包**：修改 C++ 后需重新 CMake 构建 `core_engine`，再执行项目惯用的 PyInstaller / `run.bat` 流程；勿将 `build/`、`Package/logs` 等产物误当源码提交（见根目录 `.gitignore`）。
3. **回归**：至少覆盖 LZSS 两种 `use_flag_encoding` 的压缩–解压往返；DPFlate 建议对文本与随机数据各跑一轮体积与 `done` 标志检查。

---

## 四、相关文档索引

| 文档 | 用途 |
|------|------|
| `README.md` | 仓库入口、布局、构建与运行入口 |
| `docs/gui_architecture.md` | GUI 模块边界与目录权威说明 |
| `docs/engineering_standardization_directive.md` | 工程规范总纲（若与本地流程冲突以仓库为准） |
| `docs/standardization/build_baseline_v0.md` | 可复现构建基线 |
| `src/README.md` | `src/` 下各子系统说明 |

---

*文档生成用于工作交接；若实现与行为不一致，以 Git 历史与源码为准。*
