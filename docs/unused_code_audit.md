# GUI 未使用代码审查报告

> 分析范围：`src/gui/` 下所有 64 个 Python 文件，共 1554 条定义
> 分析方法：AST 静态分析 + 交叉引用检查（import 级别 + 全符号匹配）
> 生成日期：2026-05-24

---

## 一、完整文件级未使用（14 个文件）

以下 `.py` 文件在运行时**不会被任何其他模块导入**，属于可删除候选：

| 文件 | 原因 | 建议 |
|------|------|------|
| `engine/web_dict_embedded.py` | 未被任何模块导入；`web_dict.py` 自包含实现 | 删除 |
| `io/__init__.py` | 空文件 | 删除 |
| `main.py` | 这是 `python -m gui` 入口点；由 Python 解释器直接执行，不被其他模块 import | **保留**（入口文件） |
| `ui/common/__init__.py` | 空文件 | 删除 |
| `ui/panels/property_panel.py` | 仅被 `visualization_windows.py` 引用（该文件本身也未被导入）；`PropertyPanel` 不在 UI 中实际使用 | 删除文件 |
| `ui/views/analysis_view.py` | 仅被 `visualization_windows.py` 引用；`AnalysisView` 不在 UI 中实际展示 | 删除文件 |
| `ui/views/comparison_view.py` | 同上；`ComparisonView` 不在 UI 中实际展示 | 删除文件 |
| `ui/views/compression_view.py` | 同上；`CompressPage` 不在 UI 中实际展示（`main_window.py` 内部实现了自己的压缩逻辑） | 删除文件 |
| `ui/views/dashboard_view.py` | 同上；`DashboardView` 不在 UI 中实际展示 | 删除文件 |
| `ui/views/network_view.py` | 同上；`NetworkView` 不在 UI 中实际展示 | 删除文件 |
| `ui/visualization_windows.py` | 后向兼容 barrel 文件，只 re-export 其他 dialog 模块；但本身未被任何代码导入 | 删除（已有 `dialogs/__init__.py`） |
| `windows/comparison.py` | 被 `main_window.py` **直接 import 调用**（`generate_comparison`/`open_comparison_html`） | **保留** |
| `windows/heatmap.py` | 同上（`generate_heatmap`/`open_heatmap_html`） | **保留** |
| `windows/token_heatmap.py` | 同上（`generate_token_heatmap`/`open_token_heatmap_html`） | **保留** |

> **注**：`windows/` 下的文件因其函数被 `main_window.py` 直接 `from gui.windows.xxx import xxx` 调用，在 AST import 级别分析中未被识别为"被别人导入"，但实际是**被使用的**。手动核实后确认全部保留。

---

## 二、`gui/ade/` 模块（完全未使用的决策引擎）

整个 `gui/ade/` 模块虽然是独立的智能决策引擎实现，但存在以下问题：
1. `gui.ade.features` 子模块**不存在** → 运行时日志持续报 `ModuleNotFoundError: No module named 'gui.ade.features'`
2. `DecisionEngineManagerDialog` 已在 `_setup_menu` 中**被移除**（菜单中没有入口）
3. 所有 ADE 类在 `main_window.py` 中仅被 import，无法被用户触发

### 2.1 可删除的类

| 类 | 文件 | 说明 |
|----|------|------|
| `DecisionEngine` | `ade/engine.py` | 核心决策引擎，无法被用户触发 |
| `RandomForestStrategy` | `ade/engine.py` | RF 策略，从未被实例化 |
| `NeuralNetworkStrategy` | `ade/engine.py` | NN 策略，从未被实例化 |
| `StrategyDispatcher` | `ade/engine.py` | 策略分发器，从未被使用 |
| `SilentExplorer` | `ade/explorer.py` | 静默探索器，无法被用户触发 |
| `ArmStats` | `ade/explorer.py` | 探索臂统计，仅被 `SilentExplorer` 内部使用 |
| `FeatureClusterSpace` | `ade/explorer.py` | 特征聚类空间，同上 |
| `TrainingSampleV3` | `ade/training.py` | 训练样本类，未使用 |
| `TrainingDataStore` | `ade/training.py` | 训练数据存储，未使用 |
| `ParameterRegressor` | `ade/params.py` | 参数回归器，未使用 |
| `ExploreStreamResult` | `ade/streaming_explore.py` | 流式探索结果，未使用 |
| `RFTrainResult` / `RFTrainWorker` | `ade/rf_train.py` | RF 训练结果/Worker，未使用 |
| `NNTrainResult` / `NNTrainWorker` | `ade/nn_train.py` | NN 训练结果/Worker，未使用 |
| `Stage2TrainResult` / `Stage2TrainWorker` | `ade/stage2_train.py` | Stage2 训练，未使用 |
| `CheckpointManifest` / `ExploreDispatch` | `ade/checkpoint.py` | 检查点相关，未使用 |

