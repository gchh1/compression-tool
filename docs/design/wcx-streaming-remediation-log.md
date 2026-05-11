# WCX / 流式整改工作日志

本文档收录 **WCX 单协议化** 与 **流式 / GUI 相关** 的按日期整改条目。内容自 `streaming-workspace-spec.md` §18.5 拆分而来，便于主规格专注架构、状态机与验收标准。

## 文档关系

| 文档 | 职责 |
|------|------|
| `docs/design/streaming-workspace-spec.md` | 流式工作区、进度事件、GUI 契约、阶段里程碑与 **§18.4 阶段表** |
| 本文档 | **按日期** 的整改记录（改了什么、验证、风险） |
| `streaming-workspace-spec.md` §18.3 | 新条目的 **必填模板** |

---

## 按日期记录

#### [2026-05-11 16:22] C++ 流式文件接口接入 WCX 头（占位+回填）

- **阶段**: Phase 2
- **目标**: 先打通单协议路径，避免继续输出裸流
- **改动文件**:
  - `src/api/api.cpp`
- **接口/命名变更**:
  - 新增 WCX 头写入/解析辅助逻辑（位于 `api.cpp` 内，后续需抽到独立模块）
- **协议影响**:
  - 压缩输出新增 WCX 头
  - `compressed_size` 采用“先占位后回填”策略
  - 解压支持“优先 WCX，兼容旧裸流”过渡行为
- **验证**:
  - `core_engine` 构建通过
- **风险与回滚**:
  - 风险: 当前实现仍在 `api.cpp`，模块边界未完全收敛
  - 回滚: 可暂时回退到旧 `compressFile/decompressFile` 路径
- **结论**: 部分完成（已打通，待模块化与测试补齐）

#### [2026-05-11 16:28] WCX 协议模块化与命名规范化

- **阶段**: Phase 2
- **目标**: 将 `api.cpp` 内联 WCX 逻辑抽离为独立模块，落实“规范命名，不只是重整”
- **改动文件**:
  - `src/api/include/WCXProtocol.hpp`（新增）
  - `src/api/WCXProtocol.cpp`（新增）
  - `src/api/api.cpp`
  - `src/api/CMakeLists.txt`
- **接口/命名变更**:
  - `to_wcx_algo_code` -> `wcx::toAlgoCode`
  - `write_wcx_header` -> `wcx::writeHeader`
  - `try_read_wcx_header` -> `wcx::tryReadHeader`
  - 统一类型命名：`wcx::HeaderView`
- **协议影响**:
  - 无字段变更，仅实现位置收敛
  - WCX 头写入与解析仍保持兼容
- **验证**:
  - `cmake --build . --target core_engine` 通过
- **风险与回滚**:
  - 风险: 目前仍保留“非 WCX 输入回退裸流”兼容分支，后续需按治理计划逐步收口
  - 回滚: 可回退到单文件 `api.cpp` 实现
- **结论**: 完成（模块化已落地，下一步进入跨语言互读测试）

#### [2026-05-11 16:32] 跨语言互读验证（C++ <-> Python）

- **阶段**: Phase 2
- **目标**: 验证单协议 `wcx` 在 C++ 与 Python 路径间互读可用
- **改动文件**:
  - 无代码结构改动（测试验证）
- **验证用例**:
  1. C++`pipeline_compress_file` 生成 `.wcx`，Python `unpack_compressed_file` 解析头与 payload，随后 C++ `pipeline_decompress`(INFLATE) 解回原文。
  2. Python `pack_compressed_file` 生成 `.wcx`，C++ `pipeline_decompress_file`(INFLATE) 解压到文件并比对原文。
- **验证结果**:
  - 用例 1 通过：`A_cpp_to_py_header True 90000 208 208 True True`
  - 用例 2 通过：`B_py_to_cpp_file True 90000 True`
- **风险与回滚**:
  - 风险: 当前兼容逻辑仍保留“非 WCX 输入回退裸流”，后续阶段需收敛策略
  - 回滚: 保持现有兼容分支即可，不影响当前互读通过结论
- **结论**: 完成（单协议互读已跑通）

#### [2026-05-11 16:37] Python 主写头路径切换到 C++ WCX API

