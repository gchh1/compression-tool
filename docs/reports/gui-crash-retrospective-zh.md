# GUI 压缩「闪退」问题 — 分析汇报（回顾）

本文档汇总针对 **非流式 / 内存路径** 压缩（尤其 DEFLATE 等）时 **进程异常退出或任务无干净收尾** 的调查结论、根因假设与已落地修复，供评审与发布核对使用。

---

## 1. 现象与范围

| 项目 | 说明 |
|------|------|
| **用户感知** | 对特定文件（例如大型 `.ipynb`）选择 **非流式** 或走 **内存压缩** 时，界面 **闪退** 或压缩流程 **突然中断**，日志中缺少预期的「压缩完成」类记录。 |
| **对比** | 同文件在 **流式（文件到文件）** 路径下有时可正常完成，与 **内存路径** 行为不一致。 |
| **证据来源** | `Package/logs/gui.log`：可区分 `STREAMING` 与 `loaded raw data` / `compressing with ...` 等分支；异常路径常表现为 **在 `smart_compress` 之后缺少后续日志**。 |

---

## 2. 调查结论（分层根因）

### 2.1 绑定层：Python `bytes` 与 C++ 重载不匹配（高置信）

- **现象（早期日志）**  
  调用具体压缩器（如 `DPFlateCompressor.compress`）传入 **`bytes`** 时，曾出现：  
  `TypeError: compress(): incompatible function arguments`  
  即 pybind11 未选中期望的 **buffer** 入口，仍绑定到 **`std::vector<uint8_t>`** 等不匹配签名的重载。

- **机制**  
  若在抽象基类（如 `ICompressor`）上注册了基于 **buffer** 的 `compress`/`decompress` 包装，而 **具体子类在 C++ 中重写了同名方法**，则 Python 侧可能仍解析到子类的 **非 buffer** 重载，导致 **`bytes` 无法传入**。

- **修复方向（C++ / pybind）**  
  在各 **具体压缩器** 绑定末尾补充与基类一致的 **`py::buffer` → `buffer_to_u8vec`** 的 `compress`/`decompress`（及个别如 **`LZDPCompressor.get_dp_visualization`** 的 buffer 形参）重载，并 **重新编译** `core_engine`。  
  涉及类型包括但不限于：`DeflateCompressor`、`LZSSCompressor`、`LZDPCompressor`、`DPFlateCompressor`、`GzipCompressor`、`BrotliCompressor`、`ZstdCompressor`（以仓库当前 `pybind_module.cpp` 为准）。

### 2.2 打包与运行环境：开发态与安装包 pyd 不一致（中高置信）

- **现象**  
  在 **`PYTHONPATH` 指向本地 `build_py` / `build_debug` 产物** 下，对同一大小的 notebook 做 `compress` / `smart_compress` **可成功**；而 **PyInstaller 打包的 `WebCompress.exe`** 从 **`_MEIPASS/.../core_engine/`** 加载的 **可能是旧版 `.pyd`**，未包含上述 buffer 修复时，仍会在内存路径触发绑定错误或原生层异常。

- **建议**  
  每次发版明确：**将最新构建的 `core_engine*.pyd`（及 MinGW 等依赖 DLL，若需要）同步进 Package / 安装包**，并与 CI 或构建脚本对齐，避免「源码已修、包体仍旧」的漂移。

### 2.3 GUI 工作线程：`bytes(result.data)` 与巨型 `list`（中置信）

- **风险**  
  若绑定层在某种配置下仍把压缩结果暴露为 **巨大的 Python `list[int]`**（历史 STL 容器绑定形态），则执行 **`bytes(result.data)`** 会在 Python 层构造 **体量极大的临时对象**，表现为 **内存尖峰、卡顿或进程被系统终止**，用户侧亦可能描述为「闪退」。

- **修复方向（Python）**  
  在 `CompressionWorker` 内存路径中：  
  - 对 **`bytes`/`bytearray`/`memoryview`** 避免不必要的再包装；  
  - 若检测到 **`list` 形载荷** 且存在可用磁盘路径，则 **记录告警并回退到与流式一致的文件到文件**（`smart_compress_file` + WCX 工作区路径），避免在 Python 层对整表做 `bytes(list)`。