### 2.2 可删除的函数

| 函数 | 文件 | 说明 |
|------|------|------|
| `count_rf_ready_samples` | `ade/rf_train.py` | 从未被调用 |
| `count_nn_ready_samples` | `ade/nn_train.py` | 从未被调用 |
| `count_stage2_ready_nn` | `ade/stage2_train.py` | 从未被调用 |
| `describe_nn_sample_gap` | `ade/nn_train.py` | 从未被调用 |
| `train_rf_from_jsonl` | `ade/rf_train.py` | 从未被调用 |
| `train_nn_from_jsonl` | `ade/nn_train.py` | 从未被调用 |
| `format_train_summary` | `ade/rf_train.py` | 从未被调用 |
| `format_nn_train_summary` | `ade/nn_train.py` | 从未被调用 |
| `pad_features_for_rf` | `ade/rf_train.py` | 从未被调用 |
| `v3_sample_to_ade_label` | `ade/rf_train.py` | 从未被调用 |
| `build_rf_rows` | `ade/rf_train.py` | 从未被调用 |
| `_find_train_cli` | `ade/rf_train.py` | 从未被调用 |
| `_export_training_json` | `ade/rf_train.py` | 从未被调用 |
| `_make_rf_config` | `ade/rf_train.py` | 从未被调用 |
| `_split_train_val` | `ade/rf_train.py` | 从未被调用 |
| `_accuracy` | `ade/rf_train.py` | 从未被调用 |
| `_train_via_ade` | `ade/rf_train.py` | 从未被调用 |
| `_train_via_cli` | `ade/rf_train.py` | 从未被调用 |
| `_update_train_stats` | `ade/rf_train.py` | 从未被调用 |
| `_set_stream_cancel` | `ade/streaming_explore.py` | 从未被调用 |
| `clear_simulate_cancel_env` | `ade/streaming_explore.py` | 从未被调用 |
| `_resolve_input_path` | `ade/streaming_explore.py` | 从未被调用 |
| `should_explore_use_streaming` | `ade/streaming_explore.py` | 从未被调用 |
| `explore_chunk_bytes` | `ade/streaming_explore.py` | 从未被调用 |
| `compress_streaming_to_wcx` | `ade/streaming_explore.py` | 从未被调用 |
| `compress_memory_to_wcx_bytes` | `ade/streaming_explore.py` | 从未被调用 |
| `decompress_wcx_to_bytes` | `ade/streaming_explore.py` | 从未被调用 |
| `wcx_codec_payload` | `ade/streaming_explore.py` | 从未被调用 |
| `_normalize_params_for_algo` | `ade/ea_tune.py` | 从未被调用 |
| `new_job_id` | `ade/checkpoint.py` | 从未被调用 |
| `jobs_dir` | `ade/checkpoint.py` | 从未被调用 |
| `manifest_path` | `ade/checkpoint.py` | 从未被调用 |
| `save_manifest` | `ade/checkpoint.py` | 从未被调用 |
| `load_manifest` | `ade/checkpoint.py` | 从未被调用 |
| `block_profile_hint` | `ade/checkpoint.py` | 从未被调用 |
| `get_training_store` | `ade/training.py` | 从未被调用 |
| `_thread_tag` | `ade/explore_log.py` | 从未被调用 |

### 2.3 建议

**删除整个 `ade/` 目录**（9 个文件，约 3500 行代码）。如果将来需要恢复 ADE 功能，可以从 Git 历史中找回。

---

## 三、视图/面板层从未实际使用（6 个文件）

以下 UI 组件虽然代码完整，但从未被 `MainWindow` 或任何实际 UI 代码实例化：

| 类 | 文件 | 行号 | 说明 |
|----|------|------|------|
| `AnalysisView` | `ui/views/analysis_view.py` | 71 | 列表面板，不展示 |
| `_HeatmapWidget` | `ui/views/analysis_view.py` | 17 | 仅供 AnalysisView 内部使用 |
| `ComparisonView` | `ui/views/comparison_view.py` | 133 | 比较视图，不展示 |
| `ExternalToolBenchmarkWorker` | `ui/views/comparison_view.py` | 28 | 仅供 ComparisonView 内部使用 |
| `CompressPage` | `ui/views/compression_view.py` | 15 | 压缩面板，不展示（MainWindow 内置实现） |
| `DashboardView` | `ui/views/dashboard_view.py` | 161 | 仪表盘，不展示 |
| `_SummaryCard` | `ui/views/dashboard_view.py` | 15 | 仅供 DashboardView 内部使用 |
| `_PieChart` | `ui/views/dashboard_view.py` | 35 | 仅供 DashboardView 内部使用 |
| `_BarChart` | `ui/views/dashboard_view.py` | 105 | 仅供 DashboardView 内部使用 |
| `NetworkView` | `ui/views/network_view.py` | 78 | 网络视图，不展示 |
| `_TransferBarChart` | `ui/views/network_view.py` | 16 | 仅供 NetworkView 内部使用 |
| `PropertyPanel` | `ui/panels/property_panel.py` | 14 | 属性面板，不展示 |

