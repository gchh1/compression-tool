# 工作交接：流式压缩（LZDP / 管线）— 截至 2026-05-12

本文档面向**接手人**：汇总需求方在「流式压缩」议题上提出的问题、**实际落地到仓库的改动**、以及**仍未按需求方预期闭环**的部分，便于续作与验收口径对齐。

---

## 一、需求方提出的问题（验收视角）

以下按对话脉络归纳；**不代表**均已从用户角度视为「已解决」。

| # | 问题 | 用户期望 / 关切 |
|---|------|------------------|
| 1 | **非流式与流式 LZDP 结果不一致** | 设计上分块+滑窗应与整文件 DP 衔接一致；怀疑实现未处理好 HashChain/KMP 与分块边界。 |
| 2 | **「解耦」** | 分块衔接逻辑应独立于 HashChain/KMP；实现上是否真正解耦。 |
| 3 | **取消不立即生效；磁盘 I/O 显示接近 0；怀疑未做流式** | 点取消应立刻停；任务管理器磁盘低 ≠ 未流式；需澄清路径与指标。 |
| 4 | **流式压缩结果损坏** | 大文件（如 `imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx`）解压失败：`LZDP decompress: invalid bit widths in header`；压缩率异常差。 |
| 5 | **非流式 vs 流式差异分析报告与试验设计** | 书面根因 + 可复现实验矩阵。 |

**需求方结论性意见（交接时）**：认为上述流式相关问题**基本上未得到解决**，需要导出工作交接。

---

## 二、代码与文档侧**已做**事项（事实清单）

### 2.1 LZDP 流式内核（`LZDP_OutOfCore`）与 DPFlate

- **HashChain**：由错误的 `prev_[abs_pos % SEARCH_SIZE]` 改为按缓冲区下标的 `prev_buf_`，与 `compress_dp` 的 `prev[pos]` 语义对齐；仅在**窗口左移**后调用 `reseedHashChainPrefix`（避免每批全量重播的 O(n²)）；`reseed` 与 DP 内层增加**取消检查**频率。  
  - 路径：`src/algorithm/LZDP.cpp`、`src/algorithm/include/LZDP.hpp`；`src/algorithm/DPFlate.cpp`、`src/algorithm/include/DPFlate.hpp`。

- **DP 环形槽**：`DPFlate` 使用 `dp_slot_count_`（与 LZDP 侧思路一致），减轻长匹配撞槽。

- **流式比特流与解压**（多次迭代）：  
  - 发射阶段补充与内存版一致的 **2 字节明文头**；解压端按**字节**累积读取头（跨 chunk）。  
  - **Flag 路径**：字面/匹配控制位与 `compress_dp` 对齐（字面=1，匹配=0）；非 flag 单字面补 **offset=0 + run 长度=1** 再跟 8 比特字面。  
  - **`BitWriter`**：增加 `resetPendingBits()`；`LZDP_OutOfCore::reset` 与**写头前**调用，避免残留分数比特导致头错位 → `invalid bit widths` 与体积膨胀。  
  - 路径：`src/utils/include/BitWriter.hpp`、`src/algorithm/LZDP.cpp`。

### 2.2 GUI / 取消

- 取消时调用 `_core_set_streaming_compress_cancel(True)`；C++ `compressFile` 循环中检查取消（既有逻辑）。  
- 与**长临界区**（DP/reseed）相关的响应性通过**更密的取消检查**与 **reseed 策略**改善；是否在用户设备上「足够快」**未做专项验收**。

### 2.3 文档与测试

- **分析报告**：`docs/analysis/lzdp-nonstreaming-vs-streaming-divergence-report.md`（目标函数差异、WCX 口径、试验层级 A/B/C）。  
- **管线设计**：`docs/design/lzdp-file-pipeline-design.md`（含 BitWriter/头对齐说明与指向分析文档的链接）。  
- **自动化测试**：`tests/test_lzdp_memory_streaming_parity.cpp`（CMake 目标 `test_lzdp_memory_streaming_parity`）：**分块不变性**、`compress_dp` 自解压、`decompressFile(WCX)` 闭环；**不以**「WCX payload 与 `compress_dp` 裸流逐字节相同」为通过条件。  
- **总交接索引**（旧）：`docs/handoff_work_summary.md`（2026-05-11，偏 GUI/架构，**不**含本次流式争议全貌）。

