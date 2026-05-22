# 分层文件归属与输出位置

目标：防止文件“随手落在仓库根目录”，保持可定位、可维护、可清理。

## 1. 仓库根目录允许内容

仅允许：

- 构建入口与元文件（`CMakeLists.txt`、`README.md`、`requirements.txt` 等）
- 项目级配置放在 `config/`（如 `config/webcompress_settings.json`）
- 必要的一次性入口脚本（需在文档说明用途）

禁止新增：

- 临时测试脚本（`quick_*.py`、`tmp*.txt`）
- 临时可视化 HTML (`test_*_viz.html`)
- 临时输出文本 (`*_output.txt`)
- 测试报告文本（统一放 `docs/reports/`）
- Windows 快捷方式文件（`*.lnk`）
- 压缩运行产物（`*.wcx`、`*.restored`）

## 2. 源码归属

- `src/algorithm`：算法实现
- `src/core`：压缩器封装与工厂
- 流式适配器（如 `StreamingAdapter`）归 `src/core`，不放 `src/algorithm`
- `src/processor`：流式编排与 pipeline 构建（含压缩链/解压链构建）
- `src/archiver`：归档容器读写（不再承载算法链构建策略）
- `src/bindings`：pybind 绑定（wasm 目录已移除）
- `src/gui`：桌面 GUI（`ui/`、`engine/`、`config/`、`ade/`、`models.py`、`windows/`、`utils/` 等）
  - `ui/views/`：页面级视图组件
  - `ui/panels/`：复用面板/控件
  - `ui/helpers.py`：共享辅助（原 `label_utils` / 编码预览等合并）
  - `ui/dialogs/`：模态对话框（压缩热力图、LZ/Flate 演示等）
- `src/ade`：ADE 决策与特征提取

## 3. 测试归属

- C++ 测试：`tests/`
- Python 测试：放到 `tests/python/`（已执行首批根目录迁移）
- 性能/实验脚本：`scripts/experiments/`
- 原 `src/others/` 类目录不再用于新增代码；实验代码统一放 `scripts/experiments/`
- 测试压缩样本：`resources/fixtures/compressed/`

## 4. 产物输出归属

- 可分发包：`Package/`
- 构建目录：`build/`、`build_pybind/`
- 运行日志：开发态 `logs/`，打包态 `Package/logs/`
- 打包配置：开发态根目录配置，打包态 `Package/config/`
- ADE 模型与训练数据：源码侧 `assets/ade/`，打包时复制到 `Package/bin/ade/`
- 测试与评测报告：`docs/reports/benchmarks/` 与 `docs/reports/debug/`

## 4.1 文档与报告边界

- `docs/`：人工维护的设计、规范、决策、交接文档（稳定知识）。
- `docs/reports/`：脚本/测试运行产生的输出产物（可重复生成）。
- 同名主题若同时存在：
  - 结论/设计放 `docs/`
  - 原始运行输出放 `docs/reports/`

## 5. 命名规范（文件级）

- 窗口聚合模块：`*_window.py` / `*_windows.py`
- 复用视图：`*_view.py`
- 面板：`*_panel.py`
- 兼容转发层：`*_compat.py`（或在 README 明确 shim）
- 历史备份：放入 `legacy/`，并禁止运行时导入

## 6. 执行门禁

提交前至少满足：

1. 根目录无新增临时脚本/临时输出文件
2. 新文件放入对应归属目录
3. 若新增“历史兼容路径”，必须写明迁移计划与删除时间点