- **阶段**: Phase 3
- **目标**: 让 GUI 侧 `file_protocol.py` 优先使用 C++ WCX 读写，Python 逻辑降级为 fallback
- **改动文件**:
  - `src/api/include/api.hpp`
  - `src/api/api.cpp`
  - `src/api/include/WCXProtocol.hpp`
  - `src/api/WCXProtocol.cpp`
  - `src/bindings/pybind/pybind_module.cpp`
  - `src/gui/engine/file_protocol.py`
- **接口/命名变更**:
  - C++ API 新增：
    - `pack_wcx(...)`
    - `unpack_wcx(...)`
  - Python 绑定新增：
    - `core_engine.pack_wcx(...)`
    - `core_engine.unpack_wcx(...)`
  - Python `pack_compressed_file/unpack_compressed_file` 改为优先调用 C++ API
- **协议影响**:
  - 字段定义未变（仍是 WCX v2）
  - 主要变化是“协议写入责任”从 Python 主路径迁移到 C++ 主路径
- **验证**:
  - `cmake --build . --target core_engine` 通过
  - 冒烟测试通过：`pack_compressed_file` + `unpack_compressed_file` 往返一致
- **风险与回滚**:
  - 风险: 目前 `AlgorithmType` 到 `AlgorithmID` 映射仅覆盖已接入算法；未覆盖项走 fallback
  - 回滚: Python 保留原实现，可立即回退到纯 Python 路径
- **结论**: 完成（Phase 3 已有实质进展）

#### [2026-05-11 16:41] 检测路径统一到 WCX 公共解析入口

- **阶段**: Phase 3
- **目标**: 清理 GUI 侧仍直接调用 `CompressedFileHeader.from_bytes` 的分支，减少协议实现分叉
- **改动文件**:
  - `src/gui/engine/file_protocol.py`
- **接口/命名变更**:
  - `detect_algorithm_from_file` 从“直接解头”改为复用 `unpack_compressed_file` 公共入口
- **协议影响**:
  - 无字段变更
  - 算法检测路径与主解析路径一致，优先走 C++ `unpack_wcx`（有引擎时）
- **验证**:
  - 冒烟测试通过：`detect_algorithm_from_file` 能正确识别 `AlgorithmType.DEFLATE`
- **风险与回滚**:
  - 风险: 若 `unpack_wcx` 异常将走 Python fallback，行为与之前兼容
  - 回滚: 可恢复原 `from_bytes` 调用
- **结论**: 完成（统一解析入口进一步收敛）

#### [2026-05-11 16:42] 增加 WCX 严格模式（禁用 Python fallback）

- **阶段**: Phase 3
- **目标**: 提供 CI/联调可用的强约束模式，确保 WCX 主路径必须经过 C++ 实现
- **改动文件**:
  - `src/gui/engine/file_protocol.py`
- **接口/命名变更**:
  - 新增环境变量开关：`WCX_STRICT_CPP`
  - 开启时：
    - `pack_compressed_file` 若无法走 C++ `pack_wcx` 立即报错
    - `unpack_compressed_file` 若无法走 C++ `unpack_wcx` 立即报错
- **协议影响**:
  - 无字段改动，仅调用路径约束增强
- **验证**:
  - 本地严格模式冒烟通过：`strict_ok AlgorithmType.DEFLATE 500 500 500 True`
- **风险与回滚**:
  - 风险: 在无 `core_engine` 环境下开启严格模式会直接失败（符合预期）
  - 回滚: 关闭环境变量 `WCX_STRICT_CPP` 即恢复兼容模式
- **结论**: 完成（为 Phase 3 收敛提供强约束工具）

#### [2026-05-11 16:44] 严格模式接入 GUI 配置与测试默认值

- **阶段**: Phase 3
- **目标**: 避免只能手工设环境变量，支持通过配置启用严格模式；同时让 Python 测试默认走严格路径
- **改动文件**:
  - `src/gui/config/settings.py`
  - `src/gui/main.py`
  - `tests/python/conftest.py`
- **接口/命名变更**:
  - 新增配置键：`streaming.wcx_strict_cpp`（默认 `false`）
  - 新增配置读取函数：`get_wcx_strict_cpp(config)`
  - GUI/CLI 启动时若配置开启，则自动设置 `WCX_STRICT_CPP=1`
  - Python 测试会话默认设置 `WCX_STRICT_CPP=1`（可用 `WCX_STRICT_CPP_DISABLE` 关闭）
- **协议影响**:
  - 无字段变更，仅执行路径控制增强