### 2.4 其它已关联修复（上下文）

- **`unpack_wcx` / 管线 / `Archiver.unpack`**：对 **`bytes`** 等 buffer 的统一接受（`buffer_to_u8vec`），避免「仅接受 vector 重载」导致的解压入口失败。  
- **`bridge.py`**：优先解析 **`build_py` / `build_debug`** 下的 pyd，减少开发机「误以为已加载新核心」的路径歧义。  
- **解压策略与写入**：与 **MemoryPool** 仅用于管线内部等设计约束**相关的注释/文档**已对齐，避免误用 API 引发不稳定（详见对应设计/修复提交）。

### 2.5 ADE 接入 GUI 压缩/解压：潜在不稳定点（中置信，架构级）

> **结论摘要**：解压主路径**不调用** `DecisionEngine` / `dec.decide`，ADE **不是**「解压逻辑本身」的必需依赖；但 ADE 相关的 **后台线程 + 全局 `CompressionEngine` 配置** 与 **AUTO 路径上的大内存复制**，曾在「压缩进行中 / 刚结束」与「主线程解压」交错时构成 **配置竞态** 风险面。已在 `compressor.py` 用 **`RLock`** 串行化全局配置与依赖该配置的 native 压/解压入口，并与 **默认关闭静默探索** 组合，从接入层逼近「不因 ADE 交错写配置而闪退」（见 §2.6 对「绝对保证」边界的说明）。

#### （1）静默探索线程与全局引擎配置（竞态）

- **位置**：`CompressionWorker` 在流式/内存压缩成功后会调用 `SilentExplorer.get().maybe_explore(...)`（`src/gui/ui/worker.py`）。  
- **机制**：`SilentExplorer._execute_explore_async` 在 **独立 `threading.Thread`** 中执行，流程为：  
  `original_config = CompressionEngine.get_config()` → `CompressionEngine.set_config(full_config, save=False)` → `engine.compress(record.raw_data, target_algo)` → `finally: CompressionEngine.set_config(original_config, save=False)`（`src/gui/ade/explorer.py`）。  
- **风险**：`CompressionEngine._config` 为 **进程级单例可变状态**。与 **`CompressionWorker`（`QThread`）** 内同一时刻的 **`smart_compress` / `smart_compress_file`**、以及 **主线程** `_on_decompress` 里对引擎的调用 **无统一互斥锁**。交错时可能出现：  
  - 探索线程 `set_config` 与业务压缩/解压读取的配置不一致，**native 侧读到半更新状态**（是否崩溃取决于 C++ 实现是否假设单线程）；  
  - `finally` 恢复的配置快照与当前任务期望不一致，**覆盖** AUTO 刚写入的配置。  
- **与解压的关系**：解压在主线程调用 `CompressionEngine`；若此时探索线程正在 `set_config`，同属 **未定义行为** 风险面（概率依赖探索触发频率与文件大小）。

#### （2）AUTO 路径修改全局配置且与探索「快照恢复」叠加

- **位置**：AUTO 时在 worker 内 `CompressionEngine.set_config(full_cfg, save=False)` 合并 `_auto_params`（`worker.py`），**不会在单次任务末尾显式恢复**到压缩前的全局配置（设计意图可能是持久化用户态配置，但与探索线程的「快照→恢复」模型叠加后，**顺序依赖**更强）。  
- **风险**：多任务、多线程下更难推理「某一时刻全局配置到底是谁写的」，放大（1）中的竞态与误配参进 native 的可能。

#### （3）`AlgorithmType.SKIP` 引用错误（已修正）

- **原问题**：`SilentExplorer` 曾引用不存在的 `AlgorithmType.SKIP`，在非 `NONE` 的 `greedy_algo` 下会 **`AttributeError`**，被 worker 吞掉。  
- **现状**：已改为仅排除 **`AUTO` / `NONE` / `TRANSFORMER`** 等真实枚举值；探索逻辑可正常跑通（若启用）。

#### （4）ADE 决策阶段的大对象与内存压力

