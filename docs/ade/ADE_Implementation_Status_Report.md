# ADE 模块实现状态分析报告

> **生成日期**: 2026-05-26  
> **分析范围**: `src/ade/`（C++）与 `src/gui/ade/`（Python 编排）  
> **参考文档**: `docs/ade/algorithm_decision_engine_design.md`、`docs/ade/ADE-Decision-Pipeline.md`  
> **对比基准**: `algorithm_branch` 分支中的训练脚本

---

## 概述

ADE 完整流水线包含以下阶段：**Stage0（特征提取）→ Stage0′（规则引擎）→ Stage1（随机森林算法分类）→ Stage2a（参数回归网络）→ Stage2b（进化算法精调）→ Collect（数据采集与训练闭环）**。

各阶段完成度概要：

| 阶段 | 完成度 | 关键状态 |
|------|--------|---------|
| Stage0 特征提取 | ✅ ~95% | C++ 完整；Python 侧缺 Extension 特征 |
| Stage0′ 规则引擎 | ✅ 100% | 完整实现 |
| Stage1 随机森林 | ✅ ~85% | C++ 完整；缺 `rf_train.py` 训练脚本 |
| **Stage2a 参数回归** | ❌ **~10%** | **C++ 核心完全缺失** |
| Stage2b 进化算法 | ✅ ~90% | C++ 完整；缺 `ea_tune.py` 先验工具 |
| Collect 数据采集 | ⚠️ ~85% | 核心存储与探索完整；缺辅助函数 |

---

## Stage0 — 特征提取（Feature Extraction）

**设计规格**：Base 20 维 + Extension 5–8 维（按文件类型），单遍 O(n) 扫描。

