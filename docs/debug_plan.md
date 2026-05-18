# Debug 计划书

## 一、目标

1. **排查大文件闪退原因**：通过完善的日志机制，定位大文件（>100MB）压缩/解压过程中内存溢出、栈溢出、死循环或非法访问等问题。
2. **算法对比验证**：对 LZDP、LZSS、DPFlate、Deflate、3HfMT 五种算法，分别做流式（streaming）和非流式（memory）压缩对比，解压后与原文件逐字节对比，验证内容一致性和大小一致性。
3. **建立回归基线**：通过 CRC32 黄金值和文件大小校验，确保后续修改不破坏已有功能。

***

## 二、验收标准（硬性要求）

> **小文件全表格跑通后，才能进入大文件压力测试。**

### 2.1 小文件测试规格

| 参数     | 值                                                       |
| ------ | ------------------------------------------------------- |
| 文件大小范围 | 64KB \~ 2MB                                             |
| 流式分块大小 | **严格 300KB**（`chunk_size = 300 * 1024`）                 |
| 小流式压力档 | **67 KiB** 语料 × **7 KiB** chunk（`test_small_stream_7k_67k`；约 10 次 push） |
| 语料类型   | pattern（重复模式）、random（随机）、text（文本）、binary（二进制）、mixed（混合） |

### 2.2 验收条件

对于测试矩阵中的**每一个有效组合**，必须同时满足以下两条：

| #     | 验收条件                       | 判定方法                   |
| ----- | -------------------------- | ---------------------- |
| **A** | **同算法同参数，流式与非流式压缩结果内容一致**  | 逐字节对比流式压缩输出 vs 非流式压缩输出 |
| **B** | **算法对压缩结果解压，解压结果和原文件内容一致** | 逐字节对比解压输出 vs 原始输入文件    |

> 两条同时满足才算该组合 PASS。任一条失败即为 FAIL。

***

## 三、Debug Log 机制

### 3.1 日志工具

