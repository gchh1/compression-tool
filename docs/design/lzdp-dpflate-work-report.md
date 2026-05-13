# LZDP / DPFlate 流式整改工作报告



**文档性质**：面向产品与研发的阶段性汇总（算法语义、实现落点、接口与配置、已知缺口）。  

**关联规格**：`streaming-workspace-spec.md` §16.1.1（LZDP）、§16.6（DPFlate）、§12（流式配置）；**LZDP 文件管线专文** `lzdp-file-pipeline-design.md`。  

**关联日志**：`wcx-streaming-remediation-log.md`（WCX / 流式按日期条目）。



---



## 1. 背景与目标



两类算法若采用「每收到一块数据就独立跑一次压缩核心」的 **legacy 分帧** 路径，整体结果一般 **不等价** 于对整份输入只做一次的内存版语义（LZDP 的前向 DP + 回溯；DPFlate 的整文件 DP + Huffman）。



**整改目标**：



| 算法 | 策略（配置 / 产品） | 目标 |

|------|---------------------|------|

| **LZDP** | **无** `streaming_mode`；`WholeFileFramedCompressAdapter` + 与 `LZDPCompressor` 相同的 `compress` / `compress_dp` 分支 | 文件管线与内存压缩 **比特级一致**（同 `LzdpWholeFileParams`）。 |

| **DPFlate** | **无流式模式开关**；文件管线固定为 **外存链路 + 环状 DP 状态 + 整文件 Huffman**（`algorithm::DPFlate`） | 与整文件单次 DPFlate 语义对齐；**已移除**「旧版分帧 / 可切换 `streaming_mode`」产品路径。 |



---



## 2. LZDP 实现摘要



### 2.1 机制



- **`WholeFileFramedCompressAdapter`**（`ChunkedStreamAdapter.hpp` / `.cpp`）：读入阶段缓冲全部明文直至 EOF；结束时 **只调用一次** 压缩核心；输出仍为 **`[u32 长度][payload]` … + 终止帧**，与 `StreamingCompressAdapter` 外层格式一致。

- **压缩核心**：`AlgorithmFactory` 内对 `algorithm::LZDP` 调用与 **`LZDPCompressor::compress` 相同**：`dp_top > 1` → `compress_dp`，否则 `compress`（整文件一次）。

- **参数对齐**：引入 **`LzdpWholeFileParams`**（`AlgorithmFactory.hpp`），字段与 `LZDPCompressor` 可调项一致：`search_size`、`lookahead_size`、`min_match`、`dp_top`、`use_flag_encoding`、`match_engine`。文件管线在开启整文件 LZDP 时由 Python 传入当前引擎配置快照，避免与 GUI 内存压缩口径漂移。



### 2.2 开关与工厂



- **`kFileCompressLzdpWholeFileFramed`**（`file_compress_opts` **bit 0**）：**ABI / 日志保留**；路由不依赖该位，`id == LZDP` 时 **始终** 使用整文件帧适配器 + `LzdpWholeFileParams`（缺省字段用 C++ 默认结构体）。

- **`createAlgorithm(id, file_compress_opts, lzdp_whole_file)`**：`id == LZDP` 时使用 `lzdp_whole_file`（或默认 `LzdpWholeFileParams{}`）；链式管线中其它算法忽略该指针。



### 2.3 Python / GUI



- `CompressionEngine._lzdp_whole_file_params_for_file_pipeline()`：从 `get_config()[LZDP]` 构造 `core_engine.LzdpWholeFileParams`。

- `smart_compress_file` / `pipeline_compress_file`：对 LZDP **始终** 置 `_FILE_COMPRESS_LZDP_WHOLE_FILE` 并传入 `LzdpWholeFileParams` 快照。

- Pybind：`LzdpWholeFileParams` 类型绑定；`pipeline_compress_file(..., lzdp_whole_file=None)` 对应 C++ `optional`。



---



## 3. DPFlate 实现摘要（当前）



### 3.1 机制



- **工厂**：`AlgorithmID::DPFlate` → **`std::make_unique<algorithm::DPFlate>()`**，**不再**根据 `file_compress_opts` 切换行为。

- **流式语义**：`DPFlate` 在 `COLLECT_INPUT` 中按绝对位置前向 DP，**packed link** spill 到临时文件（见 `TempFile.hpp` / `SpillBitStream.hpp`）；EOF 后回溯、建树、`EMIT_TOKENS` 输出 Deflate 风格比特流；解压仍为 **`Inflate`**。

