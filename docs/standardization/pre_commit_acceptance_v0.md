# Pre-Commit Acceptance v0

目标：在不改变既有可观察行为的前提下，为当前大整改提供“可提交范围”清单。

## A. 可提交（本轮架构规范化核心）

- 目录与命名规范化
  - `src/gui/web/*` -> `src/gui/windows/*`
  - `heatmap_widgets` removed; compression dialogs live under `src/gui/ui/dialogs/`（`heatmap_dialog.py` 等）；`src/gui/ui/visualization_windows.py` 提供兼容用聚合 re-export（与直接 import `dialogs` 等价）
  - `src/gui/widgets/main_window.py.backup` -> `src/gui/ui/legacy/main_window_legacy_backup.py`（包已迁至 `ui/`）
- 配置与路径归位
  - `webcompress_settings.json` -> `config/webcompress_settings.json`
  - `src/others/transformer.py` -> `scripts/experiments/transformer_scratch.py`
  - `src/test_streaming_large.py` -> `scripts/experiments/test_streaming_large.py`
  - `src/random_test.deflate` / `src/repetitive_test.deflate` -> `resources/fixtures/compressed/`
  - 根目录报告归档到 `docs/reports/`，命令记录归档到 `docs/legacy/`
- 规则与文档
  - `.gitignore`（新增 `*.lnk`、`*.wcx`、`*.restored`、`src/*.dll`、`src/logs/` 等）
  - `docs/standardization/*`
  - `src/README.md`、`docs/reports/README.md`
- 功能修复与兼容
  - `src/gui/models.py`（历史 `core/domain` 模型）
  - `src/gui/ade/engine.py` 等（历史 `core/decision` 职责）
  - `src/gui/config/settings.py`（历史 `core/app_config`）
  - `src/gui/main.py`
  - `src/gui/ui/main_window.py`（历史 `widgets/main_window.py`）
  - `src/core/include/LZDPCompressor.hpp`
  - `src/bindings/pybind/pybind_module.cpp`
  - `tests/python/test_compression_ratios.py`

## B. 建议排除（运行产物，不应进入整改提交）

- `Package/bin/WebCompress.exe`
- `Package/logs/gui.log`
- `Package/ade/ade_stats_v3.json`
- `Package/ade/ade_training_v3.jsonl`
- `Package/config/webcompress_settings.json`（若只是运行后回写）
- `bin/test_Deflate.exe`

## C. 待你确认（策略项）

- `CMakeLists.txt`（是否纳入本次整改提交）
- `run.bat`（当前已修改；建议保留，因为与打包路径和模型资源绑定）
- `ade/default_model.bin`（是否作为源码/模型资产纳入追踪）

## D. 提交前最小验证

1. `python -m py_compile src/gui/config/settings.py src/gui/main.py tests/python/test_compression_ratios.py`
2. GUI 冒烟：启动 -> 压缩 -> 解压 -> 演示窗口打开
3. 打包冒烟：`run.bat` 输出 `Package/bin/WebCompress.exe` 可启动