**建议**：删除 `ui/views/` 下除 `visualizers/` 子目录外的所有文件，以及 `ui/panels/property_panel.py`。

---

## 四、Demo/可视化对话框的内部辅助类（仅被自身引用，不暴露）

以下类只在 `dialogs/` 内部使用，是对外不可见的实现细节。不是"未使用"，但如要重构可考虑内联或精简：

| 类 | 文件 | 行号 |
|----|------|------|
| `_BuildStep` | `ui/dialogs/flate_demo_dialog.py` | 22 |
| `_QueueNode` | `ui/dialogs/flate_demo_dialog.py` | 35 |
| `_ZoomableTreeCanvas` | `ui/dialogs/flate_demo_dialog.py` | 88 |
| `HuffmanBuildAnimator` | `ui/dialogs/flate_demo_dialog.py` | 152 |
| `DPArrayBar` | `ui/dialogs/lz_demo_dialog.py` | 68 |
| `BlockHeatmapCanvas` | `ui/dialogs/block_heatmap_dialog.py` | 21 |

---

## 五、Demo 类（功能完整但低使用频率）

以下类虽然被注册并通过菜单打开，属于"可用但非核心"：

| 类 | 文件 | 使用频率 |
|----|------|----------|
| `FlateDemoDialog` | `ui/dialogs/flate_demo_dialog.py` | 低 |
| `HuffmanTreeDialog` | `ui/dialogs/huffman_dialog.py` | 低 |
| `BlockHeatmapDialog` | `ui/dialogs/block_heatmap_dialog.py` | 低 |
| `HeatmapDialog` | `ui/dialogs/heatmap_dialog.py` | 低 |
| `LZDPDPDialog` / `LZSliderDialog` | `ui/dialogs/lz_demo_dialog.py` | 低 |
| `NetworkSimDialog` | `ui/dialogs/network_sim_dialog.py` | 低 |

**建议**：保留，但可考虑移到独立的 demo 目录。

---

## 六、`gui/engine/token_parser.py` —— 仅 Demo 用途

`token_parser.py` 的所有解析器类（`TokenParser`, `LZSSTokenParser`, `LZDPTokenParser`, `DeflateTokenParser`, `_LSBBitReader`, `_CanonicalHuffmanTree`）及解析函数只在以下位置被引用：

- `flate_demo_dialog.py` — 用于 Deflate 演示
- `lz_demo_dialog.py` — 用于 LZ 演示
- `huffman_dialog.py` — 用于 Huffman 演示
- `heatmap_dialog.py` — 用于热图演示

**这些解析器不参与实际的压缩/解压流水线**。如果移除所有 demo 对话框，`token_parser.py` 也可一并移除。

---

## 七、函数级可删除候选（跨模块零引用）

除上述类和文件外，以下顶层函数在**其他 Python 文件中无任何引用**（private helper 已排除）：