- **位置**：`DecisionEngine.decide` 在无法走 `analyze_file` 时使用 `self._ade.analyze(list(record.raw_data))`（`src/gui/ade/engine.py`）；与已加载的 `record.raw_data` 叠加，**短时双倍内存**（大 `.ipynb` 等场景）。  
- **风险**：不直接等于「ADE 导致解压闪退」，但在 **AUTO + 大文件 + 内存压缩路径** 上与 §2.3 的巨型 `list`/`bytes` 问题叠加，可能提高 **OOM 被系统杀进程** 的概率（用户仍可能描述为闪退）。

#### （5）解压路径与 ADE 的直接关系

- **主路径**：`_decompress_payload` / `_on_decompress` 使用 `CompressionEngine` 与 WCX 解析，**未调用** `DecisionEngine.decide` 或训练存储写入。  
- **间接关系**：仅通过 **共享的 `CompressionEngine` 全局状态** 与（1）（2）产生耦合。

### 2.6 「绝不闪退」在工程上能承诺到哪里（与 ADE 解耦）

| 手段 | 作用 | 能否「绝对」 |
|------|------|----------------|
| **进程隔离** | 将「ADE 推理 / 探索压缩」放在 **子进程**（或独立 worker 进程），主 GUI 只收 JSON 结果；子进程崩溃不拖死 UI。 | 对 **Python 未捕获异常、部分 native 崩溃** 可做到 UI 不跟着退出；若 native 破坏共享资源仍非 100%。 |
| **取消共享可变状态** | 每次压/解压向 C++ 传入 **显式参数字典**（或不可变快照），**不在**后台线程里 `set_config` 改全局。 | 从架构上 **与 ADE 解耦**；需改 API 面，工作量大。 |
| **同进程互斥（已实现）** | `CompressionEngine` 对 **`_config` 读写** 与 **依赖该配置的 native 压/解压入口** 使用 **`threading.RLock` 串行化**（含 `SilentExplorer` 与主线程解压交错场景）。 | 消除 **配置撕裂类** 竞态；**不能**防止 C++ 内部单线程逻辑 bug 或 OOM。 |
| **默认关闭静默探索（已实现）** | `SilentExplorer` 默认 `enabled=False`；需设置环境变量 `WEBCOMPRESS_SILENT_EXPLORE=1` 才开启后台探索。 | 默认路径下 **后台线程不再参与** 引擎配置与压缩，ADE 与「解压/压缩竞态」解耦；开启后仍受锁与 native 质量约束。 |
| **AUTO 决策只做 I/O 与有界计算** | 大文件一律 `analyze_file(path)`，避免 `list(raw_data)` 双倍内存（见 `DecisionEngine.decide`）。 | 降低 **OOM 杀进程** 概率；不消除全部内存尖峰。 |
| **熔断 / 超时** | 对 `decide`、探索压缩设 wall-clock 超时，超时报错回退算法。 | 避免「卡死」；闪退若为 native 无超时仍无效。 |