- **验证**:
  - 本地冒烟通过：配置默认值与环境变量解析正确
  - `pytest` 不可用（当前环境缺少 `pytest` 包），已记录为环境限制
- **风险与回滚**:
  - 风险: 若测试环境无 `core_engine` 且启用严格模式，相关协议路径会快速失败（符合预期）
  - 回滚: 配置中关闭 `wcx_strict_cpp`，或设置 `WCX_STRICT_CPP_DISABLE=1`
- **结论**: 完成（严格模式已从“手工开关”升级为“配置可控 + 测试默认”）

#### [2026-05-11 16:46] 算法配置对话框：流式页增加 WCX 严格模式与分块持久化

- **阶段**: Phase 3
- **目标**: 用户可在 GUI「算法配置 → 流式设置」中开关 WCX 严格模式，并把 `chunk_size_kb` 与 `wcx_strict_cpp` 写入配置文件；应用后立即同步 `WCX_STRICT_CPP` 环境变量
- **改动文件**:
  - `src/gui/ui/main_window.py`
- **接口/命名变更**:
  - 流式分块初值改为读取 `get_streaming_chunk_size(load_config())`（不再仅用常量）
  - 「应用」时合并写入 `streaming.chunk_size_kb` 与 `streaming.wcx_strict_cpp`
- **协议影响**:
  - 无
- **验证**:
  - 静态检查：`main_window.py` 无新增 linter 报错
- **风险与回滚**:
  - 风险: 无 C++ 引擎时勾选严格模式会导致后续 WCX 打包失败（与既有设计一致）
  - 回滚: 取消勾选并应用，或编辑配置文件关闭 `wcx_strict_cpp`
- **结论**: 完成（GUI 可见可控）

#### [2026-05-11 16:48] 算法对比任务使用配置中的流式分块大小

- **阶段**: Phase 3
- **目标**: 消除 `ComparisonWorker` 等处硬编码 `STREAMING_CHUNK_SIZE_KB`，与「流式设置」中持久化的 `chunk_size_kb` 一致
- **改动文件**:
  - `src/gui/ui/main_window.py`
- **接口/命名变更**:
  - `_start_comparison_worker` 使用 `get_streaming_chunk_size(load_config())`
  - 移除 `AlgorithmConfigDialog._setup_ui` 内未使用的 `gui.models` 常量导入
- **协议影响**:
  - 无
- **验证**:
  - linter：`main_window.py` 无新增问题
- **结论**: 完成

#### [2026-05-11] WCX `algo_code` 补齐 Brotli/Zstd 并与 C++ 映射一致

- **阶段**: Phase 1（协议基线）
- **目标**: 消除 Brotli/Zstd 在 Python 头中落到 code `0`、以及 C++ `toAlgoCode` 将二者写成 `0` 的不一致；解压侧可按字节还原 GUI 算法类型
- **改动文件**:
  - `src/api/WCXProtocol.cpp`
  - `src/gui/engine/file_protocol.py`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 新增 WCX 算法字节 **7=Brotli**、**8=Zstd**；C++ 对解压类 `AlgorithmID` 与压缩类映射到同一字节
- **协议影响**:
  - 新产生的 WCX 在 Brotli/Zstd 场景下 `algo_code` 与旧行为不同（旧实现误为 `0`）；读侧已支持 7/8
- **验证**:
  - `cmake --build build --target core_engine`（或等价目标）通过
- **结论**: 完成

#### [2026-05-11 19:26] 目录归档接口接入 WCX 外层头（文件夹标记）

- **阶段**: Phase 2（C++ 侧对齐 WCX 读写）
- **目标**: 消除 `compressDirectory`/`decompressAndUnpackToDisk` 与单文件 WCX 路径分裂，目录归档输出也统一为 WCX 容器
- **改动文件**:
  - `src/api/api.cpp`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - `compressDirectory` 写入 WCX 头（`FLAG_FOLDER=1`）并回填 `compressed_size`
  - `decompressAndUnpackToDisk` 先尝试 `unpack_wcx` 获取 payload，再交给 `PackReader`；失败时回退旧裸 Pack 数据兼容
- **协议影响**:
  - 新生成目录归档默认带 WCX 头（`is_folder=true`），可统一走 WCX 识别
  - 兼容策略: 解包仍兼容历史裸 Pack 文件
- **验证**:
  - `cmake --build build --target core_engine` 通过
- **结论**: 完成

