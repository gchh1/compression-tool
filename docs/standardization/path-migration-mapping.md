# 路径迁移对照表（旧路径 → 新路径）

阶段 0 不进行大规模搬迁。本表记录当前已发生且可确认的映射，并为后续阶段预留。

> **2026 更新**：`src/gui/widgets/` 已整体迁至 `src/gui/ui/`。下表行 26–36 的「新路径」列已写为 **当前仓库中的最终路径**（不再指向 `widgets/` 下的中间目录）。

| 旧路径 | 新路径 | 类型 | 状态 |
|---|---|---|---|
| `src/gui/prompts.md` | `docs/prompts(important).md` | 文档迁移 | 已完成 |
| `src/core/LZMineCompressor.cpp` | `src/core/LZDPCompressor.cpp` | 模块重命名 | 已完成 |
| `src/core/MyFlateCompressor.cpp` | `src/core/DPFlateCompressor.cpp` | 模块重命名 | 已完成 |
| `src/gui/web/` | `src/gui/windows/` | 目录语义重命名 | 已完成 |
| `src/gui/widgets/heatmap_widgets.py` | *removed* — use `gui.ui.dialogs.*` / `gui.ui.views.visualizers.token_heatmap`；可选 `gui.ui.visualization_windows`（聚合 re-export） | 模块职责正名 | 已完成 |
| `src/gui/ui/visualization_windows.py`（原单体） | `src/gui/ui/dialogs/*.py` + `views/visualizers/token_heatmap.py`、`huffman.py` + 薄 `visualization_windows.py` | UI 拆分 | 已完成 |
| `src/gui/widgets/main_window.py.backup` | `src/gui/ui/legacy/main_window_legacy_backup.py`（历史归档；仓库未必保留 `.py` 正文） | 备份代码归档 | 已完成 |
| `src/others/transformer.py` | `scripts/experiments/transformer_scratch.py` | 实验脚本归位 | 已完成 |
| `src/test_streaming_large.py` | `scripts/experiments/test_streaming_large.py` | 性能脚本归位 | 已完成 |
| `src/random_test.deflate` | `resources/fixtures/compressed/random_test.deflate` | 测试样本归位 | 已完成 |
| `src/repetitive_test.deflate` | `resources/fixtures/compressed/repetitive_test.deflate` | 测试样本归位 | 已完成 |
| `src/libgcc_s_seh-1.dll` | `build/src/bindings/pybind/libgcc_s_seh-1.dll` | 运行时二进制归位 | 已完成 |
| `src/libstdc++-6.dll` | `build/src/bindings/pybind/libstdc++-6.dll` | 运行时二进制归位 | 已完成 |
| `src/libwinpthread-1.dll` | `build/src/bindings/pybind/libwinpthread-1.dll` | 运行时二进制归位 | 已完成 |
| `compression_results.txt` | `docs/reports/benchmarks/compression_results.txt` | 根目录报告归档 | 已完成 |
| `compression_test_results.txt` | `docs/reports/benchmarks/compression_test_results.txt` | 根目录报告归档 | 已完成 |
| `test_output.txt` | `docs/reports/debug/test_output.txt` | 根目录报告归档 | 已完成 |
| `test_full_output.txt` | `docs/reports/debug/test_full_output.txt` | 根目录报告归档 | 已完成 |
| `command.txt` | `docs/legacy/command_legacy.txt` | 临时命令文档归档 | 已完成 |
| `src/gui/widgets/analysis_view.py` | `src/gui/ui/views/analysis_view.py` | widgets→ui 视图归位 | 已完成 |
| `src/gui/widgets/comparison_view.py` | `src/gui/ui/views/comparison_view.py` | widgets→ui 视图归位 | 已完成 |
| `src/gui/widgets/compression_view.py` | `src/gui/ui/views/compression_view.py` | widgets→ui 视图归位 | 已完成 |
| `src/gui/widgets/dashboard_view.py` | `src/gui/ui/views/dashboard_view.py` | widgets→ui 视图归位 | 已完成 |
| `src/gui/widgets/network_view.py` | `src/gui/ui/views/network_view.py` | widgets→ui 视图归位 | 已完成 |
| `src/gui/widgets/block_visualizer.py` | `src/gui/ui/views/visualizers/block.py` | widgets→ui visualizers | 已完成 |
| `src/gui/widgets/huffman_visualizer.py` | `src/gui/ui/views/visualizers/huffman.py` | widgets→ui visualizers | 已完成 |
| `src/gui/widgets/info_panel.py` | `src/gui/ui/panels/info_panel.py` | widgets→ui 面板归位 | 已完成 |
| `src/gui/widgets/property_panel.py` | `src/gui/ui/panels/property_panel.py` | widgets→ui 面板归位 | 已完成 |
| `src/gui/widgets/resource_tree.py` | `src/gui/ui/panels/resource_tree.py` | widgets→ui 面板归位 | 已完成 |
| `src/gui/widgets/label_utils.py` | `src/gui/ui/helpers.py`（与 encoding 预览等合并） | widgets→ui 工具合并 | 已完成 |
| `src/gui/widgets/`（桌面 UI 包） | `src/gui/ui/` | 包重命名 | 已完成 |
| `src/gui/web_backup/` | `（已删除）` | 过时备份目录清理 | 已完成 |
| `docs/algorithm_decision_engine_design.md` | `docs/design/algorithm_decision_engine_design.md` | 文档分层归位 | 已完成 |
| `docs/lzdp_dp_design.md` | `docs/design/lzdp_dp_design.md` | 文档分层归位 | 已完成 |
| `docs/lz_token_encoding_discussion.md` | `docs/design/lz_token_encoding_discussion.md` | 文档分层归位 | 已完成 |
| `docs/feature_vector_specification_v3_final.md` | `docs/specs/feature_vector_specification_v3_final.md` | 文档分层归位 | 已完成 |
| `docs/algorithm_comparison_report.md` | `docs/analysis/algorithm_comparison_report.md` | 文档分层归位 | 已完成 |
| `docs/compression_analysis_report.md` | `docs/analysis/compression_analysis_report.md` | 文档分层归位 | 已完成 |
| `docs/work_handover_report.md` | `docs/analysis/work_handover_report.md` | 文档分层归位 | 已完成 |
| `src/algorithm/include/StreamingAdapter.hpp` | `src/core/include/StreamingAdapter.hpp` | 分层语义归位（非纯算法） | 已完成 |
| `src/algorithm/StreamingAdapter.cpp` | `src/core/StreamingAdapter.cpp` | 分层语义归位（非纯算法） | 已完成 |
| `src/ade/data/*` | `assets/ade/*` | ADE 资产归位 | 已完成 |
| `src/ade/tests/*` | `tests/cpp/ade/*` | ADE C++ 测试归位 | 已完成 |
| `archiver` 内部算法链构建逻辑 | `processor/PipelineBuilder.*` | 编排职责抽离 | 已完成 |

## 阶段 3 预留（待审批）

- `Package/` 运行产物目录规范化（目标路径待需求方确认）
- 大样本与评测产物归档策略
- GUI 备份目录与临时脚本归位/移除
- `src/gui/web_backup/` 已完成清理