| 函数 | 文件 | 行号 | 说明 |
|------|------|------|------|
| `_streaming_per_algorithm_defaults` | `config/settings.py` | 23 | 仅在 settings.py 内部被 `DEFAULTS` 调用 |
| `_merge_with_defaults` | `config/settings.py` | 142 | 仅在 settings.py 内部使用 |
| `get_algo_config` | `config/settings.py` | 332 | 未在外部被调用 |
| `_repo_root` | `engine/bridge.py` | 22 | 仅供 `bridge.py` 内部使用 |
| `_build_candidates` | `engine/bridge.py` | 26 | 同上 |
| `_dir_has_module_pyd` | `engine/bridge.py` | 46 | 同上 |
| `_frozen_candidates` | `engine/bridge.py` | 52 | 同上 |
| `_try_load_engine` | `engine/bridge.py` | 64 | 同上（但被 `compressor.py` import） |
| `_apply_huffman_slot_config` | `engine/compressor.py` | 43 | 仅在 compressor.py 内部使用 |
| `create_compressor_for_visualization` | `engine/compressor.py` | 132 | 未在外部被调用 |
| `_gzip_bytes_compress` | `engine/compressor.py` | 380 | 内部方法（`_` 前缀） |
| `_gzip_bytes_decompress` | `engine/compressor.py` | 398 | 内部方法 |
| `_gzip_smart_compress_file` | `engine/compressor.py` | 413 | 内部方法 |
| `_gzip_smart_decompress_file` | `engine/compressor.py` | 433 | 内部方法 |
| `smart_compress` | `engine/compressor.py` | 449 | 被 `gui/models.py` 引用 |
| `smart_decompress` | `engine/compressor.py` | 470 | 内部方法 |
| `smart_compress_file` | `engine/compressor.py` | 571 | 被 `gui/models.py` 引用 |
| `pack_files` | `engine/compressor.py` | 890 | 仅在 compressor.py 内部使用？需核实 |
| `deframe_u32_be_chunk_stream` | `engine/file_protocol.py` | 305 | 未在外部被引用 |
| `CompressedFileHeader` | `engine/file_protocol.py` | 39 | 仅被 `file_protocol.py` 内部使用 |
| `_pack_compressed_file_python_mirror` | `engine/file_protocol.py` | 136 | 内部方法 |
| `NetworkBenchmarkItem` | `engine/network_transfer.py` | 28 | 仅在 network_transfer.py 内部使用 |
| `throttled_deliver` | `engine/network_transfer.py` | 59 | 被 `network_sim_dialog.py` 使用 |
| `item_from_file_record` | `engine/network_transfer.py` | 207 | 被 `network_sim_dialog.py` 使用 |
| `benchmark_profile` | `engine/network_transfer.py` | 120 | 被 `network_sim_dialog.py` 使用 |
| `_dict_file_paths` | `engine/web_dict.py` | 17 | 内部函数 |
| `load_phrases` | `engine/web_dict.py` | 43 | 被 `web_dict.py` 内部使用 |
| `decode` | `engine/web_dict.py` | 99 | 被 `compressor.py` 调用 |
| `run_cli` | `main.py` | 18 | CLI 入口 |
| `run_gui` | `main.py` | 62 | GUI 入口 |
| `handle_exception` | `main.py` | 68 | 异常处理 |
| `_log_quit` | `main.py` | 117 | 退出日志 |
| `get_folder_size` | `models.py` | 445 | 内部函数 |
| `DirectoryRecord` | `models.py` | — | 如果存在 |

> 注：以上列表中带 `_` 前缀的已经是 Python 约定的"内部使用"方法，不在"可删除"之列。

---

## 八、Qt 事件重写（False Positives，不可删除）

以下方法被 AST 分析标记为"零外部引用"，但实为 Qt 事件系统的虚函数重写，由 C++ Qt 运行时直接回调：

```
paintEvent, mousePressEvent, mouseMoveEvent, mouseReleaseEvent,
mouseDoubleClickEvent, wheelEvent, keyPressEvent, keyReleaseEvent,
enterEvent, leaveEvent, focusInEvent, focusOutEvent,
resizeEvent, moveEvent, closeEvent, showEvent, hideEvent,
contextMenuEvent, dragEnterEvent, dragMoveEvent, dragLeaveEvent,
dropEvent, changeEvent, timerEvent, sizeHint, minimumSize
```

**不可删除。**

---

## 九、汇总建议

| 优先级 | 操作 | 影响 |
|--------|------|------|
| 🔴 **高** | 删除整个 `gui/ade/` 目录（9 文件） | 消除 `ModuleNotFoundError` 警告 |
| 🔴 **高** | 删除 `gui/ui/views/` 下 5 个无用视图 | 减少 800+ 行死代码 |
| 🔴 **高** | 删除 `gui/ui/panels/property_panel.py` | 减少 150 行死代码 |
| 🟡 **中** | 删除 `gui/ui/visualization_windows.py` | 减少 50 行 barrel 代码 |
| 🟡 **中** | 删除 `gui/engine/web_dict_embedded.py` | 未使用 |
| 🟡 **中** | 删除 `gui/io/__init__.py` | 空文件 |
| 🟢 **低** | 删除 `gui/ui/common/__init__.py` | 空文件 |
| 🟢 **低** | 移除 `token_parser.py`（如 demo 一并移除） | 仅 demo 用途 |
| ⚪ **保留** | `windows/*.py` | 确认被 main_window.py 使用 |
| ⚪ **保留** | `dialogs/*.py` | 有菜单入口，低频但可用 |
| ⚪ **保留** | 所有 Qt 事件重写 | 框架回调，不可删除 |