#### [2026-05-11] 目录 WCX + 裸 Pack 回退：C++ 回归测试

- **阶段**: Phase 4（回归与兼容测试）
- **目标**: 将目录归档 WCX 化与裸 Pack 兼容路径纳入可重复构建验证
- **改动文件**:
  - `tests/test_wcx_directory_archive.cpp`（新增）
  - `tests/CMakeLists.txt`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 无
- **协议影响**:
  - 无
- **验证**:
  - `cmake --build build --target test_wcx_directory_archive` 通过；运行 `build/bin/test_wcx_directory_archive` 退出码 0
- **结论**: 完成

#### [2026-05-11 19:31] 目录解包新增严格模式开关（收口裸 Pack 回退）

- **阶段**: Phase 5（删除旧路径与规则固化）/ Phase 4（兼容验证）
- **目标**: 在不破坏默认兼容的前提下，为目录解包提供可配置的“仅接受 WCX 容器”行为，便于 CI 和灰度收口
- **改动文件**:
  - `src/api/api.cpp`
  - `tests/test_wcx_directory_archive.cpp`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 新增环境变量 `WCX_STRICT_ARCHIVE`（`1/true/yes/on` 生效）
  - `decompressAndUnpackToDisk` 在开关生效时，若 `unpack_wcx` 失败则直接报错，不再回退裸 Pack
- **协议影响**:
  - 默认（开关关闭）无行为变化，保持历史兼容
  - 开关开启后，非 WCX 输入会被拒绝，作为迁移收口策略
- **验证**:
  - `test_wcx_directory_archive` 覆盖：默认模式下裸 Pack 可解包；`WCX_STRICT_ARCHIVE=1` 下裸 Pack 被拒绝
  - 构建与测试通过
- **结论**: 完成

#### [2026-05-11 19:35] GUI/CLI 接入 `WCX_STRICT_ARCHIVE` 配置

- **阶段**: Phase 5（删除旧路径与规则固化）
- **目标**: 将目录解包严格模式从“仅环境变量可用”提升为配置可控，保证 GUI/CLI 与 C++ 行为一致
- **改动文件**:
  - `src/gui/config/settings.py`
  - `src/gui/main.py`
  - `src/gui/ui/main_window.py`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 配置新增 `streaming.wcx_strict_archive`（默认 `false`）
  - 新增 `get_wcx_strict_archive()`
  - GUI「算法配置 -> 流式设置」新增复选框“归档解包仅接受 WCX 容器（禁用裸 Pack 回退）”
  - GUI/CLI 启动按配置设置 `WCX_STRICT_ARCHIVE` 环境变量
- **协议影响**:
  - 无字段变化；仅行为开关统一到配置层
- **验证**:
  - IDE lints：`settings.py`、`main.py`、`main_window.py` 无新增报错
- **结论**: 完成

#### [2026-05-11] pytest 默认启用 `WCX_STRICT_ARCHIVE`

- **阶段**: Phase 4（回归与兼容测试）
- **目标**: 与 `WCX_STRICT_CPP` 一致，让 Python 测试会话默认走目录归档严格策略；需要调查裸 Pack 时用显式关闭开关
- **改动文件**:
  - `tests/python/conftest.py`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - `pytest_sessionstart`：若未设置 `WCX_STRICT_ARCHIVE` 且未设置 `WCX_STRICT_ARCHIVE_DISABLE`，则写入 `WCX_STRICT_ARCHIVE=1`
- **协议影响**:
  - 无
- **验证**:
  - 静态检查：`conftest.py` 无 linter 问题
- **结论**: 完成

#### [2026-05-11] GitHub Actions：C++ 目录 WCX 回归

- **阶段**: Phase 4（回归与兼容测试）
- **目标**: 仓库内落地可重复的 CI：配置、构建并运行目录 WCX 回归用例
- **改动文件**:
  - `.github/workflows/cpp.yml`（新增）
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 无
- **协议影响**:
  - 无
- **验证**:
  - 工作流步骤：`cmake -DBUILD_PYTHON=OFF` → 构建目标 → 运行二进制
  - Job 级 `env` 设置 `WCX_STRICT_CPP` / `WCX_STRICT_ARCHIVE` 与 pytest 默认策略一致
- **结论**: 完成

#### [2026-05-11] CMake 注册 PackWriter 测试并扩展 CI