- **`DPFlate::set_prefer_disk_dp_tables(bool)`**：头文件中仍保留 setter；**当前 `DPFlate.cpp` 未实现 mmap 大表切换**，与规格中的「可选 mmap」仍为后续优化项。



### 3.2 已移除的产品 / API 项



- **`kFileCompressDpflateDiskDpTables`（原 bit 1）**：已删除；Python **`_FILE_COMPRESS_DPFLATE_DISK_DP`** 与 **`get_dpflate_streaming_mode`** 已删除。

- **GUI**：DPFlate 算法页 **不再提供** `global_dp_spill` / `legacy_chunked` 下拉框；配置中的 `streaming.per_algorithm.dpflate.streaming_mode` 在加载时 **忽略并剔除**。



---



## 4. C API 一览（文件压缩）



| 符号 | 含义 |

|------|------|

| `compressFile(..., stream_chunk_bytes, file_compress_opts, lzdp_whole_file)` | 单文件流式压缩；后两项仅在有 LZDP 整文件需求时使用。 |

| `kFileCompressOptsNone` | `0` |

| `kFileCompressLzdpWholeFileFramed` | `1 << 0` |



**解压**：`decompressFile` 仍按算法 ID 构建管线；LZDP/DPFlate 解压侧不依赖上述 bit（载荷格式不变）。



---



## 5. 配置契约（与规格对齐）



- **LZDP**：**无** `streaming_mode`（历史键加载时剔除）；见 `lzdp-file-pipeline-design.md`。

- **DPFlate**：**无** `streaming_mode`；文件流式路径固定为 §16.6 描述的外存 DP + 整文件 Huffman 管线。

- **语义**：见 `streaming-workspace-spec.md` §16.1.1、§16.6。



---



## 6. 主要涉及文件



| 区域 | 路径 |

|------|------|

| 整文件帧适配器 | `src/processor/include/ChunkedStreamAdapter.hpp`、`ChunkedStreamAdapter.cpp` |

| 工厂 / LZDP 快照 | `src/core/include/AlgorithmFactory.hpp`、`AlgorithmFactory.cpp` |

| 文件 API | `src/api/include/api.hpp`、`api.cpp` |

| DPFlate 核心 | `src/algorithm/include/DPFlate.hpp`、`DPFlate.cpp` |

| Pybind | `src/bindings/pybind/pybind_module.cpp` |

| GUI 引擎 | `src/gui/engine/compressor.py` |



---



## 7. 已知缺口与后续（摘自当前代码现状）



1. **`compressDirectory` / PackWriter** 路径尚未传入 `file_compress_opts` / `LzdpWholeFileParams` 的完整对齐策略；目录内单文件 LZDP 是否与 GUI 快照一致需单独对齐需求。

2. **GUI `pipeline_decompress_file`**：DPFlate 等是否在解压链全接入，以实现「压缩包 → 文件」一键解压，需按产品优先级补映射（与内存 `decompress` 能力无关）。

3. **DPFlate `prefer_disk_dp_tables`**：若需与规格中的 mmap 大表一致，应在 `DPFlate.cpp` 落地实现或删除未使用 API。



---



## 8. 建议验收要点



- **LZDP**：同一配置下，大文件 **文件管线** 与 **`CompressionEngine.compress`（内存）** **比特级一致**（`LzdpWholeFileParams` 与 `LZDPCompressor` 同源）；帧外层 WCX 解压仍可还原明文。

- **DPFlate**：文件流式与内存 `DPFlateCompressor.compress` 在相同参数下语义一致；无 `streaming_mode` 回归项。

- **回归**：既有 WCX / `compressFile` / `pipeline_*` 测试与手工 GUI 大文件抽查。



---



## 9. 现场观测：大输入下内存与 GUI 表现（待核实 / 待优化）



以下为用户在 **GUI 压缩约 20 MiB 单文件** 时的现象记录（**未在 CI 中自动复现**；环境、构建类型、是否走内存路径 vs 文件流式路径需对照当时配置）。



| 现象 | 量级（观测值） | 备注 |

|------|----------------|------|

