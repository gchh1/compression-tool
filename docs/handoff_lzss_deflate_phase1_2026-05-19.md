# 工作交接：LZSS / Deflate 流式 Phase1（`algorithm_new`）— 截至 2026-05-19

本文档面向**接手人**：汇总本轮已落地的流式三分块 phase1、相关修复、测试状态，以及**建议续作顺序**。与旧版 `src/algorithm` 流式争议文档区分：本轮工作在 **`src/algorithm_new`** + **`src/api_new`** + GUI 绑定。

**相关文档（勿混读旧路径）**

| 文档 | 用途 |
|------|------|
| [`docs/design/streaming-compression-design.md`](design/streaming-compression-design.md) | 砖块与 §1.1 三分块、§1.20 管线总览 |
| [`docs/handoff_streaming_lzdp_work_2026-05-12.md`](handoff_streaming_lzdp_work_2026-05-12.md) | 旧 `algorithm` LZDP 流式争议（需求方曾认为未闭环） |
| [`docs/handoff_work_summary.md`](handoff_work_summary.md) | 2026-05-11 总索引（GUI/架构） |
| [`docs/debug_plan.md`](debug_plan.md) | DPFlate 矩阵与 P1–P12 记录 |

---

## 一、本轮目标与验收口径

**目标**：LZSS、Deflate 与 LZDP/DPFlate 共用同一套 **Phase1 I/O 壳**（分块读盘 + `VirtualBuffer` + `process_end_exclusive` + `try_release_front_u8`），匹配阶段用 **贪心** `matchAtPosition(..., dp_top=1)`，**不再** `full_input.insert` + 一次性 `greedyWholeInput`。

**验收口径（已达成）**

- 同算法、同参数、同语料：`test_algorithm_new_lzss_deflate` 中 **流式压缩输出 ≡ 内存 `compress_bytes_*` 输出**（比特级），且 roundtrip 正确。
- Deflate Huffman 阶段与 DPFlate 一致：`Deflate::huffmanEncode`（`countFreq` → `buildTree` → `encode`），无 DP backtrack。

**管线对照**

```text
LZDP/DPFlate:  phase1(dpforward) → phase2(backtrack) → Huffman
Deflate:        phase1(greedy)     → Huffman（无 backtrack）
LZSS:           phase1(greedy)     → literalrun? → encoding_triple_lz（无 Huffman）
```

---

## 二、已完成事项（可核对）

### 2.1 共享 Phase1 贪心模块

| 文件 | 说明 |
|------|------|
| `src/algorithm_new/include/GreedyPhase1.hpp` | `process_end_exclusive`、`try_release_front_u8`、`run_phase1_greedy_match` |
| `src/algorithm_new/GreedyPhase1.cpp` | 实现；可选 `temp_triples_path` spill |
| `src/algorithm_new/CMakeLists.txt` | 已加入 `GreedyPhase1.cpp` |

`LZDP.cpp` 已删除重复的匿名命名空间边界函数，改为 `#include "GreedyPhase1.hpp"`。

### 2.2 LZSS / Deflate 流式管线

| 文件 | 行为 |
|------|------|
| `src/algorithm_new/LZSS.cpp` | `compress_file` → `run_phase1_greedy_match` → `literalrun?` → `encoding_triple_lz` |
| `src/algorithm_new/Deflate.cpp` | `compress_file` → phase1 → `huffmanEncode`；非流式仍用 `greedyWholeInput` |

**当前默认**：Phase1 **输入侧流式**；**triples 留在内存**（与 LZSS 相同）。Deflate **未**默认传 `temp_a_path()`（见第三节 spill）。

### 2.3 取消与 API 砖块

- `src/algorithm_new/include/Utils.hpp`：全局 `set_compress_cancel_check` / `throw_if_compress_cancelled*`
- `MatchEngine.hpp` `greedyWholeInput`、`LZDP::dpforward`、phase1 读盘循环等已轮询取消
- `StreamingBrick.hpp/.cpp` + `src/api_new/api.cpp`：砖块收口；`set_streaming_compress_cancel_requested` 挂接

### 2.4 其它已合入修复（影响 GUI / 旧包）

| 问题 | 处理 | 备注 |
|------|------|------|
| imdb notebook 解压约 2 字节 | `Huffman_3HfMTree.hpp` offset/length remainder 公式修复 | **旧 WCX 须重新压缩**；新解码器不能修旧比特流 |
| 解压大小不一致仍报成功 | `src/gui/ui/main_window.py` 改为 **抛错** | |
| `MatchEngine.hpp` 重复 `#include "Utils.hpp"` | 已删重复 | |

