# LZDP 非流式与流式结果差异：分析报告与试验设计

**日期**：2026-05-12  
**范围**：GUI / PyBind 内存路径（`LZDPCompressor`）与 C++ 文件管线（`compressFile` → `LZDP_OutOfCore`）在**同一组参数**下压缩**结果（体积/比特流）可能不一致**的原因说明；**不包含**「未做流式」的指控验证（实现上文件侧为分块读入 + 外存 DP，见 `lzdp-file-pipeline-design.md`）。

**更正（同日）**：曾发现 `LZDP_OutOfCore::handleEmitTokens` 未输出与 `LZDP::compress_dp` / `LZDP::decompress` 一致的两字节**明文头**，导致 WCX 内 payload 无法被 `LZDPDecompress_OutOfCore` / `decompressFile` 正确解析；已在 `src/algorithm/LZDP.cpp` 与 `LZDP.hpp` 修复（发射端 `writeBytes` 两字节头，解压端按字节累积读取）。**旧版无头 payload 的 WCX 将无法用当前解压器解码。**

**更正 2（同日）**：流式发射曾将 **flag 路径** 的「字面 / 匹配」控制位与 `compress_dp` **反相**（应为字面=1、匹配=0）；已修正。非 flag 路径下，流式发射为**单字节字面**补上与 `compress_dp` 一致的 **offset=0、run 长度=1** 的前缀（逐字面一条），避免解压端把字面误读为匹配。**仍须注意**：字段内比特序若与内存版 `BitWriter`（LSB-first 打包）不完全一致，则 **WCX payload 与 `compress_dp` 裸流仍可逐字节不同**；`tests/test_lzdp_memory_streaming_parity.cpp` 以 **`decompressFile` 闭环** 与 **分块不变性** 为主，不把「`LZDP::decompress(payload)` 与内存流一致」作为通过条件。

---

## 1. 结论摘要

| 维度 | 说明 |
|------|------|
| **是否 bug** | 多数差异来自**两套不同实现与不同优化目标**，属**已知语义分歧**，不是单纯「衔接写错」即可与内存版逐比特对齐。 |
| **可逆性** | 两条路径产出均应对应解压器可还原（需各自格式一致）；**互压互换**不要求字节级相同。 |
| **用户观测** | 「压缩率接近但不同」「时间/内存接近」与「一个在算 token、一个在近似算比特 + 环形 DP」并不矛盾。 |

---

## 2. 代码路径对照

| 环节 | 非流式（内存） | 流式（文件管线） |
|------|----------------|------------------|
| 入口 | `LZDPCompressor::compress`（`src/core/LZDPCompressor.cpp`） | `api::compressFile`（`src/api/api.cpp`）→ `Pipeline` → `LZDP_OutOfCore` |
| 核心算法类 | `algorithm::LZDP` | `algorithm::LZDP_OutOfCore`（`src/algorithm/LZDP.cpp`） |
| `dp_top > 1` 时 | `LZDP::compress_dp`：链表 DP，**每步代价为 +1 token**（literal 与 match 均 +1） | 前向递推用 `lit_cost_` / `match_cost_`（**按位宽估算的整数代价**），**不是** `compress_dp` 的 token 计数 |
| `dp_top == 1` 时 | `LZDP::compress`（**贪心**，与 DP 最优性无关） | 仍为 `LZDP_OutOfCore` 的代价型前向 + 回溯（与内存 greedy **不对应**） |
| 明文可见范围 | 全文 | 滑动窗口（约 `search_size` + 前瞻缓冲）；匹配候选在窗口内应与「全局中看到的历史」一致，但 **DP 在全局选的路径** 与 **流式在局部代价模型下选的路径** 仍可不同 |
| 容器 | 裸 LZDP 比特流（含 2 字节头） | WCX 封装（头 + payload）；对比体积时需**同一粒度**（见 §5） |

**关键代码语义（内存 DP）**：`compress_dp` 中 `dp[pos]->num + 1` 表示 token 数（`src/algorithm/LZDP.cpp` 前向循环）。  
**关键代码语义（外存 DP）**：`LZDP_OutOfCore::handleCollectInput` 中用 `cur.cost + lit_cost_` / `+ match_cost_` 更新 `DpState::cost`（同文件）。

因此：**即使 HashChain/KMP 与内存版完全一致、分块衔接正确，只要前向目标函数不同，最终路径与比特流就可以稳定地不一致。**

---

## 3. 其它放大差异的因素

1. **参数是否真的一致**  
   - 内存：`gui/engine/compressor.py` → `_create_compressor` 读 JSON。  
   - 流式：`LzdpWholeFileParams` / `_lzdp_whole_file_params_for_file_pipeline()`。  
   - AUTO / ADE 可能改内存侧配置或 `forced_no_stream`，易出现「以为同一参数、实际不同」。

2. **`min_match == 0` 的推导**  
   - `LZDP_OutOfCore` 构造里会按位宽推导默认最小匹配长；内存侧 `LZDPCompressor` 若未同步设置，可能不一致。