- **阶段**: Phase 4（回归与兼容测试）
- **目标**: 将已有源码 `test_PackWriter*.cpp` 纳入默认构建与 GitHub Actions；明确排除 `test_CompressFile`（1GB）不进 CI
- **改动文件**:
  - `tests/CMakeLists.txt`
  - `tests/test_PackWriter.cpp`（补 `#include "Pipeline.hpp"` 以满足 `unique_ptr<Pipeline>` 完整析构）
  - `.github/workflows/cpp.yml`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 无
- **协议影响**:
  - 无
- **验证**:
  - 本地构建并运行：`test_wcx_directory_archive`、`test_PackWriter_RoundTrip`、`test_PackWriter` 通过
- **结论**: 完成

#### [2026-05-11] `decompressFile` 严格模式收口 + 新增文件级回归

- **阶段**: Phase 5（删除旧路径与规则固化）/ Phase 4（回归与兼容测试）
- **目标**: 让 `decompressFile` 与目录解包路径一致：开启 `WCX_STRICT_ARCHIVE` 时禁止“非 WCX 裸流回退”
- **改动文件**:
  - `src/api/api.cpp`
  - `tests/test_wcx_file_strict.cpp`（新增）
  - `tests/CMakeLists.txt`
  - `.github/workflows/cpp.yml`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - `decompressFile`：非 WCX 输入在 `WCX_STRICT_ARCHIVE=1` 时直接失败
  - 新增测试目标：`test_wcx_file_strict`
- **协议影响**:
  - 默认兼容模式无变化
  - 严格模式下单文件解压与目录解包策略统一（仅接受 WCX）
- **验证**:
  - 本地构建并运行：`test_wcx_file_strict`、`test_wcx_directory_archive`、`test_PackWriter_RoundTrip`、`test_PackWriter` 全部通过
  - CI 工作流已加入 `test_wcx_file_strict`
- **结论**: 完成

#### [2026-05-11] 兼容回退路径增加显式告警日志

- **阶段**: Phase 5（删除旧路径与规则固化）
- **目标**: 为“默认兼容模式”下的裸流回退提供可见信号，便于统计调用来源并推进后续删除
- **改动文件**:
  - `src/api/api.cpp`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 无
- **协议影响**:
  - 无；仅新增 stderr 告警：
    - `decompressFile` 回退裸流时输出 `[wcx][compat] ...`
    - `decompressAndUnpackToDisk` 回退裸 Pack 时输出 `[wcx][compat] ...`
- **验证**:
  - 运行 `test_wcx_file_strict` / `test_wcx_directory_archive` 可观察到兼容回退日志
  - 严格模式测试仍通过
- **结论**: 完成

#### [2026-05-11] CI 增加 strict-no-compat gate（M2 预演）

- **阶段**: Phase 5（删除旧路径与规则固化）/ Phase 4（回归与兼容测试）
- **目标**: 在 CI 中显式验证“严格模式正常路径不触发兼容回退日志”，为 M2 默认切换做预演门禁
- **改动文件**:
  - `tests/test_wcx_strict_no_compat.cpp`（新增）
  - `tests/CMakeLists.txt`
  - `.github/workflows/cpp.yml`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 新增测试目标：`test_wcx_strict_no_compat`
  - 新增 CI job：`strict-no-compat-gate`
- **协议影响**:
  - 无
- **验证**:
  - 本地执行 `test_wcx_strict_no_compat`，输出不含 `[wcx][compat]`
  - gate 逻辑：运行日志若匹配 `[wcx][compat]` 即失败
- **结论**: 完成

#### [2026-05-11] M3 落地：GUI/CLI 默认切换 strict

- **阶段**: Phase 5（删除旧路径与规则固化）
- **目标**: 将默认行为从“兼容优先”切换为“strict 优先”，legacy 回退仅通过显式关闭开关启用
- **改动文件**:
  - `src/gui/config/settings.py`
  - `src/gui/ui/main_window.py`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 默认配置变更：
    - `streaming.wcx_strict_cpp`: `false -> true`
    - `streaming.wcx_strict_archive`: `false -> true`
  - 算法配置对话框“恢复默认”同步改为勾选两项 strict 开关
- **协议影响**:
  - 无字段变化；仅默认执行策略切换
- **验证**:
  - IDE lints：`settings.py`、`main_window.py` 无新增报错
- **结论**: 完成

#### [2026-05-11] M4 起步：legacy 兼容回退集中到 adapter 辅助函数