### 2.5 测试与构建

```bash
# 从 build 目录
cmake --build . --target test_algorithm_new_lzss_deflate -j 8
./bin/test_algorithm_new_lzss_deflate.exe
# 结果：298 PASS / 0 FAIL（2026-05-19 本地）

cmake --build . --target core_engine -j 8
# 成功；若 Permission denied → 关闭 WebCompress 再链
```

---

## 三、已知未闭环 / 技术债

### 3.1 Triple spill（`temp_triples_path`）— **优先续作**

`run_phase1_greedy_match` 支持将 triple 批次写入临时文件再 `load_triples_from_temp`，用于超大 token 序列降内存。

**现状**

| 场景 | 结果 |
|------|------|
| 无 spill（LZSS / 当前 Deflate） | 测试全过 |
| 有 spill + `binary_64k` | 曾观测 **PASS** |
| 有 spill + `pattern_672` / `text_10k` 等小文件 | **FAIL**：流式输出约 16 字节、解压 0 字节；`tmp.dp` 大小常为 `triple_count × 9` |

**已做但仍不足**

- spill 前 `fs::remove(temp_triples_path)`（避免 `File_Chunk_Writer` **append** 脏数据）
- 写：`record_io::triples_to_u8`（连续比特流）；读：整文件 `u82triple`
- 曾尝试定长 `triple_to_record_bytes` 逐条写 + 按 9 字节切分解析，小文件仍失败

**接手建议**

1. 加 **单元测试**：`greedyWholeInput` triples → spill 写 → load → **逐条比对**（不经过 Huffman），定位是写、读还是仅 Deflate 路径问题。
2. 考虑 `File_Chunk_Writer` 打开时 `trunc`（全局行为需评估 DPFlate/LZDP temp 是否依赖 append）。
3. 大文件 Deflate 若启用 spill：在 `DeflateStreamingPipeline::compress_file` 恢复 `temp_a_path()` 参数，并通过上述测试后再合入。

### 3.2 GUI 阻塞与取消体验（根因已定位，2026-05-21 已修 GIL）

**现象**：大/中文件点「取消」无效；窗口偶发「未响应」；日志里 `set_streaming_compress_cancel` 与 `compress done` 同一秒（作业已结束才设上标志）。

**根因（非 algorithm_new 标志未接）**

| 层 | 说明 |
|----|------|
| C++ 标志 | `api_new::set_streaming_compress_cancel_requested` → `core_new::g_streaming_cancel_flag`；`algorithm_new` 热循环用 `core_new::is_streaming_cancel_requested()` — **接线正确** |
| GUI 路径 | 默认 `threshold_mb=10`：imdb ~3MB 走 **`loaded raw data` + `compressor.compress()`**（内存 LZDP），**不是** `pipeline_compress_file` |
| **GIL** | `LZDPCompressor.compress` 等 pybind **未** `gil_scoped_release`；压缩线程占 GIL 时，取消槽里调用 `set_streaming_compress_cancel_requested` **阻塞排队** → C++ 永远看不到 `True` |
| 取消后处理 | 抛 `cancelled` 时 worker 曾误走 **流式 fallback**；`GuiCompressors` 未把异常收成 `success=false` |

**已做修复（需重编 `core_engine`）**

- `pybind_module.cpp`：`kNativeJobNoGil` 用于各 `*Compressor.compress/decompress` 与 `set_streaming_compress_cancel_requested`
- `GuiCompressors.cpp`：`run_compress_job` 捕获 `cancelled` → `error_message="cancelled"`
- `worker.py`：取消时不 fallback 流式，直接「已取消」

**仍建议**：`GreedyPhase1` 贪心循环补取消轮询；Huffman/编码阶段补检查；小于 10MB 仍整文件进 RAM（阈值/强制流式策略另议）。

### 3.3 LZDP 流式解压失败 / 与内存不一致（2026-05-21 修复）

**现象**：WCX 流式压缩后解压约 2491713 B，WCX 头记录 2996683 B；流式 payload 与内存压缩字节不一致。

**根因 A（解压必挂）**：`lzdp_from_params` 只改了 `window`，**未同步** `encoding.offset_bits/length_bits`。GUI `lookahead_size=31` 时流式仍用默认 `length_bits=8`（对应 255），与内存 `LZDPCompressor.set_lookahead_size` 不一致；`decompressFile` 又用 `nullptr` 参数（按 255 解码）→ 比特流错位。

