# 工程标准化文档（索引）

本目录存放**治理类、门禁类、基线类**成文材料，与 `docs/engineering_standardization_directive.md`（任务书）配套使用。  
命名规则：**主题-用途.md**（kebab-case），便于排序与检索；历史文件名中的 `_v0` 已去掉，旧名可在 Git 历史中查看。

| 文档 | 用途 |
|------|------|
| [phase-0-governance-plan.md](phase-0-governance-plan.md) | 阶段 0 范围、产出与闸门（只读审计 + 低风险修复） |
| [build-baseline.md](build-baseline.md) | CMake / 打包 / 冒烟的最低可复现命令与验收底线 |
| [technical-debt-audit.md](technical-debt-audit.md) | 技术债与仓库卫生审计记录 |
| [risk-decisions.md](risk-decisions.md) | 高风险项 A/B 方案与决策留痕 |
| [path-migration-mapping.md](path-migration-mapping.md) | 历史路径 → 现行路径对照表 |
| [file-ownership-by-layer.md](file-ownership-by-layer.md) | 分层职责、文件归属与根目录允许内容 |
| [naming-dictionary.md](naming-dictionary.md) | 命名正名与别名对照 |
| [duplicate-code-tracker.md](duplicate-code-tracker.md) | 重复代码登记与后续动作 |
| [contribution-checklist.md](contribution-checklist.md) | 大整改期的可提交范围与排除项（PR/提交自检） |
| [commit-scope-phase-a-paths.txt](commit-scope-phase-a-paths.txt) | 建议 `git add` 路径清单（阶段 A 示例） |