---

## 三、**未**在需求方预期下闭环或需接手验证的项

1. **非流式 vs 流式「结果一致」**  
   - 文档与实现均承认：内存 `compress_dp`（**token 数**）与 `LZDP_OutOfCore`（**位代价启发式**）**不承诺**比特级或压缩率一致。  
   - 若产品目标是「流式 ≡ 内存」，需**单独立项**（统一前向目标函数或在外存复现同一递推）。

2. **架构「解耦」**  
   - **未**把分块衔接抽成独立模块；仍为 `handleCollectInput` 内 `match_engine` 分支。**概念上**可分离，**工程上**未重构。

3. **取消 / 磁盘 I/O / 「是否假流式」**  
   - 路径上：`compressFile` 分块读入 + `LZDP_OutOfCore` 仍为**真流式管线**（非整文件缓冲明文）；磁盘指标低**多为** CPU 瓶颈与缓存，**未**对用户环境做对比日志验收。  
   - AUTO + ADE 带参时 **`forced_no_stream`** 仍会走内存路径（`src/gui/ui/worker.py`），易被误判为「没流式」。

4. **大文件 WCX 损坏与压缩率**  
   - 已针对 **BitWriter 头对齐** 与 **flag/非 flag 编码** 修过数轮；**需求方是否在最新构建上复测通过，交接时未确认**。  
   - 若仍失败：需抓取**新版本** WCX、`chunk_size`、算法参数与完整报错栈，继续查 **emit/decompress 与 `compress_dp` 的比特序是否仍不完全一致**、或 **WCX 长度/切片** 问题。

5. **`test_lzdp_memory_streaming_parity` 链接失败**  
   - 部分 Windows 环境下 exe 被占用导致 **Permission denied**；CI/本地需关闭占用进程后重链。**未**保证该测试在仓库默认 CI 中稳定跑通。

---

## 四、建议接手人的工作顺序

1. **与用户对齐验收标准**：比特级一致 / 仅解压正确 / 压缩率差距上限 / 取消 SLA / 是否必须 GUI 日志证明走 `smart_compress_file`。  
2. **用同一文件、同一配置**复现：记录日志关键字 `[compress] STREAMING mode` vs `loaded raw data`；对比 WCX 与内存输出大小及解压结果。  
3. **跑测试**：`test_roundtrip`、`test_lzdp_memory_streaming_parity`（及 WCX 相关 `test_wcx_*`）；大文件可另加脚本压测。  
4. **若仍追求与 `compress_dp` 完全一致**：评估在 EMIT 阶段复用内存版 **同一 BitWriter 编码路径**（或整段 payload 用 `compress_dp` 生成再封装 WCX）— **内存与工程权衡**需产品拍板。

---

## 五、关键路径速查

| 主题 | 路径 |
|------|------|
| 流式压缩入口 | `src/api/api.cpp` · `compressFile` |
| LZDP 外存状态机 | `src/algorithm/LZDP.cpp` · `LZDP_OutOfCore` / `LZDPDecompress_OutOfCore` |
| 工厂 | `src/core/AlgorithmFactory.cpp` |
| GUI 流式 vs 内存 | `src/gui/ui/worker.py` · `single_compress`（`use_streaming`、`forced_no_stream`） |
| 引擎文件管线 | `src/gui/engine/compressor.py` · `smart_compress_file` |
| BitWriter | `src/utils/include/BitWriter.hpp` |
| 流式争议分析 | `docs/analysis/lzdp-nonstreaming-vs-streaming-divergence-report.md` |
| 本交接单 | `docs/handoff_streaming_lzdp_work_2026-05-12.md` |

---

## 六、移交声明

- 本文档仅汇总**可核对**的仓库状态与对话需求；**「问题是否已解决」**以需求方在**当前主分支构建**上的复测结论为准。  
- 若需并入总览，可在 `docs/handoff_work_summary.md` 增加一节链接至本文。
