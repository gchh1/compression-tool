# 技术债审计报告 v0

> 依据：`docs/engineering_standardization_directive.md`  
> 范围：阶段 0（只读审计 + 可确认安全项）  
> 日期：2026-05-10

## 1) 结构与仓库卫生

- 发现仓库中存在大量二进制/产物已被跟踪（`git ls-files "*.exe" "*.bin"`）：
  - `Package/bin/WebCompress.exe`
  - `bin/test_Deflate.exe` 等多项测试可执行文件
  - `assets/ade/default_model.bin`
- `.gitignore` 中存在 `*.exe/*.bin/*.wcx/*.restored` 忽略规则，但已被跟踪文件不会自动失效，导致仓库“历史污染持续”。

## 2) 契约一致性问题

- `CompressionStatus.SKIPPED` 在 `src/gui/ade/engine.py`（历史 `core/decision.py`）被使用，但原枚举未定义（审计时定位）：
  - `src/gui/ade/engine.py`：`record.status = CompressionStatus.SKIPPED`
  - `src/gui/models.py`：`CompressionStatus` 仅含 `PENDING/COMPRESSING/DONE/FAILED`（现已补齐为低风险修复）

## 3) 命名与语义漂移

- `dp_depth` 在不同实现层语义混用：
  - `src/core/include/LZDPCompressor.hpp`：`set_dp_depth -> dp_range_`
  - `src/core/include/DPFlateCompressor.hpp`：`set_dp_depth -> dp_sub_match_max_`
  - `src/bindings/pybind/pybind_module.cpp` 同时暴露 `set_dp_depth` 与 `set_dp_sub_match_max`
- 结果：对外同名参数在不同算法中含义不同，后续需进入“命名正名词典”统一。

## 4) 架构边界问题（待收敛）

- GUI 层调用核心引擎私有构造路径（已部分治理，但仍存在审计价值）：
  - 重点检查点：`src/gui/engine/compressor.py` 的 `_create_compressor` 与上层调用边界
- 目录语义偏差（已部分整改）：
  - 该层已从 `src/gui/web` 规范为 `src/gui/windows`，用于表达“窗口层”职责。
  - `src/gui/web_backup` 仍是历史备份目录，需后续清理策略。
- 说明：阶段 0 仅标注，不做高风险重构。

## 5) 文档与可维护性

- 根 `README.md` 仍偏历史说明，缺少现行“工程规约 + 构建矩阵 + 目录职责边界”正式版。
- 缺少统一的“路径迁移映射表 / 命名正名词典 / 重复代码消除清单”落地文件（本阶段开始补齐框架）。

## 6) 阶段 0 已执行低风险修复

- 修复：补齐 `CompressionStatus.SKIPPED` 枚举值，消除决策路径状态写入不一致问题。

## 7) 下一阶段建议

- 阶段 1：冻结规约与词典（不搬迁）。
- 阶段 2：先做“实体中心薄层”与接口契约收敛，再做目录大迁移。