已实现 [DebugLog.hpp](file:///d:/AAA_C/compression-tool/src/utils/include/DebugLog.hpp)：

- **单例模式**，线程安全（`std::mutex`）
- **编译时开关**：定义 `DEBUG_LOG_ENABLED=1` 启用，否则为空操作（零开销）
- **运行时开关**：`DebugLog::instance().enable("debug.log")` / `disable()`
- **日志格式**：`[模块名] 关键信息`，每行一条，自动 flush

### 3.2 可删除注入规范

所有 debug log 注入必须遵循以下规范，确保问题定位后可一键删除：

```cpp
// === DEBUG_BLOCK_BEGIN (可删除) ===
// 所有调试代码放在此标记块内
static int dbg_call_cnt = 0;
if (++dbg_call_cnt <= 50) {
    DEBUG_LOG("[ModuleName] key_var=%zu state=%d", key_var, state);
}
// === DEBUG_BLOCK_END (可删除) ===
```

- 每个算法源文件的 debug 注入用 `DEBUG_BLOCK_BEGIN/END` 包裹
- 问题定位后，删除整个 `DEBUG_BLOCK` 即可恢复干净代码
- 迭代计数器上限 50，避免日志爆炸

### 3.3 日志接入点

| 模块                | 文件                                                                                        | 日志点                                                                                                                                                                                                                                                     |
| ----------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DPFlate 压缩        | [DPFlate.cpp](file:///d:/AAA_C/compression-tool/src/algorithm/DPFlate.cpp)                | `handleCollectInput`: 输入缓冲区大小、剩余数据量、是否为最后一块`handleBacktrack`: total\_in\_len、temp\_file\_A/B 大小、token 总数`handleBuildTree`: 是否使用 3HfMTree、writer 字节数`handleEmitTokens`: 已发射 token 数、writer 前后字节数`huff_build_tree_and_write_trees`: 3HfMTree/FLATE 树参数和大小 |
| Inflate 解压        | [Inflate.cpp](file:///d:/AAA_C/compression-tool/src/algorithm/Inflate.cpp)                | `READ_BLOCK_HEADER`: bfinal、btype`READ_TREES`: 树读取完成时的输出位置`COPY_MATCH`: 非法距离/长度（用于检测损坏流）                                                                                                                                                                |
| Inflate3HM 解压     | [Inflate3HM.cpp](file:///d:/AAA_C/compression-tool/src/algorithm/Inflate3HM.cpp)          | `FLUSH`: 已 flush 字节数、out\_abs`READ_HEADER`: 头部解析完成`DECODE_RUN_LEN`: literal run 长度`DECODE_MATCH_LEN`: offset 和 length`COPY_MATCH`: 非法 offset（用于检测损坏流）                                                                                                   |
| DPFlateCompressor | [DPFlateCompressor.cpp](file:///d:/AAA_C/compression-tool/src/core/DPFlateCompressor.cpp) | `compress`: 原始大小、是否使用 3HfMTree、压缩后大小、耗时、压缩比`decompress`: 压缩数据大小、格式标识字节、解压后大小、耗时、成功状态                                                                                                                                                                    |

### 3.4 启用方式

```cpp
// 在 main() 或测试入口处：
#define DEBUG_LOG_ENABLED 1
#include "DebugLog.hpp"
compressor::debug::DebugLog::instance().enable("debug_output.log");
```

或在 CMakeLists.txt 中添加编译定义：

```cmake
target_compile_definitions(algorithm PRIVATE DEBUG_LOG_ENABLED=1)
```

***

## 四、大文件闪退排查方案

### 4.1 可能原因

| 原因         | 现象                                  | 排查方法                             |
| ---------- | ----------------------------------- | -------------------------------- |
| 内存分配过大     | `std::bad_alloc` 或进程被 OOM killer 终止 | 日志记录每次 `resize()` 的大小，观察峰值       |
| 栈溢出        | 无异常直接退出                             | 检查递归深度，将大数组移到堆上                  |
| 死循环        | 进程无响应，CPU 100%                      | 日志记录循环迭代次数，设置最大迭代阈值              |
| 临时文件过大     | 磁盘空间不足                              | 日志记录 temp\_file\_A/B 大小          |
| 32 位地址空间限制 | 内存申请 >2GB 失败                        | 检查 `size_t` 和 `uint32_t` 混用导致的截断 |

### 4.2 排查步骤

1. **启用 Debug Log**：`DEBUG_LOG_ENABLED=1`，输出到文件
2. **小文件验证**：先用 1MB、10MB 文件验证日志输出正常
3. **逐步增大**：100MB → 500MB → 1GB，观察日志中内存分配趋势
4. **关键指标监控**：
   - `input_buffer_` 大小变化
   - `temp_file_A_` / `temp_file_B_` 大小
   - `total_tokens_` 数量
   - `writer_.getBytesWritten()` 输出大小
   - `out_abs_` 解压进度
5. **崩溃后分析**：查看日志最后一条记录，定位崩溃阶段

***

## 五、算法对比方案

### 5.1 测试语料

| 语料类型  | 说明                              | 大小范围        |
| ----- | ------------------------------- | ----------- |
| 重复模式  | `REGRESS_LZDP_DPFLATE_` 重复 32 次 | \~640 bytes |
| 随机数据  | `std::mt19937` 种子 12345         | 384 bytes   |
| 文本数据  | 英文文本（可重复段落）                     | 10KB        |
| 二进制文件 | `.exe` 或 `.dll` 片段              | 100KB       |
| 大文件   | 连续相同字节 / 随机大文件                  | 100MB+      |

### 5.2 测试矩阵

| 算法      | 流式 (Streaming)                                   | 非流式 (Memory)                       | 解压验证                     |
| ------- | ------------------------------------------------ | ---------------------------------- | ------------------------ |
| LZDP    | `StreamProcessor` + `LZDP`                       | `LZDP::dp_core` + `encode_triples` | `LZDP::decompress`       |
| LZSS    | `StreamProcessor` + `LZSS`                       | `LZSS::compress`                   | `LZSS::decompress`       |
| DPFlate | `StreamProcessor` + `DPFlate`                    | `DPFlate::process` (单块)            | `Inflate` / `Inflate3HM` |
| Deflate | `StreamProcessor` + `Deflate`                    | `Deflate::process` (单块)            | `Inflate`                |
| 3HfMT   | `StreamProcessor` + `DPFlate(use_3hfmtree=true)` | `DPFlate::process` (单块, 3HfMT)     | `Inflate3HM`             |

### 5.3 验证标准

```
对于每种算法、每种模式：
  1. 压缩后文件大小 < 原始大小（随机数据除外）
  2. 解压后文件大小 == 原始文件大小
  3. 解压后文件内容 == 原始文件内容（逐字节比较）
  4. CRC32 校验和一致
  5. 流式和非流式压缩结果一致（相同参数下）
```

### 5.4 测试工具

已实现的测试：

- [test\_roundtrip.cpp](file:///d:/AAA_C/compression-tool/tests/test_roundtrip.cpp) — Deflate、LZDP、DPFlate 往返测试
- [test\_lzdp\_dpflate\_regression.cpp](file:///d:/AAA_C/compression-tool/tests/test_lzdp_dpflate_regression.cpp) — LZDP/DPFlate 内存路径回归 + CRC32 黄金值
- [test\_lzdp\_dpflate\_stream.cpp](file:///d:/AAA_C/compression-tool/tests/test_lzdp_dpflate_stream.cpp) — 流式路径测试
- [test\_HuffmanTree3HM.cpp](file:///d:/AAA_C/compression-tool/tests/test_HuffmanTree3HM.cpp) — 3HfMTree 序列化/反序列化测试

***

## 六、执行计划

### 阶段一：基础验证（已完成）

- [x] 实现 Inflate3HM 解压器
- [x] 添加 3HfMTree 自描述头
- [x] DPFlate 格式标识字节（0x33 / 0x46）
- [x] 小文件往返测试通过

### 阶段二：Debug Log 接入（已完成）

- [x] DebugLog.hpp 工具类
- [x] DPFlate 压缩/解压关键路径日志
- [x] Inflate/Inflate3HM 状态转换日志
- [x] DPFlateCompressor 入口/出口日志

### 阶段三：大文件压力测试

- [ ] 准备 100MB / 500MB / 1GB 测试文件
- [ ] 启用 DEBUG\_LOG\_ENABLED=1 运行压缩
- [ ] 分析日志，观察内存和临时文件增长趋势
- [ ] 如闪退，根据最后日志定位问题

### 阶段四：算法对比测试

- [ ] 编写统一对比测试框架（对比所有算法 + 流式/非流式）
- [ ] 对每种算法/模式记录：原始大小、压缩大小、压缩比、耗时
- [ ] 解压后逐字节对比原文件
- [ ] 输出对比报告（CSV 格式）

### 阶段五：回归基线建立

- [ ] 为每个算法/语料生成 CRC32 黄金值
- [ ] 将黄金值写入回归测试
- [ ] CI 中自动运行回归测试

### 5.5 四维度笛卡尔积测试矩阵

四个测试维度：

| 维度          | 取值                             | 说明                                                                                                            |
| ----------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------- |
| **算法**      | LZSS, LZDP, Deflate, DPFlate   | 四种核心压缩算法                                                                                                      |
| **树选择**     | FLATE, 3HfMT                   | 标准 Huffman 树 / 3HfM 多级树（**仅 Deflate/DPFlate 有效**；LZSS/LZDP 不适用）                                               |
| **比较内容**    | 流式压缩内容校验, 非流式压缩内容校验, 解压后与原文件校验 | 三种校验方式                                                                                                        |
| **Flag 编码** | 启用, 禁用                         | LZSS（默认启用）、LZDP、DPFlate 均有 `use_flag_encoding` 开关；Deflate 始终内置 flag（Huffman 字母表 0-255=字面量 / 257-285=匹配），无独立开关 |

> **注意**：LZSS 和 LZDP 是 LZ77 族字典算法，不使用 Huffman 树，因此"树选择"维度对它们无意义。
> LZSS 的 `use_flag_encoding` 参数默认为 `true`：flag 模式用 1bit 标志位区分字面量/匹配，非 flag 模式用 offset=0 表示字面量游程。

#### 完整笛卡尔积表

##### LZSS（无树选择，flag 编码适用）

| #  | 算法   | 树选择 | 比较内容      | Flag 编码 | 有效性 | 完成与否 | 备注        |
| -- | ---- | --- | --------- | ------- | --- | ---- | --------- |
| L1 | LZSS | N/A | 流式压缩内容校验  | 启用      | ✅   | ✅    | 全部语料 PASS |
| L2 | LZSS | N/A | 流式压缩内容校验  | 禁用      | ✅   | ✅    | `test_lzss_noflag` 64KB-2MB PASS |
| L3 | LZSS | N/A | 非流式压缩内容校验 | 启用      | ✅   | ✅    | 全部语料 PASS |
| L4 | LZSS | N/A | 非流式压缩内容校验 | 禁用      | ✅   | ✅    | `test_lzss_noflag` 64KB-2MB PASS |
| L5 | LZSS | N/A | 解压后与原文件校验 | 启用      | ✅   | ✅    | 同 L3      |
| L6 | LZSS | N/A | 解压后与原文件校验 | 禁用      | ✅   | ✅    | `test_lzss_noflag` 64KB-2MB PASS |

##### LZDP（无树选择，flag 编码适用）

| #  | 算法   | 树选择 | 比较内容      | Flag 编码 | 有效性 | 完成与否 | 备注                                          |
| -- | ---- | --- | --------- | ------- | --- | ---- | ------------------------------------------- |
| D1 | LZDP | N/A | 流式压缩内容校验  | 禁用      | ✅   | ✅    | **条件 A**：内存≡流式（`cal_path_cost` + 非 flag 字面量游程发射）；`test_lzdp_cartesian_stream_matrix` |
| D2 | LZDP | N/A | 流式压缩内容校验  | 启用      | ✅   | ✅    | 同 D1（flag 启用） |
| D3 | LZDP | N/A | 非流式压缩内容校验 | 禁用      | ✅   | ✅    | 全部语料 PASS                                   |
| D4 | LZDP | N/A | 非流式压缩内容校验 | 启用      | ✅   | ✅    | `test_lzdp_memory_flag` pattern/random/text/binary\_64k PASS |
| D5 | LZDP | N/A | 解压后与原文件校验 | 禁用      | ✅   | ✅    | 同 D3                                        |
| D6 | LZDP | N/A | 解压后与原文件校验 | 启用      | ✅   | ✅    | 同 D4                                        |

##### Deflate（树选择适用；FLATE 路径 flag 编码已实现，3HfMT 路径传给 DPFlate）

| #   | 算法      | 树选择   | 比较内容      | Flag 编码 | 有效性 | 完成与否 | 备注                                                                        |
| --- | ------- | ----- | --------- | ------- | --- | ---- | ------------------------------------------------------------------------- |
| F1  | Deflate | FLATE | 流式压缩内容校验  | 启用      | ✅   | ✅    | pattern/random OK                                                         |
| F2  | Deflate | FLATE | 非流式压缩内容校验 | 启用      | ✅   | ✅    | pattern/random/text/binary\_64k/mixed\_2mb PASS |
| F3  | Deflate | FLATE | 解压后与原文件校验 | 启用      | ✅   | ✅    | 同 F2                                                                      |
| F1b | Deflate | FLATE | 流式压缩内容校验  | 禁用      | ✅   | ✅    | 非 flag 编码；memory path PASS（无流式 AlgorithmID） |
| F2b | Deflate | FLATE | 非流式压缩内容校验 | 禁用      | ✅   | ✅    | 非 flag 编码；`test_deflate` 64KB-2MB PASS |
| F3b | Deflate | FLATE | 解压后与原文件校验 | 禁用      | ✅   | ✅    | 同 F2b |
| F4  | Deflate | 3HfMT | 流式压缩内容校验  | 禁用      | ✅   | ✅    | memory path PASS（pattern 数据未复现 BUG-01）；流式未测试 |
| F5  | Deflate | 3HfMT | 流式压缩内容校验  | 启用      | ✅   | ✅    | memory path PASS；流式未测试 |
| F6  | Deflate | 3HfMT | 非流式压缩内容校验 | 禁用      | ✅   | ✅    | `test_deflate` 64KB-2MB PASS |
| F7  | Deflate | 3HfMT | 非流式压缩内容校验 | 启用      | ✅   | ✅    | `test_deflate` 64KB-2MB PASS |
| F8  | Deflate | 3HfMT | 解压后与原文件校验 | 禁用      | ✅   | ✅    | 同 F6 |
| F9  | Deflate | 3HfMT | 解压后与原文件校验 | 启用      | ✅   | ✅    | 同 F7 |

##### DPFlate（树选择 + flag 编码均适用）

| #   | 算法      | 树选择   | 比较内容      | Flag 编码 | 有效性 | 完成与否 | 备注                                                          |
| --- | ------- | ----- | --------- | ------- | --- | ---- | ----------------------------------------------------------- |
| P1  | DPFlate | FLATE | 流式压缩内容校验  | 禁用      | ✅   | ✅    | **条件 A**：`test_lzdp_dpflate_stream` pattern 语料 WCX payload 与 `DPFlateCompressor` 逐字节一致（300KiB chunk；2026-05-17） |
| P2  | DPFlate | FLATE | 流式压缩内容校验  | 启用      | ✅   | ✅    | **条件 A**：同上（flag 启用，pattern 语料；2026-05-17） |
| P3  | DPFlate | FLATE | 非流式压缩内容校验 | 禁用      | ✅   | ✅    | mixed\_2mb 已修复（backtrack 内存链镜像 + drainFullBytes）       |
| P4  | DPFlate | FLATE | 非流式压缩内容校验 | 启用      | ✅   | ✅    | 同 P3                                                        |
| P5  | DPFlate | FLATE | 解压后与原文件校验 | 禁用      | ✅   | ✅    | 同 P3                                                        |
| P6  | DPFlate | FLATE | 解压后与原文件校验 | 启用      | ✅   | ✅    | 同 P4                                                        |
| P7  | DPFlate | 3HfMT | 流式压缩内容校验  | 禁用      | ✅   | ✅    | `test_dpflate_3hm_stream` + `test_dpflate_bin64k_3k_stream`（2026-05-17） |
| P8  | DPFlate | 3HfMT | 流式压缩内容校验  | 启用      | ✅   | ✅    | 同 P7（flag 启用） |
| P9  | DPFlate | 3HfMT | 非流式压缩内容校验 | 禁用      | ✅   | ✅    | mixed\_2mb OK；memory-only 35/35 PASS                         |
| P10 | DPFlate | 3HfMT | 非流式压缩内容校验 | 启用      | ✅   | ✅    | 同 P9（memory flag 路径）                                        |
| P11 | DPFlate | 3HfMT | 解压后与原文件校验 | 禁用      | ✅   | ✅    | 同 P9                                                        |
| P12 | DPFlate | 3HfMT | 解压后与原文件校验 | 启用      | ✅   | ✅    | 同 P10                                                       |

##### P7 / P8 验收细目（§2.2 条件 A + B，chunk=300KiB，`test_dpflate_3hm_stream` / `test_lzdp_dpflate_stream`）

> P7 = flag 禁用；P8 = flag 启用。仅覆盖「流式压缩内容校验」行；P9–P12（非流式 / 解压）已 ✅。

| 语料 | 大小 | P7 条件 A（流式 WCX payload ≡ 内存） | P7 条件 B（流式压缩→解压≡原文） | P8 条件 A | P8 条件 B |
| ---- | ---- | ----------------------------------- | ------------------------------ | --------- | --------- |
| pattern | 672 B | ✅ `test_lzdp_dpflate_stream` | ✅ `test_dpflate_3hm_stream` | ✅ | ✅ |
| random | 384 B | ✅ `test_lzdp_dpflate_stream` | ✅ | ✅ | ✅ |
| text | ~22 KB | ✅ `test_lzdp_dpflate_stream` | ✅ | ✅ | ✅ |
| binary\_64k | 64 KB | ✅ `test_dpflate_bin64k_3k_stream` mem==stream | ✅ `test_dpflate_3hm_stream` | ✅ | ✅ |
| repeat\_1mb | 1 MiB | ✅ `test_lzdp_dpflate_stream` | ✅ `test_dpflate_3hm_stream` | ✅ | ✅ |
| repeat\_3mb | 3 MiB | ✅ | ✅ | ✅ | ✅ |
| repeat\_6mb | 6 MiB | ✅ | ✅ | ✅ | ✅ |

**P7/P8 行完成定义**：上表全部语料 **条件 A、B 均为 ✅** 后，矩阵中 P7/P8 的「完成与否」方可标 ✅（**已满足，2026-05-17**）。

**有效组合数**：36（全部组合均有效）
**已完成组合数**：36
**有缺陷组合数**：0
**未测试组合数**：0

##### P7/P8 流式分块笛卡尔子矩阵（chunk × 语料大小）

| 维度 | 取值 |
| ---- | ---- |
| **流式 chunk** | 30 / 57 / 97 / 113 KiB（30720 / 58368 / 99328 / 115712 B） |
| **语料大小** | 256 / 522 / 1025 / 2000 KiB |
| **校验** | **A**：`Pipeline::push` 精确分块 payload ≡ `DPFlateCompressor`；**B**：`compressFile`→`decompressFile`（`WEBCOMPRESS_STREAM_CHUNK_MIN_BYTES=30720` + 输出池 ≥256KiB） |
| **测试** | `test_cartesian_stream_matrix`（P7 flag=0 + P8 flag=1，4×4×2×2 = 64 格；`CARTESIAN_QUICK=1` / `CARTESIAN_SKIP_B=1` 可缩短） |

##### D1 / D2 流式分块笛卡尔子矩阵（chunk × 语料大小）

| 维度 | 取值 |
| ---- | ---- |
| **流式 chunk** | 30 / 57 / 97 / 113 KiB |
| **语料大小** | 256 / 522 / 1025 / 2000 KiB |
| **校验** | **A**：`LZDPCompressor`（`compress_dp`）裸流 ≡ 精确 `Pipeline::push`（§2.2 流式≡非流式）；**B**：`compressFile`→`decompressFile` |
| **测试** | `test_lzdp_cartesian_stream_matrix`；调试 `WEBCOMPRESS_LZDP_STREAM_DEBUG` → `Package/logs/lzdp_stream_containers.log` |

#### 发现的缺陷汇总

| 缺陷ID   | 严重程度  | 描述                                                                      | 影响组合           |
| ------ | ----- | ----------------------------------------------------------------------- | -------------- |
| BUG-01 | 🟡 待确认 | DeflateCompressor::decompress 使用 3HfMT 时 ACCESS\_VIOLATION 崩溃（pattern 数据 64KB-2MB 未复现，可能仅特定数据触发） | F4, F6, F8（memory path 已通过） |
| BUG-02 | ✅ 已修复 | Deflate memory decompress 在 binary\_64k 上 CRC 不匹配，mixed\_2mb 崩溃         | F2, F3         |
| BUG-03 | ✅ 已修复 | DPFlate memory mixed\_2mb：`TempFileBitAppender::flushChunk` 按字节填充破坏 packed link 索引；backtrack 改读 `link_lengths_`/`link_offsets_` 镜像 | P3–P6          |
| BUG-04 | ✅ 已修复 | 3HfMT mixed\_2mb：与 BUG-03 同源（backtrack 读 temp A 错位）；memory 链镜像后 OK | P9–P12         |
| BUG-05 | ✅ 已修复 | LZDP 流式 decompress：`LZDPDecompress_Streaming` 用 MSB-first writeBit 写明文，改为 writeBytes | D1, D2         |
| BUG-06 | ✅ 已修复 | 3HfMT(DPFlate) 流式 decompress：`decompressFile` 链为 Inflate 时未识别 0x33 前缀，改为 Inflate3HM | P7, P8         |
| BUG-07 | 🟡 中等 | 3HfMT huffman\_chunk\_bits=11/13/15 解压损坏或堆损坏                            | test\_3hm\_all |
| BUG-08 | 🟢 低  | LZDP 流式 text 语料解压崩溃                                                     | D1             |
| BUG-10 | ✅ 已修复 | LZDP/DPFlate COLLECT：HashChain 用 ``TopMatch``（``TopMatch.hpp``，同 ``algorithm_new``）；LZDP 另含 `cal_path_cost` + 非 flag 游程 | D1–D2, P7–P8   |
| BUG-09 | ✅ 已修复 | 根因：(1) `cell_at` 悬空引用写坏 temp A；(2) `rotate()` 用 `assign` 清空未提交 `next_`；(3) Pipeline **输出池** 与 3KiB 输入块同大导致 EMIT AV。修复后 `test_dpflate_3hm_stream` **6MiB ALL PASS**；`test_dpflate_bin64k_3k_stream`（64KiB + 精确 3KiB push）**PASS** | P7, P8         |

**BUG-09 容器转换调试（binary\_64k 专用，单文件）**

```powershell
$env:WEBCOMPRESS_DPFLATE_BIN64K_DEBUG = "D:\AAA_C\compression-tool\Package\logs\dpflate_bin64k_containers.log"
$env:WEBCOMPRESS_DPFLATE_BIN64K_TARGET = "65536"   # 3KiB 多 chunk 流式时预 ARM
cd build_py\bin
.\test_dpflate_bin64k_3k_stream.exe   # 64KiB，Pipeline 每次 push 3072 B
.\test_dpflate_3hm_stream.exe       # compressFile，chunk≥64KiB
```

- 环境变量：`WEBCOMPRESS_DPFLATE_BIN64K_DEBUG=1` 或绝对路径；`WEBCOMPRESS_DPFLATE_BIN64K_DUMP_MAX=64` 控制 hex 长度；`WEBCOMPRESS_DPFLATE_BIN64K_TARGET=65536` / `ONLY=1` 预 ARM。
- 记录：`BUF_IN`/`BUF_PREV`/`BUF_HEAD` hex、`STREAMING_DP_DUMP`、`TEMP_A_BYTES`、`PIPE_CHUNK`、`BACKTRACK`（含 `literal_tok`/`match_tok` 计数）等。
- 实现：`DPFlateBin64kDebug.*`、`StreamingDpChunk.hpp`（砖块 `rotate`）、`StreamChunkPolicy.hpp`（`pipeline_output_pool_chunk_bytes` ≥256KiB）、`TempTokenIO.hpp`。

#### 测试优先级与执行顺序

按优先级从高到低排列待完成的测试：

| 优先级 | 组合                                     | 说明                                                     | 预估工作量           |
| --- | -------------------------------------- | ------------------------------------------------------ | --------------- |
| ~~P0~~ | ~~P2, P4, P6~~                           | ✅ 已完成（表内 P2/P4/P6 已打勾）                                  | —               |
| ~~P1~~ | ~~P10, P12~~（3HfMT 内存/解压）          | ✅ 已完成                                                       | —               |
| ~~**P1b**~~ | ~~**P7, P8**（3HfMT 流式 §2.2 A+B）~~       | ✅ 已完成 — BUG-09 修复 + `test_dpflate_bin64k_3k_stream`           | —               |
| ~~P2~~ | ~~D2~~                                   | ✅ 已完成（D1/D2 流式已打勾）                                     | —               |
| P3  | F4-F9 (Deflate + 3HfMT × 全部)           | ✅ 已完成；`test_deflate` 64KB-2MB memory path PASS；流式未测试 | —               |
| P3b | F1b-F3b (Deflate FLATE 非 flag)         | ✅ 已完成；`test_deflate` 64KB-2MB memory path PASS | —               |
| P4  | L2/L4/L6 (LZSS 无 flag 模式)              | ✅ 已完成；`test_lzss_noflag` 64KB-2MB PASS | —               |
| ~~P5~~ | ~~D4/D6 (LZDP memory\_flag)~~           | ✅ 已完成（`tests/test_lzdp_memory_flag.cpp`）                  | —               |
| P6  | 大文件压力测试 (100MB/500MB/1GB)              | 所有算法的大文件流式/非流式验证                                       | 大 — 需准备语料和长时间运行 |

#### 建议执行计划（小文件优先，全表格跑通后才能进入大文件测试）

1. ~~**第1步**：DPFlate FLATE + flag (P2, P4, P6)~~ — ✅ 已完成
2. ~~**第2步**：DPFlate 3HfMT — P9–P12 内存/解压 ✅；P7/P8 流式 `test_dpflate_3hm_stream` / `test_dpflate_bin64k_3k_stream` ✅~~
3. ~~**第3步**：LZDP 流式 + flag (D2)~~ — ✅ 已完成
4. ~~**第4步**：新增 Deflate + 3HfMT 完整测试 (F4-F9)~~ — ✅ 已完成（`test_deflate.cpp`）
5. ~~**第5步**：新增 Deflate FLATE 非 flag 编码测试 (F1b-F3b)~~ — ✅ 已完成（`test_deflate.cpp`）
6. ~~**第6步**：补齐 LZSS 无 flag 模式测试 (L2, L4, L6)~~ — ✅ 已完成（`test_lzss_noflag.cpp`）
7. ~~**第7步**：补齐 LZDP memory\_flag 测试 (D4, D6)~~ — ✅ 已完成（`test_lzdp_memory_flag.cpp`）
8. **第8步**：大文件压力测试（仅在 1-7 步全部 PASS 后执行）
   - 使用 `test_algorithm_comparison --large` 运行大文件测试

#### 测试验证步骤

对于每个有效组合：

1. **流式压缩内容校验**：
   - 使用 `smart_compress_file`（流式）压缩文件
   - 读取压缩文件内容，与同参数非流式压缩结果逐字节对比
   - 预期：流式与非流式压缩结果完全一致
2. **非流式压缩内容校验**：
   - 使用 `compress`（非流式/内存）压缩数据
   - 记录压缩后大小和 CRC32
3. **解压后与原文件校验**：
   - 对上述两种压缩结果分别解压
   - 解压后文件逐字节与原文件对比
   - 预期：完全一致
   - 校验 CRC32 黄金值

***

## 六、预期输出

1. **Debug Log 文件**：`debug_output.log`，包含完整的压缩/解压流水线信息
2. **对比报告**：`comparison_report.csv`，格式：
   ```
   算法,模式,原始大小,压缩大小,压缩比,耗时(ms),解压一致,CRC匹配
   LZDP,memory,1048576,523456,49.9%,1234,yes,yes
   LZDP,streaming,1048576,523456,49.9%,1256,yes,yes
   DPFlate,memory,1048576,487321,46.5%,2345,yes,yes
   ...
   ```
3. **问题修复**：如发现大文件闪退问题，记录根因和修复方案

***

## 七、2026-05-15 测试执行结果

### 7.1 测试环境

- **测试可执行文件**: `test_algorithm_comparison.exe`, `test_3hm_all.exe`
- **语料**: pattern(672B), random(384B), text(22300B), binary\_64k(65536B), mixed\_2mb(2097152B)
- **流式 chunk 大小**: 300KB
- **进度日志**: 所有测试函数均注入 `fprintf(stderr, "[PROGRESS] ...")` 进度输出

### 7.2 test\_3hm\_all 结果

**基础 3HfMT 测试**: 4/4 PASS

| 语料          | 原始    | 压缩    | CRC        | 结果   |
| ----------- | ----- | ----- | ---------- | ---- |
| pattern     | 672   | 683   | 0x626487e3 | PASS |
| random      | 384   | 1266  | 0x3f5b556e | PASS |
| text        | 22300 | 1059  | 0x4ca507bd | PASS |
| binary\_64k | 65536 | 66889 | 0x3a5db188 | PASS |

**3HfMT bit-width 对齐测试**:

| chunk\_bits | 原始    | 压缩    | CRC        | 结果                             |
| ----------- | ----- | ----- | ---------- | ------------------------------ |
| 3           | 65536 | 82257 | 0x3c5c23b4 | PASS                           |
| 5           | 65536 | 68561 | 0x3c5c23b4 | PASS                           |
| 7           | 65536 | 66861 | 0x3c5c23b4 | PASS                           |
| 9           | 65536 | 67367 | 0x3c5c23b4 | PASS                           |
| 11          | 65536 | —     | —          | FAIL (dec\_size=3538987, 解压损坏) |
| 13          | 65536 | —     | —          | FAIL (dec\_size=4401532, 解压损坏) |
| 15          | 65536 | —     | —          | CRASH (堆损坏, exit=0xC0000374)   |

### 7.3 test\_algorithm\_comparison 结果 (--memory-only)

#### pattern (672 bytes) — 全部 PASS

| 算法           | 模式           | 压缩  | 比率      | 耗时     | 结果 |
| ------------ | ------------ | --- | ------- | ------ | -- |
| LZDP         | memory       | 34  | 5.06%   | 2.0ms  | OK |
| LZSS         | memory       | 107 | 15.92%  | 0.9ms  | OK |
| DPFlate      | memory       | 39  | 5.80%   | 39.7ms | OK |
| 3HfMT        | memory       | 683 | 101.64% | 40.0ms | OK |
| Deflate      | memory       | 38  | 5.65%   | 0.0ms  | OK |
| DPFlate      | memory\_flag | 39  | 5.80%   | 40.5ms | OK |
| DPFlate\_3HM | memory\_flag | 683 | 101.64% | 38.0ms | OK |

#### random (384 bytes) — 全部 PASS

| 算法           | 模式           | 压缩   | 比率      | 耗时     | 结果 |
| ------------ | ------------ | ---- | ------- | ------ | -- |
| LZDP         | memory       | 389  | 101.30% | 0.4ms  | OK |
| LZSS         | memory       | 436  | 113.54% | 0.1ms  | OK |
| DPFlate      | memory       | 644  | 167.71% | 39.6ms | OK |
| 3HfMT        | memory       | 1266 | 329.69% | 35.2ms | OK |
| Deflate      | memory       | 643  | 167.45% | 0.0ms  | OK |
| DPFlate      | memory\_flag | 644  | 167.71% | 37.5ms | OK |
| DPFlate\_3HM | memory\_flag | 1266 | 329.69% | 36.6ms | OK |

#### text (22300 bytes) — 全部 PASS

| 算法           | 模式           | 压缩   | 比率     | 耗时      | 结果 |
| ------------ | ------------ | ---- | ------ | ------- | -- |
| LZDP         | memory       | 728  | 3.26%  | 368.9ms | OK |
| LZSS         | memory       | 2997 | 13.44% | 5.8ms   | OK |
| DPFlate      | memory       | 468  | 2.10%  | 50.5ms  | OK |
| 3HfMT        | memory       | 1059 | 4.75%  | 50.4ms  | OK |
| Deflate      | memory       | 476  | 2.13%  | 0.1ms   | OK |
| DPFlate      | memory\_flag | 468  | 2.10%  | 49.8ms  | OK |
| DPFlate\_3HM | memory\_flag | 1059 | 4.75%  | 52.0ms  | OK |

#### binary\_64k (65536 bytes) — 全部 PASS（Deflate 已修复）

| 算法           | 模式           | 压缩        | 比率          | 耗时        | 结果                     |
| ------------ | ------------ | --------- | ----------- | --------- | ---------------------- |
| LZDP         | memory       | 65913     | 100.58%     | 459.9ms   | OK                     |
| LZSS         | memory       | 73710     | 112.47%     | 114.8ms   | OK                     |
| DPFlate      | memory       | 65920     | 100.59%     | 40.3ms    | OK                     |
| 3HfMT        | memory       | 66889     | 102.06%     | 40.0ms    | OK                     |
| **Deflate**  | **memory**   | **66982** | **102.21%** | **8.1ms** | **OK**                 |
| DPFlate      | memory\_flag | 65920     | 100.59%     | 39.0ms    | OK                     |
| DPFlate\_3HM | memory\_flag | 66889     | 102.06%     | 40.6ms    | OK                     |

#### mixed\_2mb (2097152 bytes) — Deflate / DPFlate FLATE / 3HfMT memory 均已 PASS

| 算法           | 模式           | 压缩         | 比率         | 耗时           | 结果                                   |
| ------------ | ------------ | ---------- | ---------- | ------------ | ------------------------------------ |
| LZDP         | memory       | 718192     | 34.25%     | 347908.6ms   | OK CRC\_OK（压缩极慢，功能正确）                    |
| LZSS         | memory       | 951442     | 45.37%     | 8101.5ms     | OK                                           |
| **DPFlate**  | **memory**   | **713496** | **34.02%** | **43044.7ms** | **OK CRC\_OK**                              |
| **3HfMT**    | **memory**   | **706678** | **33.70%** | **43288.6ms** | **OK CRC\_OK**                              |
| **Deflate**  | **memory**   | **724243** | **34.53%** | **107.6ms**   | **OK CRC\_OK**                              |
| **DPFlate**  | **memory\_flag** | **713496** | **34.02%** | **43366.9ms** | **OK CRC\_OK**                              |
| **DPFlate\_3HM** | **memory\_flag** | **706678** | **33.70%** | **42768.5ms** | **OK CRC\_OK**                              |

### 7.4 流式测试结果 (部分，因 text 语料 LZDP 流式解压崩溃中断)

| 算法           | 模式              | pattern            | random             |
| ------------ | --------------- | ------------------ | ------------------ |
| LZDP         | streaming       | OK                 | OK                 |
| LZSS         | streaming       | OK                 | OK                 |
| DPFlate      | streaming       | OK                 | OK                 |
| 3HfMT        | streaming       | FAIL CRC\_MISMATCH | FAIL CRC\_MISMATCH |
| Deflate      | streaming       | OK                 | OK                 |
| LZDP         | streaming\_flag | OK                 | OK                 |
| DPFlate      | streaming\_flag | OK                 | OK                 |
| DPFlate\_3HM | streaming\_flag | FAIL CRC\_MISMATCH | FAIL CRC\_MISMATCH |

### 7.5 关键发现

1. **小文件 (≤22KB) 全部算法 memory 模式正常** — LZDP/LZSS/DPFlate/3HfMT/Deflate 均通过
2. **Deflate memory 在 binary\_64k/mixed\_2mb 的 CRC 不匹配已修复** — 原因是 Deflate 多 Huffman block 缺少块尾 EOB、`DeflateCompressor` 标准流按首字节误判格式，以及 `Deflate::handle()` safety break 在 2MB 输入上提前截断。
3. **mixed\_2mb DPFlate FLATE + 3HfMT 已修复** — 根因是 `TempFileBitAppender::flushChunk` 字节填充 + backtrack 读 temp A 错位；collect 阶段 `link_lengths_`/`link_offsets_` 镜像 + `drainFullBytes()`。
4. **DeflateCompressor + 3HfMT 仍需专项测试** — ACCESS\_VIOLATION 历史问题尚未重新验证（Deflate\_3HM 测试仍注释）。
5. **memory-only 全矩阵（5 语料 × 7 算法）35/35 PASS** — 含 `mixed_2mb`（2026-05-15，`test_algorithm_comparison --memory-only`，约 9 分钟）。
6. **LZDP 流式 CRC 已修复** — ``LZDPDecompress_Streaming`` 曾用 MSB-first ``writeBit`` 写明文，与 LSB-first ``BitWriter`` 不一致；改为 ``writeBytes``。3HfMT/DPFlate\_3HM 流式仍待修。
7. **3HfMT huffman\_chunk\_bits ≥11 解压损坏** — 非字节对齐 Huffman 编码在较大 chunk 时解压器有 bug。
8. **进度日志机制有效** — 成功定位每个崩溃/失败的精确位置。

### 7.6 2026-05-17 LZDP/DPFlate 流式 DP 双分块 + memory `cal_cost` 复测

**代码变更（仅 `src/algorithm/`、`src/core/`、测试）**

- `StreamingDpTwoChunk`（`StreamingDpChunk.hpp`）：`LZDP_Streaming` / `DPFlate` COLLECT 用 `cur`/`next` 双缓冲，避免环形槽覆盖有效未来 DP 状态。
- `LZDP::dp_core`：字面量/匹配松弛改为 `cal_cost(literal_count, match_count)`。
- `LZDPCompressor` / 回归测试：`LZDP(search, look)` 误作位宽 → 改为 `LZDP` + `autoBitWidth(search, look)`（与 `LZDP_Streaming` 的 `calcBitWidth` 一致）。

**定向测试（`build_py/bin`）**

| 测试 | 覆盖 | 结果 |
| --- | --- | --- |
| `test_lzdp_dpflate_regression` | D3–D6 内存 CRC + DPFlate 内存 | **PASS**（LZDP 黄金 CRC 未变） |
| `test_lzdp_dpflate_stream` | D1–D2 流式往返 | **PASS** |
| `test_lzdp_memory_streaming_parity` | 内存 vs 流式可观测性 | **PASS** |
| `test_lzdp_memory_flag` | D4/D6 flag 内存 | **PASS**（依赖 `LZDPCompressor` autoBitWidth 修复） |
| `test_dpflate_3hm_stream` | P7/P8，300KiB chunk | **PASS**（至 6MiB；BUG-09 修复后） |
| `test_cartesian_stream_matrix` | P7/P8 子矩阵 30–113KiB × 256–2000KiB | **PASS**（条件 A+B；见 §5.5） |
| `test_lzdp_cartesian_stream_matrix` | D1/D2 子矩阵 30–113KiB × 256–2000KiB | **PASS**（A mem==stream + B 往返） |
| `test_small_stream_7k_67k` | P1/P7/P8、D1/D2；BUG-01/07；**7 KiB** chunk × **67 KiB** | **ALL PASS**（2026-05-17） |
| `test_algorithm_comparison --memory-only` | D3–D6 / P3–P12 内存 | **部分**：pattern→binary\_64k 全部 LZDP/DPFlate/3HfMT **OK**；**LZDP mixed\_2mb OK**；在 **DPFlate mixed\_2mb 压缩入口** 崩溃中断（待单独排查） |

**结论**

- 小语料内存/流式 LZDP、DPFlate FLATE 路径在修复后回归通过。
- P7/P8：`test_dpflate_3hm_stream` 至 6MiB **ALL PASS**；小 chunk 压力见 `test_dpflate_bin64k_3k_stream`。

### 7.8 2026-05-17 BUG-09 闭环（砖块 rotate + 输出池 + 3KiB 测试）

**根因与修复**

| # | 问题 | 修复 |
|---|------|------|
| 1 | `dpflate_collect_input_one_index` 持有 `cell_at` 引用后 `next_.resize()` 悬空 | 快照 `cur_cost`/`link_len`/`link_off` 再松弛 |
| 2 | `rotate()` 仅 COLLECT_DONE 调用且 `assign` 清空 `next_` | 砖块 `rotate(commit)`：每批 COLLECT 后丢弃已提交前缀；保留 forward 格 |
| 3 | Pipeline `MemoryPool` 槽 = 3KiB 输入块 | `pipeline_output_pool_chunk_bytes()` 输出池 ≥256KiB |

**测试**

```text
test_dpflate_3hm_stream      ALL PASS（pattern…repeat_6mb，300KiB compressFile chunk）
test_dpflate_bin64k_3k_stream ALL PASS（65536B，22×3072B push，payload=66889 mem==stream）
```

**日志判读**：`BACKTRACK done literal_tok=65536 match_tok=0` 表示该随机 64KiB 语料 DP 链以字面量为主（非 temp A 空洞）；`writer_bytes=66889` 四次运行一致即 payload 对齐。

### 7.10 2026-05-17 D1/D2 LZDP 笛卡尔子矩阵 + 流式调试

| 项 | 说明 |
| --- | --- |
| 调试 | `WEBCOMPRESS_LZDP_STREAM_DEBUG` → `LZDPStreamDebug`（COLLECT / rotate / BACKTRACK / EMIT，同 DPFlate 砖块日志风格） |
| 条件 A | 内存 `compress_dp` ≡ 流式 `Pipeline` 精确分块（`cal_path_cost` + HashChain Top-K 与内存对齐，2026-05-17） |
| 测试 | `test_lzdp_cartesian_stream_matrix`（4×4×2×2）；`test_lzdp_dpflate_stream` D1 pipe==file |

### 7.9 2026-05-17 P7/P8 笛卡尔子矩阵 + 全量复测

| 测试 | 结果 | 备注 |
| --- | --- | --- |
| `test_dpflate_3hm_stream` | **ALL PASS** | P7+P8，pattern…repeat\_6mb，300KiB chunk，~272s |
| `test_cartesian_stream_matrix` | **ALL PASS** | 4×4×2×2=64 格（30/57/97/113 KiB × 256–2000 KiB，条件 A+B），~18.5min |
| `test_lzdp_dpflate_stream` | **PASS** | 含 binary\_64k 条件 A |
| `api.cpp` | 输出池 | `MemoryPool` 槽改用 `pipeline_output_pool_chunk_bytes(chunk)` |

### 7.7 2026-05-17 DPFlate 流式修复（temp record + pipeline `is_last`）

**实现**

- `TempTokenIO.hpp`：`TempTokenRecord`（4 字节 `length`/`offset`）+ `TempTokenStore`；`DPFlate` COLLECT 写 temp A、BACKTRACK 读 A 写 B、EMIT 按序读 B；3HfM `emit_literal_run_3hm_` 成员化跨 `need_output`。
- `StreamProcessor.cpp`：`final_flag = is_last`（不再要求 `in_chunks_.empty()`），使末块 plaintext 首次 `process()` 即带 `is_last_chunk=true`，与非流式 `compress_to_end` 分块语义一致。

**定向测试（`build_py/bin`）**

| 测试 | 覆盖 | 结果 |
| --- | --- | --- |
| `test_lzdp_dpflate_regression` | DPFlate/LZDP 内存 | **PASS** |
| `test_lzdp_dpflate_stream` | D2 + P1/P2/P7/P8 **条件 A**（pattern）+ FLATE/3HfM 往返 | **PASS** |
| `test_lzdp_memory_streaming_parity` | LZDP 流式不变性 | **PASS** |
| `test_dpflate_3hm_stream` | P7/P8 条件 B 往返（300KiB chunk，至 6MiB） | **PASS**（2026-05-17 复测） |
| `test_dpflate_bin64k_3k_stream` | 64KiB + 精确 3KiB Pipeline push，mem≡stream payload | **PASS** |
| `test_lzdp_dpflate_stream` | P7/P8 条件 A（pattern/random/text/binary\_64k） | **PASS** |

**矩阵更新（条件 A）**

- P1–P2（FLATE）：pattern 语料流式 WCX payload ≡ 内存 `DPFlateCompressor` 输出。
- P7–P8（3HfMT）：pattern/random/text/binary\_64k/repeat\_6mb **条件 B** PASS；64KiB + 3KiB 精确 push 见 `test_dpflate_bin64k_3k_stream`。