**根因 B（取消后脏 temp）**：`temp_a.dp` 以 **append** 打开，取消压缩后残留数据污染 phase1。

**修复**：`api_new/api.cpp` 同步位宽；`decompressFile` + pybind + `compressor.py` 传入 `lzdp_whole_file`；流式开始前 `fs::remove(temp_a/temp_b)`。回归：`tests/test_lzdp_imdb_gui_cfg.cpp`（GUI 参数 + imdb 语料）。

**说明**：修后流式与内存压缩大小可能仍差数十字节（分块 `ext_end` 前瞻），但 **roundtrip 应恢复**；旧 WCX 须 **重新压缩**。

### 3.3 旧 WCX / 用户数据

- 用修复前编码器生成的 **Deflate+3HfM** 包：**不能**用新库解压出正确明文；必须 **重新压缩**。
- GUI 日志：`Package/logs/gui.log`；配置：`Package/config/webcompress_settings.json`（`threshold_mb` 等决定 `use_streaming`）。

### 3.4 与 `handoff_streaming_lzdp_work_2026-05-12.md` 的关系

- 该文档描述 **`src/algorithm`** 旧实现争议；**本轮不替代**其结论。
- `algorithm_new` 中 LZDP/DPFlate 流式此前已单独达标（见 `debug_plan`）；本轮补齐 **LZSS/Deflate** 同一 Phase1 壳。

---

## 四、建议接手工作顺序

### P0 — 验证与发布

1. 拉取含 `GreedyPhase1` 的分支，跑 `test_algorithm_new_lzss_deflate`（全矩阵）。
2. 关 GUI 后重编 `core_engine`，替换 `Package` 内 `.pyd`；对大 notebook **重新压缩** 再解压验大小。
3. GUI 实测：大文件压缩时任务管理器内存是否随分块释放；点取消是否在数秒内停下（记录日志关键字）。

### P1 — Triple spill 修通

1. 按 §3.1 加 spill roundtrip 单测（仅 triple，无 Huffman）。
2. 修通后 Deflate 大文件启用 `temp_a_path()`；文档更新 §2.4 Deflate 内存模型。
3. 若 spill 长期不稳定，产品层可约定：仅当 `triple_count * 9` 或估算 RAM 超阈值才 spill。

### P2 — 体验与架构

1. pybind GIL 释放 + 取消 SLA 日志。
2. 解压路径分块写盘与 `streaming-compression-design.md` §1.21 对照验收（`plaintext_flush_chunk_bytes > search_size`）。
3. 在 `handoff_work_summary.md` 增加指向本文的链接。

### P3 — 可选清理

1. `LZSSStreamingPipeline` 中未再使用的 `lzss_` 成员可删（仅风格债）。
2. 统一 `File_Chunk_Writer` 截断语义，避免所有 temp 文件依赖手动 `remove`。
3. ADE / AUTO 路径下 `forced_no_stream` 与 `algorithm_new` 流式阈值文档化（避免用户误判「假流式」）。

---

## 五、关键路径速查

| 主题 | 路径 |
|------|------|
| Phase1 贪心 | `src/algorithm_new/GreedyPhase1.{hpp,cpp}` |
| LZSS 流式 | `src/algorithm_new/LZSS.cpp` |
| Deflate 流式 | `src/algorithm_new/Deflate.cpp` |
| LZDP phase1 参考 | `src/algorithm_new/LZDP.cpp` · `run_phase1_dpforward` |
| 匹配 / 贪心整段 | `src/algorithm_new/include/MatchEngine.hpp` |
| 3HfM 修复 | `src/algorithm_new/include/Huffman_3HfMTree.hpp` |
| 砖块 API | `src/algorithm_new/StreamingBrick.*`、`src/api_new/api.cpp` |
| core 封装 | `src/core_new/LZSScompressor.cpp`、`Deflatecompressor.cpp` |
| GUI worker | `src/gui/ui/worker.py`、`src/gui/engine/compressor.py` |
| 回归测试 | `tests/test_algorithm_new_lzss_deflate.cpp` |
| 流式设计主文档 | `docs/design/streaming-compression-design.md` |

---

## 六、移交声明

- **「LZSS/Deflate 流式 phase1」** 在 `test_algorithm_new_lzss_deflate` 口径下已闭环；**triple spill 与大文件 GUI 复测** 仍待接手。
- 行为以 **当前 Git 源码 + 本地构建** 为准；若与本文冲突，以源码为准。
- 续作完成后请更新本文 §三、§四 或归档为「已解决」附录。
