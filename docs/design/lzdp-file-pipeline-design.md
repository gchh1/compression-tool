# LZDP 文件管线设计（外存 DP / 分块读入）

**文档性质**：实现说明与产品口径（与 `streaming-workspace-spec.md` §16.1.1 一致）。  
**更正（2026-05-12）**：曾短暂采用「整文件明文缓冲 + 单次 `compress`/`compress_dp`」以追求与内存版比特级一致，但会 **整文件进 RAM**，违背「分块 + DP 外存」的流式目标；**默认路径已恢复为 `LZDP_Streaming`**。

---

## 1. 目标（你要求的流式语义）

对大文件走 C++ `compressFile` / pipeline 时：

1. **分块读入**：按配置的 `chunk_size_kb` 从磁盘读明文（与 C++ ``compressor::processor::effective_stream_chunk_bytes`` / ``StreamChunkPolicy.hpp`` 及 Deflate/LZSS 流式段长**同一套** clamp），**不在块边界结束算法**；明文仅在 **滑动窗口**（约 `search_size + lookahead` 量级）内常驻内存。  
2. **前向 DP → 外存暂存（spill A）**：每个绝对字节位置上的「入边」以 **packed link** 写入临时文件（工作区 `tmp/` 等），不在内存里持有完整 `O(n)` 的 DP 表。  
3. **回溯（backtrack）**：从输入末尾沿 spill A 反向走父链，将链路写入 **第二临时文件（spill B）**（逆向 token 序列）。  
4. **发射（emit）**：先写出与内存版一致的 **2 字节明文头**（`offset_bits`/`length_bits`/flag），再按与编码器一致的顺序从 spill B 读出并写出 **token 比特流**（flag / literal run / match 等）。

以上四步对应实现中的 **`COLLECT_INPUT` → `BACKTRACK` → `EMIT_TOKENS`**（`src/algorithm/LZDP.cpp` · `LZDP_Streaming`）。

---

## 2. 与内存版 `LZDPCompressor` 的差异（诚实说明）

| 项目 | 内存 `LZDPCompressor` | 文件 `LZDP_Streaming` |
|------|------------------------|------------------------|
| 明文 | 一次持有完整 `vector` | **滑动窗口**，旧字节丢弃 |
| DP 目标 | `compress_dp`：**token 数**最少；`compress`：`dp_top==1` 时 **贪心** | 前向步进使用 **按位权** 的 `lit_cost_` / `match_cost_` 启发式（与 `compress_dp` 的「每步 +1 token」**不完全相同**） |
| 压缩率 | 以 GUI 参数为准的「标准」口径 | **可能**与内存版略有差别；换的是 **峰值内存可控** + **符合外存 DP 管线** |

若将来需要 **既外存又比特级对齐 `compress_dp`**，需要把前向阶段的目标函数与 `compress_dp` 完全统一（或在外存上复现同一递推），这是单独的研发项，**不是**当前 `LZDP_Streaming` 已承诺的行为。

更完整的根因说明、对比口径与试验矩阵见：**`docs/analysis/lzdp-nonstreaming-vs-streaming-divergence-report.md`**。

---

## 3. 参数

与 GUI / `LzdpWholeFileParams` 一致：`search_size`、`lookahead_size`、`min_match`、`dp_top`、`use_flag_encoding`、`match_engine`。  
工厂：`createAlgorithm(LZDP, …, lzdp_whole_file, …)` → `LZDP_Streaming(...)`。

---

## 4. 实现注意

- **DP 环形槽**：`dp_slot_count_ = max(64, 2 * lookahead + 2)`，避免长匹配下前向表下标取模 **撞槽**（旧版仅用 `lookahead+1` 在极个别参数下不够稳）。  
- **临时文件**：`TempFile` / `TempFileBitAppender`；大输入下 spill 体积与输入长度相关，属预期。  
- **解压**：`LZDPDecompress_Streaming` 解析头两字节须与 `LZDP::compress` 一致（低 7 位 offset 位宽、最高位 flag）。  
- **BitWriter 与两字节头**：发射阶段用 `utils::BitWriter` 写 **明文头** 再写 token 比特。若 `buffer_` 中残留非整数字节的比特（例如实例复用未清空、或误在分数比特边界上 `writeBytes`），头两个字节会错位，解压报 `invalid bit widths in header`，且压缩体积会异常膨胀。应在 **`LZDP_Streaming::reset`** 与 **写头前** 调用 `resetPendingBits()`，并保证 `writeBytes(h,2)==2`。

---

## 5. 相关代码

| 组件 | 路径 |
|------|------|
| 外存 LZDP 状态机 | `src/algorithm/LZDP.cpp`（`LZDP_Streaming`） |
| 工厂 | `src/core/AlgorithmFactory.cpp` |
| 参数快照 | `src/core/include/AlgorithmFactory.hpp`（`LzdpWholeFileParams`） |
| Python 传参 | `src/gui/engine/compressor.py` |

---

## 6. 与 DPFlate 的对比

**DPFlate**（`src/algorithm/DPFlate.cpp`）在文件管线上 **同样**采用：分块读入、前向 DP、spill A、回溯 B、再 Huffman 建树与发射；**不会**为对齐比率而把整份明文缓冲在适配器里。若你观测到「流式内存暴增」，请先确认算法是 **LZDP** 还是 **DPFlate**、以及是否仍在使用旧的「整文件缓冲」构建产物；当前源码树中 **LZDP 默认已回到 `LZDP_Streaming`**。
