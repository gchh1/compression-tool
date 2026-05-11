# 重复代码消除清单 v0

阶段 0 仅登记，不做高风险改造。

| 位置 | 现状 | 风险 | 后续动作 |
|---|---|---|---|
| `src/gui/ui/main_window.py` 与 `src/gui/ui/legacy/main_window_legacy_backup.py` | 主文件与历史快照并存 | 易误改、误引用 | 已归档到 `ui/legacy/`，后续评估是否移出源码树 |
| `src/gui/windows/*` 与 `src/gui/web_backup/*` | 双份网页可视化脚本（现行 + 历史备份） | 维护成本高 | 阶段 3 统一保留策略 |
| DP 参数命名多义（`dp_depth`/`dp_sub_match_max`） | 同名异义 | 配置误用 | 阶段 1 词典冻结，阶段 2 改名落地 |