| 组件 | 文件路径 | 状态 | 说明 |
|------|---------|------|------|
| C++ 特征提取器 | [BaseFeatureExtractor.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/BaseFeatureExtractor.hpp) | ✅ 完成 | 20维 Base 特征提取核心 |
| C++ 主流程编排 | [FeatureExtractorV3.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/FeatureExtractorV3.hpp)、[FeatureExtractorV3.cpp](file:///d:/AAA_C/compression-tool/src/ade/features/FeatureExtractorV3.cpp) | ✅ 完成 | Magic Bytes → Base → Extension 流水线 |
| C++ 扩展特征 | [ExtensionExtractors.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/ExtensionExtractors.hpp) | ✅ 完成 | 6 类：TextCode(5)、Image(8)、Audio(6)、Video(7)、Archive(4)、Binary(5) |
| C++ 魔数检测 | [MagicBytesDetector.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/MagicBytesDetector.hpp)、[MagicBytesDetector.cpp](file:///d:/AAA_C/compression-tool/src/ade/features/MagicBytesDetector.cpp) | ✅ 完成 | ~40 种文件类型魔数模式 |
| C++ Count-Min Sketch | [CountMinSketch.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/CountMinSketch.hpp) | ✅ 完成 | 概率性 N-gram 频率估计 |
| C++ 特征向量结构 | [FeatureVectorV3.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/FeatureVectorV3.hpp)、[FeatureVector.cpp](file:///d:/AAA_C/compression-tool/src/ade/features/FeatureVector.cpp) | ✅ 完成 | 特征向量数据结构与序列化 |
| Python 特征提取 | [features.py](file:///d:/AAA_C/compression-tool/src/gui/ade/features.py) | ⚠️ 基本完成 | Python 侧复刻 20 维 Base 特征 + NumPy 加速版。Extension 特征提取未实现 |

**未完成项**：Python 侧 `features.py` 缺少 Extension 特征提取器，但可通过 C++ `ADE.analyze()` 调用绕过。

---

## Stage0′ — 规则引擎（Rule Engine）

| 组件 | 文件路径 | 状态 | 说明 |
|------|---------|------|------|
| C++ 规则引擎 | [DecisionEngine.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/DecisionEngine.hpp) `RuleEngine` | ✅ 完成 | `is_already_compressed`、`decide_low/medium/high/very_high_entropy` |
| C++ 决策逻辑 | [DecisionEngine.cpp](file:///d:/AAA_C/compression-tool/src/ade/runtime/DecisionEngine.cpp) | ✅ 完成 | 熵值区间判断 + 文件类型启发式 |

**完成度**: 100%

---

## Stage1 — 随机森林算法分类（Random Forest）

| 组件 | 文件路径 | 状态 | 说明 |
|------|---------|------|------|
| C++ 随机森林 | [RandomForest.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/RandomForest.hpp)、[RandomForest.cpp](file:///d:/AAA_C/compression-tool/src/ade/models/RandomForest.cpp) | ✅ 完成 | 训练/预测/特征重要性/序列化 |
| C++ ADE 桥接 | [ADEBridge.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/ADEBridge.hpp)、[ADEBridge.cpp](file:///d:/AAA_C/compression-tool/src/ade/runtime/ADEBridge.cpp) | ✅ 完成 | `analyze` / `analyze_file` / 模型加载 |
| C++ 训练工具 | [tools/train_model.cpp](file:///d:/AAA_C/compression-tool/src/ade/tools/train_model.cpp) | ✅ 完成 | 命令行训练工具 |
| C++ 数据集工具 | [tools/generate_dataset.cpp](file:///d:/AAA_C/compression-tool/src/ade/tools/generate_dataset.cpp) | ✅ 完成 | JSONL → RF 训练数据 |
| PyBind11 绑定 | [pybind_ade.cpp](file:///d:/AAA_C/compression-tool/src/bindings/pybind/pybind_ade.cpp) | ✅ 完成 | `ADE.analyze` / `ADE.load_model` / `ADE.train` |
| Python 决策编排 | [engine.py](file:///d:/AAA_C/compression-tool/src/gui/ade/engine.py) | ✅ 完成 | `DecisionEngine.decide` / `_map_algorithm` |
| **Python RF 训练脚本** | `src/gui/ade/rf_train.py` | ❌ **缺失** | 仅在 `algorithm_branch` 分支中存在 |

**未完成项**：`rf_train.py` 未移植到主分支。C++ 侧训练能力完整，缺少的是 UI 触发的训练流程入口。

---

## Stage2a — 参数回归网络（Param Regressor MLP）

**设计规格**：C++ MLP，输入 ~39 维（33 padded features + 6 one-hot algorithm），输出 5 维归一化参数（window_size / min_match / max_chain_length / lookahead_size / dp_range）。

| 组件 | 文件路径 | 状态 | 说明 |
|------|---------|------|------|
| **C++ 头文件** | `src/ade/include/ParamRegressorNet.hpp` | ❌ **完全缺失** | 网络中数据类型定义和接口声明未创建 |
| **C++ 实现** | `src/ade/models/ParamRegressorNet.cpp` | ❌ **完全缺失** | 网络前向/训练/预测未实现 |
| CMakeLists 引用 | [CMakeLists.txt](file:///d:/AAA_C/compression-tool/src/ade/CMakeLists.txt) | ❌ 未添加 | 未包含 ParamRegressorNet 编译目标 |
| PyBind11 绑定 | [pybind_ade.cpp](file:///d:/AAA_C/compression-tool/src/bindings/pybind/pybind_ade.cpp) | ❌ 未绑定 | 未暴露 `ParamRegressorNet` 给 Python |
| Python 封装 | [params.py](file:///d:/AAA_C/compression-tool/src/gui/ade/params.py) | ⚠️ 纯 Python 替代 | 完整 PyTorch MLP（build/predict/train/save/load），但有两个问题：<br>1. `_has_cpp` 始终 `False`，Python 侧无法替代 C++ 后端<br>2. `_extract_features` 中存在 `DecisionEngine.get()` 循环调用风险<br>3. 实际流水线中 `is_ready` 始终 `False`，NN 预测路径从未被触发 |
| **Python NN 训练脚本** | `src/gui/ade/nn_train.py` | ❌ **缺失** | 仅在 `algorithm_branch` 分支中存在 |
| **Python 重训练工具** | `src/gui/ade/retrain.py` | ❌ **缺失** | 仅在 `algorithm_branch` 分支中存在 |

**完成度**: ~10% — 这是 ADE 模块中完成度最低、影响最大的未完成子模块。

---

## Stage2b — 进化算法参数精调（Evolutionary Algorithms）

| 组件 | 文件路径 | 状态 | 说明 |
|------|---------|------|------|
| C++ 进化算法 | [EvolutionaryAlgorithms.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/EvolutionaryAlgorithms.hpp)、[EvolutionaryAlgorithms.cpp](file:///d:/AAA_C/compression-tool/src/ade/models/EvolutionaryAlgorithms.cpp) | ✅ 完成 | GA（遗传算法）、PSO（粒子群）、CMA-ES（协方差适应） |
| C++ 优化器 | [EvolutionaryAlgorithms.hpp](file:///d:/AAA_C/compression-tool/src/ade/include/EvolutionaryAlgorithms.hpp) `ParameterOptimizer` | ✅ 完成 | 统一接口，支持算法选择和参数边界 |
| PyBind11 绑定 | [pybind_ade.cpp](file:///d:/AAA_C/compression-tool/src/bindings/pybind/pybind_ade.cpp) | ✅ 完成 | `ParameterOptimizer` / `EAAlgorithm` / `ParameterBounds` / `OptimizationResult` |
| Python 引擎集成 | [engine.py](file:///d:/AAA_C/compression-tool/src/gui/ade/engine.py) `_optimize_params` | ✅ 完成 | EA 优化通过 PyBind11 调用 C++ |
| **Python EA 先验工具** | `src/gui/ade/ea_tune.py` | ❌ **缺失** | 仅在 `algorithm_branch` 分支中存在 |

**完成度**: ~90%。核心 EA 优化能力完整可用，仅有 `ea_tune.py`（从 JSONL 构建参数先验的工具）未移植。

---

## Collect — 数据采集与训练闭环

| 组件 | 文件路径 | 状态 | 说明 |
|------|---------|------|------|
| Python 训练数据存储 | [training.py](file:///d:/AAA_C/compression-tool/src/gui/ade/training.py) `TrainingDataStore` | ✅ 完成 | JSONL 格式，增量追加、验证、统计、CSV 导出 |
| Python 样本结构 | [training.py](file:///d:/AAA_C/compression-tool/src/gui/ade/training.py) `TrainingSampleV3` | ✅ 完成 | 20 维特征向量 + 算法标签 + 压缩指标 |
| Python 单例接口 | [training.py](file:///d:/AAA_C/compression-tool/src/gui/ade/training.py) `get_training_store()` | ✅ 完成 | 全局单例获取 |
| **Python 辅助函数** | `training.py` `algorithm_type_from_v3` | ❌ **缺失** | `algorithm_branch` 中有此函数，被训练脚本依赖 |
| Python 静默探索 | [explorer.py](file:///d:/AAA_C/compression-tool/src/gui/ade/explorer.py) `SilentExplorer` | ✅ 完成 | AC-UCB 算法：集群分配、UCB 分数、L1/L2 探索、预算控制、并发限制 |
| Python 样本采集 | [engine.py](file:///d:/AAA_C/compression-tool/src/gui/ade/engine.py) `collect_training_sample` | ✅ 完成 | FileRecord / DecisionResult → TrainingSample |
| Python 批量采集 | [engine.py](file:///d:/AAA_C/compression-tool/src/gui/ade/engine.py) `train_with_feedback` | ✅ 完成 | 批量样本采集 + 自动触发参数重训练 |
| Python 持久化 | [engine.py](file:///d:/AAA_C/compression-tool/src/gui/ade/engine.py) `save_training_data` | ✅ 完成 | 同时保存 V2 JSON 和 V3 JSONL 格式 |
| 探索数据回写 | [explorer.py](file:///d:/AAA_C/compression-tool/src/gui/ade/explorer.py) `_execute_explore_async` | ✅ 完成 | 探索结果写入 `TrainingDataStore` |

**完成度**: ~85%。核心采集/存储/探索闭环完整，缺少 `algorithm_type_from_v3` 辅助函数。

---

## 测试与构建

| 组件 | 文件路径 | 状态 | 说明 |
|------|---------|------|------|
| C++ 特征提取测试 | [tests/cpp/ade/test_feature_extractor.cpp](file:///d:/AAA_C/compression-tool/tests/cpp/ade/test_feature_extractor.cpp) | ✅ | 特征提取单元测试 |
| C++ 决策引擎测试 | [tests/cpp/ade/test_decision_engine.cpp](file:///d:/AAA_C/compression-tool/tests/cpp/ade/test_decision_engine.cpp) | ✅ | 决策引擎单元测试 |
| C++ 进化算法测试 | [tests/cpp/ade/test_evolutionary.cpp](file:///d:/AAA_C/compression-tool/tests/cpp/ade/test_evolutionary.cpp) | ✅ | 进化算法单元测试 |
| C++ CMakeLists | [CMakeLists.txt](file:///d:/AAA_C/compression-tool/src/ade/CMakeLists.txt) | ⚠️ 未包含 Stage2a | 未添加 `ParamRegressorNet` 编译目标 |
| PyBind11 绑定 | [pybind_ade.cpp](file:///d:/AAA_C/compression-tool/src/bindings/pybind/pybind_ade.cpp) | ⚠️ 未绑定 Stage2a | `init_ade` 中未包含 `ParamRegressorNet` 绑定 |

---

## 未完成事项清单

### P0 — 最紧急

| # | 模块 | 事项 | 文件 | 说明 |
|---|------|------|------|------|
| 1 | Stage2a | 创建 C++ 头文件 | `src/ade/include/ParamRegressorNet.hpp` | 定义 MLP 网络结构、训练/预测接口、数据类型 |
| 2 | Stage2a | 创建 C++ 实现 | `src/ade/models/ParamRegressorNet.cpp` | 实现网络前向传播、训练循环、预测、save/load |

### P1 — 高优先级

| # | 模块 | 事项 | 文件 | 说明 |
|---|------|------|------|------|
| 3 | Stage2a | 更新 CMakeLists.txt | `src/ade/CMakeLists.txt` | 添加 ParamRegressorNet 编译目标 |
| 4 | Stage2a | 更新 PyBind11 绑定 | `src/bindings/pybind/pybind_ade.cpp` | 暴露 `ParamRegressorNet` 给 Python |

### P2 — 中优先级

| # | 模块 | 事项 | 文件 | 说明 |
|---|------|------|------|------|
| 5 | Stage2a | 修复 Python `params.py` | `src/gui/ade/params.py` | 适配 C++ 后端，修复 `_has_cpp` 和循环调用 |
| 6 | Stage2a | 移植 NN 训练脚本 | `algorithm_branch → src/gui/ade/nn_train.py` | UI 触发的参数回归训练入口 |
| 7 | Stage1 | 移植 RF 训练脚本 | `algorithm_branch → src/gui/ade/rf_train.py` | UI 触发的随机森林训练入口 |
| 8 | Stage2b | 移植 EA 先验工具 | `algorithm_branch → src/gui/ade/ea_tune.py` | 从 JSONL 构建 EA 参数先验 |
| 9 | Collect | 添加辅助函数 | `src/gui/ade/training.py` | 添加 `algorithm_type_from_v3` |
| 10 | Collect | 移植重训练工具 | `algorithm_branch → src/gui/ade/retrain.py` | 参数回归数据注入 |

### P3 — 低优先级

| # | 模块 | 事项 | 文件 | 说明 |
|---|------|------|------|------|
| 11 | Stage0 | Python Extension 特征 | `src/gui/ade/features.py` | Python 侧缺少 Extension 特征提取器 |

---

## 关键文件清单

### C++ 侧 (`src/ade/`)

| 文件 | 状态 |
|------|------|
| `include/ADEBridge.hpp` | ✅ |
| `include/BaseFeatureExtractor.hpp` | ✅ |
| `include/CountMinSketch.hpp` | ✅ |
| `include/DecisionEngine.hpp` | ✅ |
| `include/EvolutionaryAlgorithms.hpp` | ✅ |
| `include/ExtensionExtractors.hpp` | ✅ |
| `include/FeatureExtractorV3.hpp` | ✅ |
| `include/FeatureVectorV3.hpp` | ✅ |
| `include/MagicBytesDetector.hpp` | ✅ |
| `include/RandomForest.hpp` | ✅ |
| **`include/ParamRegressorNet.hpp`** | ❌ **缺失** |
| `features/FeatureExtractorV3.cpp` | ✅ |
| `features/FeatureVector.cpp` | ✅ |
| `features/MagicBytesDetector.cpp` | ✅ |
| `models/EvolutionaryAlgorithms.cpp` | ✅ |
| `models/RandomForest.cpp` | ✅ |
| **`models/ParamRegressorNet.cpp`** | ❌ **缺失** |
| `runtime/ADEBridge.cpp` | ✅ |
| `runtime/DecisionEngine.cpp` | ✅ |
| `tools/train_model.cpp` | ✅ |
| `tools/generate_dataset.cpp` | ✅ |
| `CMakeLists.txt` | ⚠️ 缺 Stage2a |

### Python 侧 (`src/gui/ade/`)

| 文件 | 状态 |
|------|------|
| `__init__.py` | ✅ |
| `engine.py` | ✅ |
| `explorer.py` | ✅ |
| `features.py` | ⚠️ 缺 Extension |
| `params.py` | ⚠️ 缺 C++ 后端，unused |
| `training.py` | ⚠️ 缺辅助函数 |
| `types.py` | ✅ |
| **`rf_train.py`** | ❌ **缺失** |
| **`nn_train.py`** | ❌ **缺失** |
| **`ea_tune.py`** | ❌ **缺失** |
| **`retrain.py`** | ❌ **缺失** |

### PyBind11 (`src/bindings/pybind/`)

| 文件 | 状态 |
|------|------|
| `pybind_ade.cpp` | ⚠️ 缺 ParamRegressorNet 绑定 |

### 测试 (`tests/cpp/ade/`)

| 文件 | 状态 |
|------|------|
| `test_feature_extractor.cpp` | ✅ |
| `test_decision_engine.cpp` | ✅ |
| `test_evolutionary.cpp` | ✅ |
| **`test_param_regressor.cpp`** | ❌ 将在 Stage2a 实现后添加 |