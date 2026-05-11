# 命名正名词典 v0

| 旧名 | 建议新名 | 作用域 | 状态 | 备注 |
|---|---|---|---|---|
| `dp_depth` (DPFlate) | `dp_sub_match_max` | GUI 配置键 / pybind 暴露 | 待迁移 | 与 LZDP 的 `dp_depth` 语义冲突 |
| `dp_depth` (LZDP) | `dp_depth`（保留） | LZDP 参数 | 保留 | 语义为 DP range/depth |
| `strategy.py`（历史） | `decision.py` | GUI 决策层 | 已替换 | 已存在删除记录 |

> 说明：本文件在阶段 1 冻结，阶段 2 起按模块执行改名并更新兼容策略。