- **阶段**: Phase 5（删除旧路径与规则固化）
- **目标**: 将 `decompressFile` / `decompressAndUnpackToDisk` 的 WCX 与兼容回退分支收敛到单一入口，便于后续删除与审计 `[wcx][compat]` 日志来源
- **改动文件**:
  - `src/api/api.cpp`
  - `src/api/wcx_legacy_compat.cpp`
  - `src/api/include/wcx_legacy_compat.hpp`
  - `src/api/CMakeLists.txt`
  - `docs/design/streaming-workspace-spec.md`（阶段表见主规格 §18.4）
- **接口/命名变更**:
  - 新增翻译单元：`wcx_legacy_compat`（`resolveDecompressFilePayload`、`resolveDirectoryUnpackPackBytes`）
- **协议影响**:
  - 无
- **验证**:
  - 构建：`api` 与 `test_wcx_file_strict`、`test_wcx_directory_archive`、`test_wcx_strict_no_compat` 通过
- **结论**: 完成

#### [2026-05-11] 解码侧输入模块重命名（`wcx_legacy_compat` → `wcx_decompress_input`）

- **阶段**: Phase 5（读路径收口与可审计性）
- **目标**: 去掉「遗留 / legacy」语义误导；该翻译单元只负责**解压链路**上识别 WCX 容器与迁移期裸流/裸 Pack，并与 `WCX_STRICT_ARCHIVE` 对齐。写路径（`pack_wcx`、目录 WCX 写）已在 C++/主路径，不在此文件。
- **改动文件**:
  - `src/api/wcx_decompress_input.cpp`（重命名自 `wcx_legacy_compat.cpp`）
  - `src/api/include/wcx_decompress_input.hpp`（重命名自 `wcx_legacy_compat.hpp`）
  - `src/api/api.cpp`（调用点改为 `resolve_wcx_file_stream_payload_length`、`resolve_wcx_directory_archive_inner_pack`）
  - `src/api/CMakeLists.txt`
  - `docs/design/wcx-streaming-remediation-log.md`（本条目）
- **接口/命名变更**:
  - `resolveDecompressFilePayload` → `resolve_wcx_file_stream_payload_length`
  - `resolveDirectoryUnpackPackBytes` → `resolve_wcx_directory_archive_inner_pack`
- **协议影响**: 无
- **验证**: 本地 `cmake --build … --target api` 与 WCX 三测通过（与重命名前等价）
- **结论**: 完成

#### [2026-05-11] 移除 WCX 读路径兼容回退（裸流 / 裸 Pack / Python 头回退）

- **阶段**: Phase 5
- **目标**: 产品策略改为**仅支持当前 WCX 单协议**；不再为历史 on-disk 形态保留静默回退或 GUI「关闭严格」开关。
- **改动文件**:
  - `src/api/wcx_decompress_input.cpp` / `include/wcx_decompress_input.hpp`（仅 WCMP；失败即返回错误）
  - `src/gui/engine/file_protocol.py`（`pack_wcx` / `unpack_wcx` 唯一路径；无 `core_engine` 即报错）
  - `src/gui/config/settings.py`、`src/gui/main.py`、`src/gui/ui/main_window.py`（删除 `WCX_STRICT_*` 配置与复选框）
  - `tests/test_wcx_*.cpp`、`tests/python/conftest.py`、`.github/workflows/cpp.yml`
  - `docs/design/streaming-workspace-spec.md`（里程碑 M1–M4 表述）
- **接口/命名变更**:
  - 删除环境变量 `WCX_STRICT_ARCHIVE` / `WCX_STRICT_CPP` 的语义依赖（C++ 解压侧不再读取）
  - 删除 CI `strict-no-compat-gate` job（已无 `[wcx][compat]` 回退日志可扫）
- **协议影响**: 旧裸压缩文件 / 裸 Pack **不再可读**；用户需用迁移工具重包（若日后提供）
- **验证**: `api` + `test_wcx_*` 构建运行通过
- **结论**: 完成

#### [2026-05-11] 主规格：WCX v2 全头字节布局 + §18.4 与里程碑对齐当前实现