3. **哈希桶映射**  
   - `compress_dp` 使用 `max_search_size_` 与 `head` 尺寸（与 `autoBitWidth` 相关）。  
   - 外存版使用 ctor 传入的 `SEARCH_SIZE` 与 `head_`。若 GUI 的 `search_size` 与内部位宽推导混用，可能产生候选集细微差别（通常在修正 `prev_buf_`/reseed 后主要为边界情况）。

4. **WCX 与「算法裸流」**  
   - 对比「压缩率」时若一侧含 WCX 头、一侧不含，数值会系统性偏差。

---

## 4. 若未来要求「外存路径与 `compress_dp` 逐字节一致」

需单独研发项（设计文档中已诚实说明），方向包括：

- 前向阶段改为与 `compress_dp` **同一递推**（token 或同一代价函数），或  
- 在仅窗口可见的前提下形式化证明近似与最优的误差界（产品化少见）。

当前 `LZDP_OutOfCore` **不承诺**与 `compress_dp` 比特级一致。

---

## 5. 对比试验时的「苹果对苹果」约定

1. **解压闭环**：两种压缩结果分别解压，必须与原文一致（正确性）。  
2. **体积对比**：  
   - 要么都对 **WCX unpack 后的 payload** 比长度；  
   - 要么都对 **裸 LZDP 流**（内存 `compress_dp` 输出 vs 从 WCX 抽出的 payload）比长度。  
3. **固定随机种子**：可重复文本 / 伪随机字节便于回归。  
4. **记录**：`search_size, lookahead, min_match, dp_top, use_flag_encoding, match_engine` 及 `chunk_size_kb`、是否 AUTO。

---

## 6. 测试与试验设计

### 6.1 层级 A — 正确性（必做）

| 编号 | 目的 | 方法 |
|------|------|------|
| A1 | 内存压缩可解压 | 现有 `test_roundtrip` / GUI 解压流程 |
| A2 | 流式文件压缩可解压 | `compressFile` + `decompressFile` 或对产出 WCX 解压 |
| A3 | 分块不改变流式语义 | **同一输入、同一参数**，仅改变 `stream_chunk_bytes`（如 4 KiB vs 1 MiB），**输出 WCX 文件字节级相同**（应在 CI 中强制执行） |

**解读**：A3 若失败，说明 pipeline/状态机在分块边界有误（与「和内存是否一致」无关）。

### 6.2 层级 B — 差异度量（推荐）

| 编号 | 目的 | 方法 |
|------|------|------|
| B1 | 量化 memory vs streaming 体积差 | 固定语料库（文本、重复模式、高熵随机、结构化二进制），记录 `payload_mem`、`payload_stream`、`Δ% = (stream−mem)/mem` |
| B2 | 参数扫描 | 对 `dp_top ∈ {1,3,8}`、`match_engine ∈ {0,1}`、`use_flag_encoding ∈ {0,1}` 做小型网格，输出表格 |
| B3 | 规模曲线 | 固定参数，`n ∈ {1e4, 1e5, 1e6, 1e7}` 字节，观察 `Δ%` 是否随 n 收敛或振荡 |

### 6.3 层级 C — 调试型（可选）

| 编号 | 目的 | 方法 |
|------|------|------|
| C1 | Triple 序列对比 | 内存侧 `dp_core` vs 外存侧若暴露 debug 导出（需开发支持），找首个分歧位置 |
| C2 | 单块喂满外存 | 仅用 `LZDP_OutOfCore::process` **一次**喂入全文（仍走外存 DP 逻辑），与 `compress_dp` 比 — 若仍不同，可排除「分块读入」因素，归因于 **目标函数/环形表** |
| C3 | 极小输入手算 | 长度小于 lookahead+search 的若干手工串，人工推断最优 token 路径，对照两种实现 |

### 6.4 自动化实现状态

- 仓库内新增：`tests/test_lzdp_memory_streaming_parity.cpp`（CMake 目标 `test_lzdp_memory_streaming_parity`）。  
  - **断言通过**：层级 A3（同一输入、两种 `stream_chunk_bytes`，WCX 文件字节级相同）；内存 `compress_dp` 自解压；**`decompressFile` 对 WCX 闭环还原原文**。  
  - **仅观测**：内存流与 WCX payload 的长度 / 是否逐字节相同（允许不同，见 §2 / §更正 2）。

---

## 7. 与产品文档的关系

- 管线级说明：`docs/design/lzdp-file-pipeline-design.md` §2（与内存版差异）。  
- 本文：**展开根因（目标函数 + 路径差异）** 与 **可执行的试验矩阵**，便于评审与后续「对齐」立项。

---

## 8. 参考文献（代码）

- `src/algorithm/LZDP.cpp`：`compress_dp`、`LZDP_OutOfCore`  
- `src/core/LZDPCompressor.cpp`：内存入口  
- `src/api/api.cpp`：`compressFile`  
- `src/core/AlgorithmFactory.cpp`：`createAlgorithm` → `LZDP_OutOfCore`  
- `src/gui/ui/worker.py`：流式 vs `load_raw_data` 分支  