**结论**：在 **同一进程、同一 `core_engine` pyd`** 内，**没有任何办法数学上保证「绝不闪退」**——只要存在 **未定义行为的 native 代码** 或 **系统 OOM**，进程仍可能退出。工程上可承诺的是：**把 ADE 接入层能引入的故障类（竞态、错误的全局配置、后台线程与 UI 线程交错）压缩到接近零**；其余需靠 **子进程隔离 + native 质量与测试**。

---

## 3. 已实施的缓解与观测增强

1. **`src/gui/ui/worker.py`**  
   - 抽取 **`_run_streaming_compress_branch`**，流式成功路径与 **内存失败回退** 共用同一套收尾逻辑（状态、训练钩子、文件夹汇总、`finished_row` 等）。  
   - 内存路径对 **`smart_compress`** **`try/except`**；**`success is False` 或异常** 且在允许条件下 **自动回退文件到文件**。  
   - **`result.data` 为 `list`** 且允许回退时，走文件到文件，避免巨型 `bytes(list)`。  
   - 在 **`smart_compress` 返回后** 增加结构化日志（success / compressed_size），便于对照 `gui.log` 判断是 **未返回** 还是 **返回后失败**。

2. **绑定层 buffer 重载**（见 §2.1）— 需与 **重编译的 pyd** 一并交付。

3. **`src/gui/engine/compressor.py`（ADE 与压/解压解耦 — 竞态面）**  
   - 增加类级 **`_engine_op_lock`（`threading.RLock`）**：在 **`get_config` / `set_config` / `snapshot_for_algorithm` / 流式阈值读写 / `reset_to_defaults`** 以及 **`compress` / `decompress` / `smart_*` / `pipeline_*` / `pipeline_*_file`** 等依赖全局 `_config` 的路径上 **串行化**，避免 `SilentExplorer` 后台线程与主线程解压、`CompressionWorker` 交错修改或读取半更新配置。  
   - **`_create_compressor`** 仅在持锁路径内通过 **`_get_config_unlocked()`** 读配置。

4. **`src/gui/ade/explorer.py`**  
   - 修正 **`AlgorithmType.SKIP`** 引用；**默认关闭**静默探索，仅当 **`WEBCOMPRESS_SILENT_EXPLORE=1`** 时启用（降低默认安装下的线程与引擎交错风险）。

## 4. 验证建议

| 步骤 | 说明 |
|------|------|
| **开发机** | 将 `PYTHONPATH` 指向含新 `core_engine` 的 `build_py`（或通过 `bridge` 解析到该路径），对目标大文件分别测 **仅内存** 与 **强制流式**；查看日志是否出现 `memory smart_compress returned ...`。 |
| **安装包** | 用新 pyd 重打 `WebCompress.exe`，在 **与用户相同** 的「非流式 DEFLATE」场景下回归；对比 `Package/logs/gui.log` 是否仍有中断且无该日志行。 |
| **边界** | 无磁盘路径的内存-only 记录：无法回退流式时，应 **明确失败信息** 而非静默退出（由外层 `except` 与 `error.emit` 兜底）。 |

---

## 5. 残留风险与后续可选工作

- **原生层崩溃**（访问违例等）若仍发生，Python 的 `try/except` **无法捕获**；需依赖 **Windows 转储 / 调试器** 与 C++ 栈进一步定位。  
- 可为关键算法路径增加 **更多轻量日志点**（注意日志体积与敏感路径），或在内部构建打开 **更详细的核心日志**。  
- **CI**：增加「导入 `core_engine` + 对 `bytes` 调用各算法 compress 一轮」的 **烟测**，防止绑定回归。  
- **ADE / 静默探索（§2.5）**：**已落实** 全局配置与 native 压/解压入口的 **`RLock` 串行化**（`compressor.py`）；**已落实** 移除无效 **`SKIP`** 与 **默认关闭** 静默探索（`explorer.py`，可用 `WEBCOMPRESS_SILENT_EXPLORE=1` 打开）。**仍建议**：长期改为「每次调用显式传参、不依赖可变全局 `_config`」；AUTO 大文件优先 **`analyze_file`** 避免 `list(raw_data)`。

---

## 6. 关键文件索引

| 路径 | 角色 |
|------|------|
| `src/gui/ui/worker.py` | 压缩工作线程：流式 / 内存 / 回退与 `compressed_data` 赋值 |
| `src/bindings/pybind/pybind_module.cpp`（及子模块） | buffer 重载、`buffer_to_u8vec` |
| `src/gui/engine/bridge.py` | pyd 搜索顺序与加载 |
| `Package/logs/gui.log` | 现场行为与分支对照 |
| `Package/config/webcompress_settings.json` | 与用户流式阈值等相关的运行配置（若涉及） |
| `src/gui/engine/compressor.py` | 全局 `_config` + native 压/解压与 **`_engine_op_lock`**（§2.5 / §2.6） |
| `src/gui/ade/explorer.py` | 静默探索；默认关；`WEBCOMPRESS_SILENT_EXPLORE` |
| `src/gui/ade/engine.py` | `DecisionEngine.decide` / `analyze` 与内存拷贝（§2.5） |
| `src/gui/ui/worker.py` | AUTO、`maybe_explore`、训练存储钩子与压缩主路径 |

---

## 7. 文档信息

| 项目 | 内容 |
|------|------|
| **文档类型** | 事后分析汇报（回顾） |
| **状态** | 调查结论与缓解已写入代码库；发版需核对 **pyd 版本** 与 **安装包内容** |

如需将本汇报并入正式「事故复盘」模板（时间线、影响面、责任人），可在此文件基础上追加章节。