| **LZDP** | 进程内存涨至约 **8 GiB** | 与整文件 DP / 大表、中间结构在 **RAM** 内实现一致；**文件管线** 在适配器内 **缓冲整段明文** 再一次性跑与内存相同的 `compress` / `compress_dp`，峰值仍可能很高。 |

| **Deflate** | 涨至约 **140 MiB**；完成后界面显示算法为 **none** | 若压缩后体积 **不小于** 原文，产品逻辑会 **回退为 stored**，并把 `algorithm` 标为 **`NONE`**（非 Deflate 失败）；需区分「真·选错算法」与「膨胀后 stored」。 |

| **DPFlate** | 涨至约 **4 GiB** | **明文缓冲区**、Huffman 与其它结构仍占大量内存；20 MiB 输入下 DP 相关开销仍可能极大。 |



**建议后续动作**（不在本条一次性完成）：用同一文件与配置在 **任务管理器 / 采样分析器** 下区分堆 vs 工作集；确认是否走了 **流式文件** 还是 **内存 `smart_compress`**；对 LZDP/DPFlate 是否需要 **输入侧 spill** 或 **硬上限** 做产品决策。



---



## 10. 内存与「真流式」：设计假设与待决问题（技术讨论备忘）



以下为用户侧 **推断与待验证命题**，用于后续规格/实现评审；**不代表当前代码已按此结论修改**。



### 10.1 Token / 中间结构的数据宽度



- **命题**：除逻辑上的「动态位宽」编码外，**实现层**若用过大或不适配的标量类型（例如处处 `uint64` / 宽结构体）承载 token、候选、边表等，会在 **候选数 × 位置 × 结构体对齐** 下放大常驻内存；**位宽随参数变化** 时，**物理存储类型**（如 `uint8` / `uint16` / 按块打包）应与之匹配，以压缩元数据与中间数组的 **字节/元素宽度**。

- **与「非 8 的倍数位宽」的关系**：比特级模型可以非整字节；**内存中的表、索引、token 流** 仍宜以 **8 位对齐的容器**（或显式 bit-packing 布局）组织，避免隐式 padding 与缓存行浪费叠加。



### 10.2 DP 系：~20 MiB 输入 vs 数 GiB 峰值



- **观测量级回顾**：约 **3–8×10³ MiB 进程内存** 对比 **~20 MiB 明文**，**膨胀约两个数量级以上**（用户表述约 400× 量级，具体以采样为准）。

- **设计判断（待证）**：若主要开销来自 **整段明文 + 稠密 DP/回溯表 + 可视化或调试侧结构** 的叠加，则当前 **「流式外壳 + 整文件算法语义」** 在 **RAM 预算** 上仍可能不满足「大文件安全」的产品定义；**mmap 大表** 只解决其中一块，**不**等价于「整条管线内存与分块大小同阶」。

- **待决**：是否要求 **输入侧 spill**（明文也不全进 RAM）、**硬上限**（超限拒绝或降级算法）、或 **分阶段 DP** 等架构级方案，需在 `streaming-workspace-spec.md` 与产品目标中对齐。



### 10.3 Deflate：分块 1024 KiB vs ~140 MiB 占用



- **命题**：若 **单次读块** 为 1 MiB 量级，而 **常驻/working set** 达 **~10² MiB**，则 **仅靠「按块读入」不足以称为严格意义上的低内存流式**；中间可能仍存在 **大块输出缓冲、多份拷贝、池化 arena、或整段预处理** 等路径，需在实现与剖析中逐项核对。

- **与「none 算法」显示**：仍见 §9：膨胀后 **stored** 会显示 **`NONE`**，与内存诊断需分开讨论。



### 10.4 建议的下一步（工程化）



1. **剖面**：同一输入下对 **LZDP / DPFlate / Deflate** 分别做 **分配器/大对象** 归因（是否整文件缓冲、DP 表、Huffman 树、Python 侧 list 等）。  

2. **路径确认**：日志或标志区分 **`compressFile` 流式** vs **`smart_compress` 内存整段** vs **stored 分支**。  

3. **规格**：在流式章节明确 **「分块读」≠「峰值内存 ∝ chunk」** 的契约，以及 DP 系 **允许的最大 RAM 倍数** 或 **降级策略**。



---



*生成说明：本报告由仓库当前实现与规格文档交叉整理，用于对内交付；细节以源码为准。*