- **阶段**: Phase 1 / 文档治理（跨 Phase 5 表述修正）
- **目标**: 补齐 Phase 1 待办的「字节级全头布局」；将 §18.4 阶段表与 §15.2 M2 表述与当前「仅 WCX、无 WCX_STRICT_* 产品开关」实现对齐，避免读者误以为仍存在 legacy 回退开关。
- **改动文件**:
  - `docs/design/streaming-workspace-spec.md`（新增 §16.2.1.1 布局表；更新 §16.2.1 头说明；§15.2 M2/M4 措辞；§18.4 五行备注）
- **接口/命名变更**: 无（仅文档）
- **协议影响**: 否（文档与既有 `WCXProtocol` 一致化表述）
- **验证**: 人工对照 `WCXProtocol.cpp` 偏移与 `patchCompressedSize` seek 位置
- **风险与回滚**: 无；回滚为还原该 MD  diff
- **结论**: 完成

#### [2026-05-11] decompressFile：声明 payload 与截断校验；新增 test_wcx_corrupt

- **阶段**: Phase 4（损坏输入）；仓库卫生与 CONTRIBUTING
- **目标**: 修复 `decompressFile` 在 payload 短于头中 `compressed_size` 或读中断时仍 `success=true` 的问题；为错误 magic、声明过大、物理截断提供 CI 覆盖；修正 CONTRIBUTING 对已删治理文件的引用；忽略本地 `build_ci_check` 等目录。
- **改动文件**:
  - `src/api/api.cpp`（头后剩余字节校验；读循环后 `bytes_read == payload_size`）
  - `tests/test_wcx_corrupt.cpp`、`tests/CMakeLists.txt`
  - `.github/workflows/cpp.yml`
  - `.gitignore`、`CONTRIBUTING.md`
  - `docs/design/streaming-workspace-spec.md`（§18.4 Phase 4 备注）
- **协议影响**: 否（仅失败路径更严格）
- **验证**: 构建并运行 `test_wcx_corrupt` 及既有 `test_wcx_*`
- **结论**: 完成

#### [2026-05-11] unpack_wcx：按 compressed_size 定界 payload；拒绝截断与对齐 decompressFile

- **阶段**: Phase 4
- **目标**: 内存路径 `unpack_wcx` 原先把 `header.total_size` 之后直到 buffer 末尾一律当作 payload，截断文件仍 `success`，与 `decompressFile` 不一致。改为要求 `data.size() >= total_size + compressed_size`，payload 恰为 `compressed_size` 字节；允许 buffer 尾部附加无关字节时忽略尾段。
- **改动文件**:
  - `src/api/api.cpp`（`unpack_wcx`）
  - `tests/test_wcx_corrupt.cpp`（`unpack_wcx` 截断失败、尾字节忽略、`payload.size()==compressed_size`）
- **协议影响**: 否（与头字段语义一致；此前将尾字节误并入 payload 属宽松 bug）
- **验证**: `test_wcx_corrupt`、`test_wcx_directory_archive` 等通过
- **结论**: 完成

#### [2026-05-11] §18.4 阶段表与「WCX 主线已完成」体感对齐

- **阶段**: 文档
- **目标**: 将 `streaming-workspace-spec.md` §18.4 中仍一律「进行中」的表述，改为与仓库现状一致：Phase 1/3/5 标已完成，Phase 4 标部分完成，Phase 2 区分产品路径与 archiver 架构债。
- **改动文件**: `docs/design/streaming-workspace-spec.md`（§18.4）
- **协议影响**: 无
- **结论**: 完成

#### [2026-05-11] 流式分块：GUI `chunk_size_kb` 贯通 C++ pipeline 文件 API

- **阶段**: Phase 3 / 流式产品
- **目标**: C++ `compressFile`/`decompressFile`/`compressDirectory` 原固定 1 MiB 读缓冲，与「流式分块大小」配置脱节。增加可选 `stream_chunk_bytes`（0=默认 1 MiB，钳位 64 KiB～64 MiB）；pybind 与 `CompressionEngine.smart_compress_file`/`smart_decompress_file` 从 `get_streaming_chunk_size` 传入。
- **改动文件**:
  - `src/api/api.cpp`、`src/api/include/api.hpp`
  - `src/bindings/pybind/pybind_module.cpp`
  - `src/gui/engine/compressor.py`、`src/gui/ui/main_window.py`
  - `docs/design/streaming-workspace-spec.md`（§6）
- **协议影响**: 无
- **验证**: 构建 `api` 与 `test_wcx_*`；重编 `core_engine` 后验证 GUI 大文件流式
- **结论**: 完成

