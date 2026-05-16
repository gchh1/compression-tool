# 流式压缩设计文档

---

# 一、设计砖块

本章将流式压缩系统的各个设计概念解耦为独立"砖块"。每个砖块描述一个独立的设计决策与接口，不耦合到具体算法。后续算法章节引用这些砖块，说明各算法使用了哪些砖块及如何组合。

---

## 1.1 三分块设计（匹配阶段）

### 适用场景

**所有需要 LZ77 匹配的算法**在匹配阶段都需要三分块。无论是 DP 最优匹配还是贪心最长匹配，都需要在 search window 中查找匹配，且需要 lookahead 来确定完整匹配长度。

| 算法 | 匹配方式 | 三分块形态 |
|------|---------|-----------|
| LZDP | DP 最优匹配 | `input_buffer_` 可变长缓冲区 |
| DPFlate | DP 最优匹配 | `input_buffer_` 可变长缓冲区 |
| Deflate | 贪心最长匹配 | `window_` 滑动窗口 |
| LZSS | 贪心最长匹配 | 全文件内存（静态方法天然完整窗口） |

### 概念模型

3 个分块各司其职，构成一个滑动窗口：

| 分块 | 角色 | 用途 |
|------|------|------|
| prev_chunk | 历史窗口（search window） | 为 current_chunk 开头提供已处理字节作为字典 |
| current_chunk | 正在处理 | 指针从位置 0 推进到末尾 |
| new_chunk | 前瞻窗口（lookahead） | 为 current_chunk 末尾提供未处理字节，确定完整匹配长度 |

**为什么需要 3 个分块？**

- **prev（search window）**：位置 `i` 需要向前搜索已处理字节作为字典——没有 prev 会导致块开头匹配断档。
- **new（lookahead）**：位置 `i` 在块末尾时，匹配可能延伸到块外——没有 lookahead 无法确定完整匹配长度（贪心）或评估未来代价（DP）。

### 实现形态一：`input_buffer_` 可变长缓冲区

**适用算法**：LZDP、DPFlate

```
input_buffer_ 结构:
[0 ...... keep_start_idx ...... current_i_ ...... current_i_+LOOKAHEAD ...... size())
 ├─ search window ─┤├──── 当前处理 ────┤├── lookahead ──┤├── 新读入 ──┤
    (prev 角色)        (current 角色)     (new 角色)
```

| 变量 | 含义 |
|------|------|
| `input_buffer_` | 可变长缓冲区，包含 search window + 当前数据 + lookahead |
| `window_abs_pos_` | 当前窗口中第一个字节在原始文件中的绝对位置 |
| `current_i_` | 当前处理指针在 `input_buffer_` 中的偏移 |
| `SEARCH_SIZE` | 最大搜索距离 |
| `LOOKAHEAD_SIZE` | 最大前瞻距离 |

**滑动逻辑**：尾部追加新数据 → 处理 `processable = size - LOOKAHEAD - current_i_` 字节 → 头部释放 `SEARCH_SIZE` 之前的字节（`erase`）。

### 实现形态二：`window_` 滑动窗口

**适用算法**：Deflate

```
window_ 结构（WINDOW_SIZE = 2 × SLIDE_SIZE）:
[0 ............. SLIDE_SIZE ............. 2×SLIDE_SIZE)
 ├─ search window ─┤├── current + lookahead ──┤
     (prev 角色)          (current + new 角色)
                          ↑
                       cursor_ 指向当前处理位置
```

| 变量 | 含义 |
|------|------|
| `window_` | 定长 `2×SLIDE_SIZE` 缓冲区 |
| `cursor_` | 当前处理位置 |
| `lookahead_` | 未处理字节数 |
| `SLIDE_SIZE` | 滑动窗口半长（= search window 大小） |

**滑动逻辑**：`cursor_ >= SLIDE_SIZE` 时触发 `slideWindow()`：`memcpy` 后半→前半，`cursor_ -= SLIDE_SIZE`，哈希链指针同步偏移。

### 实现形态三：全文件内存

**适用算法**：LZSS（静态方法）

LZSS 的 `compress()` 是静态方法，接收完整 `vector<uint8_t>`，天然拥有完整的三分块语义——search window 和 lookahead 都完整无缺。流式场景通过 `StreamingCompressAdapter`（§1.7）包装。

---

### 边界处理

三分块的核心难点在于**块边界处如何保持匹配连续性**。以下按三种实现形态分别说明。

#### 形态一（`input_buffer_`）边界处理

```
块 N 处理完毕时:
  input_buffer_ = [prev_N | current_N | new_N | 新读入数据]
                                   ↓
块 N+1 开始时:
  keep_start = max(0, next_abs_pos - SEARCH_SIZE)
  input_buffer_.erase(begin → keep_start)   ← 释放 SEARCH_SIZE 之前的字节
  window_abs_pos_ += keep_start              ← 更新绝对位置
  current_i_ -= keep_start                   ← 调整处理指针
  reseedHashChainPrefix(input_buffer_.size()) ← 重建 search window 的哈希链
```

| 边界场景 | 行为 |
|---------|------|
| **首块** | `window_abs_pos_ = 0`，search window 为空。`current_i_` 从 0 开始，仅当前块数据可用作字典 |
| **中间块** | 保留上一块末尾 `SEARCH_SIZE` 字节作为 search window，`reseedHashChainPrefix` 重建哈希链 |
| **末块** | `is_last_chunk = true`，`LOOKAHEAD_SIZE` 约束解除，`processable = input_buffer_.size() - current_i_` |
| **小文件（< chunk_size）** | 单块处理，search window 和 lookahead 都在同一块内，退化为全文件内存语义 |
| **文件末尾不足 LOOKAHEAD** | `processable` 计算中 `LOOKAHEAD_SIZE` 自动缩减为剩余字节数，不阻塞 |

**哈希链跨块维护**：

```cpp
// 每块处理完毕后，search window 保留在 input_buffer_ 头部
// 下一块开始时重建哈希链，确保 search window 内的位置可被匹配
auto reseedHashChainPrefix(size_t end_exclusive) -> void {
    std::fill(head_.begin(), head_.end(), UINT32_MAX);
    for (size_t i = 0; i + 2 < end_exclusive; ++i) {
        const size_t slot = hashBucket3(i);
        prev_buf_[i] = head_[slot];
        head_[slot] = static_cast<uint32_t>(i);
    }
}
```

#### 形态二（`window_`）边界处理

```
cursor_ >= SLIDE_SIZE 时触发 slideWindow():

  滑动前: [0 ......... SLIDE_SIZE ......... 2×SLIDE_SIZE)
           ├─ search window ─┤├── current + lookahead ──┤
                              ↑ cursor_
  滑动后: [0 ......... SLIDE_SIZE ......... 2×SLIDE_SIZE)
           ├─ search window ─┤├── current + lookahead ──┤
           ↑ cursor_ -= SLIDE_SIZE
```

| 边界场景 | 行为 |
|---------|------|
| **首块** | `fillWindow()` 从 reader 读入数据填充 `window_`，`cursor_ = SLIDE_SIZE`，search window 为空 |
| **中间块** | `slideWindow()` 将后半复制到前半，保留 `SLIDE_SIZE` 字节 search window，哈希链指针同步偏移 |
| **末块** | `is_last_chunk = true`，`lookahead_` 耗尽后进入 BUILD_TREE |
| **小文件** | 单次 `fillWindow()` 即可覆盖全部数据，无需 `slideWindow()` |

**哈希链跨块维护**：

```cpp
auto slideWindow() -> void {
    // 1. 移动窗口数据
    memcpy(window_.data(), window_.data() + SLIDE_SIZE, SLIDE_SIZE);
    cursor_ -= SLIDE_SIZE;
    lookahead_ += SLIDE_SIZE;  // 滑动后 lookahead 空间扩大

    // 2. 同步调整哈希链指针
    for (size_t i = 0; i < HASH_SIZE; ++i) {
        if (head_[i] != NULL_PTR) {
            head_[i] = (head_[i] >= SLIDE_SIZE) ? (head_[i] - SLIDE_SIZE) : NULL_PTR;
        }
    }
    for (size_t i = 0; i < SLIDE_SIZE; ++i) {
        if (prev_[i] != NULL_PTR) {
            prev_[i] = (prev_[i] >= SLIDE_SIZE) ? (prev_[i] - SLIDE_SIZE) : NULL_PTR;
        }
    }
}
```

#### 形态三（全文件内存）边界处理

无边界问题——整块数据在内存中，search window 和 lookahead 天然完整。

---

### 中断信号与现场保存

三分块匹配阶段是**有状态的**——哈希链、search window、pending 表、temp 文件游标构成 L3 编解码现场。中断信号到来时，这些状态**不可安全冻结**。

#### 取消检查点位置

```
handleCollectInput 入口:
   ① 检查 cancel flag → 若拉高则设置 status.done=true 并返回
   ② 读入新数据
   ③ DP 推进循环（每 2048 次迭代检查一次 cancel flag）
   ④ 处理完毕 → 状态转移
```

```cpp
auto handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void {
    // ① 取消检查点
    if (g_cancel_callback && g_cancel_callback()) {
        status.done = true;
        return;
    }

    // ... 读入数据、DP 推进 ...

    // ② DP 循环内定期检查
    for (size_t i = 0; i < processable; ++i) {
        if ((i & 2047) == 0 && g_cancel_callback && g_cancel_callback()) {
            status.done = true;
            return;
        }
        // DP 推进 ...
    }
}
```

#### 可保存的现场（L2 管线政务）

| 字段 | 来源 | 说明 |
|------|------|------|
| `bytes_read` | `window_abs_pos_ + current_i_` | 已读入的原始字节数 |
| `total_in_len_` | 末块确定后设置 | 输入总长度 |
| `current_state` | `state_` 枚举 | 当前状态机阶段（COLLECT_INPUT / BACKTRACK / EMIT） |
| `algorithm_params` | 构造参数 | SEARCH_SIZE、LOOKAHEAD_SIZE、编码模式等 |

#### 不可保存的现场（L3 编解码）

| 组件 | 原因 |
|------|------|
| `dp_states_` 环形 DP 列 | 跨块推进的中间状态，序列化后无法保证与 temp 文件一致 |
| `prev_buf_` / `head_` 哈希链 | 依赖 `input_buffer_` 的绝对索引，取消后重建成本等于重跑 |
| temp 文件 A / B 游标 | 外存文件与内存状态耦合，版本化成本极高 |
| `pending_` 表 | 跨块未完成的 DP 推进，语义复杂 |

#### 取消后策略

**整次重跑**（参考 [`streaming-interrupt-checkpoint-design.md`](./streaming-interrupt-checkpoint-design.md) §4）：

```
取消 → 写入 CheckpointManifest（L2）→ 删除 .part → 后续重跑整次 pipeline_compress_file
```

不尝试从半截状态续压（L3 非目标）。

---

## 1.2 二分块设计（建树 / 回溯阶段）

### 适用场景

Huffman 建树和 DP 回溯阶段**不需要 LZ77 匹配**，因此不需要 search window 和 lookahead。

| 阶段 | 目的 | 分块策略 | 原因 |
|------|------|---------|------|
| **Huffman 建树** | 统计 token 频率，构建 Huffman 树 | **二分块** | 频率统计是无状态聚合，不依赖块边界 |
| **DP 回溯** | 从 temp 文件读取 token 序列，反向发码 | **二分块** | 纯顺序读取，每个 token 独立编码 |

### 概念模型

```
二分块结构:
┌──────────────────┬──────────────────┐
│   current_chunk  │  output_buffer   │
│   (当前处理数据)   │  (编码输出)       │
└──────────────────┴──────────────────┘
```

### Huffman 建树（二分块）

频率统计是**无状态聚合**：每个 token 独立计数，不需要前后文。分块只是为了避免一次性加载全部 token 到内存——每块统计完释放，只保留累加计数器。

```
while (has_more_tokens):
    chunk = read_token_chunk()
    for token in chunk:
        freq[token.symbol]++
buildHuffmanTree(freq)
```

### DP 回溯（二分块）

回溯从 temp 文件**顺序读取** token 序列，反向遍历发码。不需要 search window，不需要 lookahead——每个 token 独立编码。

```
while (has_more_tokens):
    chunk = read_token_chunk_from_temp()
    for token in reversed(chunk):
        encodeToken(token, writer)
```

### 二分块 vs 三分块

| 维度 | 三分块（匹配阶段） | 二分块（建树/回溯） |
|------|------------------|-------------------|
| 窗口数 | 3（prev + current + new） | 2（current + output） |
| search window | 需要（跨块匹配） | 不需要 |
| lookahead | 需要（完整匹配长度/未来代价） | 不需要 |
| 跨块状态 | 哈希链、search window 保留 | 累加计数器（建树）或无状态（回溯） |
| 块边界影响 | 无 lookahead 则匹配截断 | 无影响（每块独立处理） |

---

### 边界处理

二分块的核心特征是**无状态跨块**——每个 chunk 独立处理，块边界只传递累加计数器（建树）或不传递任何状态（回溯）。

#### Huffman 建树边界

```
块 N:  token_chunk_N → 统计频率 → freq_map += chunk_freq → 释放 chunk
块 N+1: token_chunk_{N+1} → 统计频率 → freq_map += chunk_freq → 释放 chunk
...
末块:  最后 chunk 统计完毕 → freq_map 完整 → buildHuffmanTree(freq_map)
```

| 边界场景 | 行为 |
|---------|------|
| **首块** | `freq_map` 从零开始累加 |
| **中间块** | 每块统计完释放 token chunk，只保留 `freq_map` 累加器 |
| **末块** | 最后一块统计完毕后触发 `buildHuffmanTree()` |
| **空块** | 无 token 的 chunk 直接跳过，累加器不变 |

**关键约束**：频率累加是交换律的——chunk 处理顺序不影响最终频率表。因此块边界对建树结果**无影响**。

#### DP 回溯边界

```
块 N:  从 temp A 逆向读 chunk → 正向写 temp B → 释放 chunk
块 N+1: 继续逆向读下一 chunk → 正向写 temp B → 释放 chunk
...
末块:  最后 chunk 回溯完毕 → 进入 EMIT_TOKENS
```

| 边界场景 | 行为 |
|---------|------|
| **首块** | 从 temp A 末尾开始逆向读取 |
| **中间块** | 连续逆向读取，块边界只传递 temp A 文件游标 |
| **末块** | 回溯到 temp A 头部后进入 EMIT_TOKENS |
| **单块** | 整次回溯在一个 chunk 内完成，无需跨块 |

**关键约束**：回溯是纯顺序读取——每个 token 独立编码，不依赖相邻 token。因此块边界对回溯结果**无影响**。

#### 发码（EMIT_TOKENS）边界

```
块 N:  从 temp B 正向读 chunk → 编码输出 → 释放 chunk
块 N+1: 继续正向读下一 chunk → 编码输出 → 释放 chunk
...
末块:  最后 chunk 发码完毕 → 写入结束标记 → DONE
```

| 边界场景 | 行为 |
|---------|------|
| **首块** | 从 temp B 头部开始正向读取 |
| **中间块** | 连续正向读取，块边界只传递 temp B 文件游标 |
| **末块** | 发码完毕后写入 EOB / 终止符 |

---

### 中断信号与现场保存

二分块阶段是**无状态或弱状态**的——每个 chunk 独立处理，不维护跨块的数据结构。中断信号到来时，现场保存比三分块简单。

#### 取消检查点位置

```
建树循环:
   ① 检查 cancel flag
   ② 读 token chunk
   ③ 统计频率
   ④ 循环

回溯循环:
   ① 检查 cancel flag
   ② 从 temp A 读 chunk
   ③ 逆向回溯，写 temp B
   ④ 循环

发码循环:
   ① 检查 cancel flag
   ② 从 temp B 读 chunk
   ③ 编码输出
   ④ 循环
```

```cpp
// 建树阶段取消检查点
auto handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void {
    if (g_cancel_callback && g_cancel_callback()) {
        status.done = true;
        return;
    }
    // ... 读 token chunk、统计频率 ...
}

// 回溯阶段取消检查点
auto handleBacktrack(AlgorithmStatus& status, bool is_last_chunk) -> void {
    if (g_cancel_callback && g_cancel_callback()) {
        status.done = true;
        return;
    }
    // ... 从 temp A 读 chunk、回溯 ...
}
```

#### 可保存的现场（L2 管线政务）

| 字段 | 来源 | 说明 |
|------|------|------|
| `bytes_read` | 已处理的输入字节数 | 从三分块阶段传递 |
| `freq_map` / `dist_freq` | 建树累加器 | 已统计的 token 频率（可序列化） |
| `total_tokens_` | 回溯阶段 | 已回溯的 token 总数 |
| `emitted_tokens_` | 发码阶段 | 已编码输出的 token 数 |
| `current_state` | `state_` 枚举 | 当前阶段（BACKTRACK / BUILD_TREE / EMIT） |

#### 不可保存的现场（L3 编解码）

| 组件 | 原因 |
|------|------|
| BitWriter 未封口比特 | 跨字节边界的中途比特，续压时无法对齐 |
| Huffman 树（已建） | 建树完成后可重新构建，无需保存 |
| temp 文件 A / B 游标 | 外存文件位置，取消后整次重跑 |

#### 取消后策略

与三分块阶段一致——**整次重跑**（参考 [`streaming-interrupt-checkpoint-design.md`](./streaming-interrupt-checkpoint-design.md) §4）：

```
取消 → 写入 CheckpointManifest（L2）→ 删除 .part → 后续重跑整次 pipeline_compress_file
```

二分块阶段虽然状态比三分块简单，但取消后仍选择整次重跑而非续压，因为：
1. 建树阶段取消时频率表可能不完整，续压需要重新读取全部 token——等于重跑
2. 回溯/发码阶段取消时 temp 文件游标与内存状态耦合，续压成本高于重跑
3. 统一策略（整次重跑）降低实现复杂度

---

## 1.3 推进式 DP（Push-style DP）

### 适用算法

LZDP、DPFlate

### 核心特性

dp[i] **不依赖**序号小于 i 的 dp 值。dp[i-1] 的决策会"向前推送"到 dp[i+L-1]：

```
dp[0] = 0  // 起点

for i = 1 to n:
    base = dp[i-1].tokennum

    // 字面量：推到 i
    dp[i].tokennum = min(dp[i].tokennum, base + 1)
    dp[i].match = (offset=0, length=1)

    // 匹配：推到 i+L-1
    for each match (offset, L):
        target = i + L - 1
        dp[target].tokennum = min(dp[target].tokennum, base + 1)
        dp[target].match = (offset, L)
```

关键特性：
- dp[i] 只从 dp[i-1] 获得 base tokennum
- dp[i] 的决策向前推送，不影响已处理的 dp[0..i-1]
- 这使流式分块成为可能——每个分块独立推进，跨分块用 pending 表衔接

### 代价模型

DP 的目标是最小化压缩输出总比特数。推进式 DP 在比较转移时使用**固定位宽代理**（建树前即可常数时间比较）：

```
字面量一步  ≈ 8 bits（non-flag）或 9 bits（flag）
匹配一步    ≈ Ob + Lb bits（non-flag）或 1 + Ob + Lb bits（flag）
```

其中 `Ob = ceil(log2(search_size + 1))`，`Lb = ceil(log2(lookahead_size + 1))`。

### Pending 表（跨分块状态衔接）

流式分块中，分块 A 末尾的 DP 推进可能"推入"分块 B 的范围。这些未完成的推进记录在 pending 表中，在分块 B 开头被消费。pending 表大小 ≤ lookahead_size。

---

## 1.4 状态机驱动模式

### 适用算法

LZDP、DPFlate、Deflate

### 统一入口

所有流式算法通过 `handle(AlgorithmStatus&, bool is_last_chunk)` 统一入口，内部 `while(true)` 循环推进状态：

```cpp
auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        (this->*kStateHandlers[static_cast<size_t>(state_)])(status, is_last_chunk);
        if (status.need_input || status.need_output || status.done) {
            return;  // 让出控制权，等待 Pipeline 满足 I/O 需求后再次调用
        }
    }
}
```

### AlgorithmStatus 字段

| 字段 | 含义 |
|------|------|
| `need_input` | 需要更多输入数据 |
| `need_output` | 输出缓冲区有数据待消费 |
| `done` | 压缩完成 |
| `bytes_consumed` | 本轮消费的输入字节数 |
| `bytes_produced` | 本轮产生的输出字节数 |

---

## 1.5 编码方案

编码方案是将 token 序列转为比特流的策略，**与算法原生决策（Triple）解耦**。

### 方案 A：Flag 编码

```
字面量: [flag=1 : 1 bit][byte : 8 bits]          = 9 bits
匹配:   [flag=0 : 1 bit][offset : Ob][length : Lb] = 1+Ob+Lb bits
```

### 方案 B：Non-flag 编码（offset=0 哨兵）

```
字面量游程: [offset=0 : Ob][run_len : Lb][byte_0 : 8]...[byte_N-1 : 8]
匹配:       [offset>0 : Ob][length : Lb]
```

### 适用算法

| 算法 | Flag 编码 | Non-flag 编码 |
|------|----------|--------------|
| LZDP | ✓ | ✓ |
| DPFlate | ✓ | ✓ |
| Deflate | ✓（Huffman 字母表内置） | ✓（bit-packed offset/length） |
| LZSS | ✓ | ✓ |

---

## 1.6 Huffman 树策略

编码方案之上，还有两种树策略决定如何构建 Huffman 树：

| 树策略 | 树结构 | 总符号数 | 适用编码 |
|--------|--------|---------|---------|
| FLATE | 字面量/长度树(286) + 距离树(30) | 316（固定） | Flag / Non-flag |
| 3HfMTree | 字面量树(256) + offset 树(2^{b_o}) + length 树(2^{b_l}) | 256 + 2^{b_o} + 2^{b_l} | Flag / Non-flag |

**FLATE**：继承自 Deflate 标准，使用码-附加值两级编码。长度码 257-285，距离码 0-29。

**3HfMTree**：固定槽宽多级查询。`offset_huffman_bwidth` 和 `length_huffman_bwidth` 为固定位宽，槽数上界分别为 `2^{b_o}` 和 `2^{b_l}`。一次 match 对 offset 树贡献 `ceil(Ob / b_o)` 次查询、对 length 树贡献 `ceil(Lb / b_l)` 次查询。

### 适用算法

| 算法 | FLATE | 3HfMTree |
|------|-------|----------|
| DPFlate | ✓ | ✓ |
| Deflate | ✓ | ✓（通过 DPFlate 内核） |
| LZDP | — | —（无 Huffman 层） |
| LZSS | — | —（无 Huffman 层） |

---

## 1.7 StreamingCompressAdapter（流式包装器）

### 适用算法

LZSS（静态方法，非状态机算法）

### 设计

对于**非状态机算法**（如 LZSS 静态方法），通过 `StreamingCompressAdapter` 统一包装为流式接口：

```
输入:  原始字节流
处理:  按 chunk_size_ 切分为独立分块，每块独立调用 compress_fn_
输出:  [u32 BE chunk_len][chunk_data]...[u32 BE 0]（终止符）
```

```cpp
auto StreamingCompressAdapter::process(span<const uint8_t> read,
                                        span<uint8_t> write,
                                        bool is_last_chunk) -> AlgorithmStatus {
    // 1. 累积输入
    input_buffer_.insert(input_buffer_.end(), read.begin(), read.end());

    // 2. 按 chunk_size_ 切分，每块独立压缩
    while (input_buffer_.size() >= chunk_size_) {
        auto chunk = take_front(chunk_size_);
        auto compressed = compress_fn_(chunk);
        emitChunk(compressed);  // u32 BE len + data
    }

    // 3. 最后一块 + 终止符
    if (is_last_chunk && !input_buffer_.empty()) {
        auto compressed = compress_fn_(input_buffer_);
        emitChunk(compressed);
        emitTerminator();  // u32 BE 0
    }
}
```

**注意**：`StreamingCompressAdapter` 的简单分块（每块独立压缩，块边界处窗口重置）会导致跨块匹配丢失。对于需要跨块匹配连续性的场景，应使用状态机实现（§1.4）而非此适配器。

---

## 1.8 内存管理

### 三分块释放规则（`input_buffer_` 形态）

```cpp
// 每次 handleCollectInput 处理完毕（非最后一块）:
size_t keep_start_idx = (next_abs_pos > SEARCH_SIZE) ? (next_abs_pos - SEARCH_SIZE) : 0;
if (keep_start_idx > 0) {
    input_buffer_.erase(begin, begin + keep_start_idx);
    prev_buf_.erase(begin, begin + keep_start_idx);
    window_abs_pos_ = keep_start_abs;
    current_i_ -= keep_start_idx;
}
```

### 三分块释放规则（`window_` 形态）

```cpp
// cursor_ >= SLIDE_SIZE 时触发:
memcpy(window_.data(), window_.data() + SLIDE_SIZE, SLIDE_SIZE);
cursor_ -= SLIDE_SIZE;
// 哈希链指针同步偏移
```

### 二分块释放规则

```cpp
// 每次 chunk 处理完毕:
output.flush()
free(cur_chunk)
cur_chunk = next_chunk
```

### 内存上限

任何时候内存中最多有：
- **Phase 1（`input_buffer_` 形态）**：`input_buffer_`（≈ SEARCH_SIZE + chunk_size + LOOKAHEAD_SIZE）+ `prev_buf_`（等长）+ `dp_states_`（≈ 2×LOOKAHEAD_SIZE 槽位）
- **Phase 1（`window_` 形态）**：`window_`（= 2×SLIDE_SIZE）+ `head_`/`prev_` 哈希表
- **Phase 2**：2 个 temp 文件分块在内存中滚动（二分块）

---

## 1.9 滑动窗口解压

### 适用算法

Inflate（Deflate 解压）、Inflate3HM（3HfMTree 解压）

### 核心设计

解压侧的 match copy 需要访问已解码的历史字节。滑动窗口方案用一个定长环形缓冲区 `window_`（32KB）保存最近解码的字节，通过 `out_abs_ % kWindowSize` 取模寻址：

```
window_ 结构（kWindowSize = 32768）:
[0 ...................................... 32768)
 ├─ 已解码历史字节（环形覆盖）──────────────┤
 
out_abs_: 全局已解码字节计数（单调递增，永不归零）
window_[out_abs_ % kWindowSize] = 新解码字节
```

**match copy 逻辑**：

```cpp
// pending_dist_: match 的向后距离
// pending_length_: match 长度
const uint64_t base = out_abs_;  // copy 开始时的全局位置
for (size_t i = 0; i < pending_length_; ++i) {
    const uint64_t src_abs = base - pending_dist_ + i;
    uint8_t b = window_[src_abs % kWindowSize];  // 环形取模
    output_buf_.push_back(b);
    window_[out_abs_ % kWindowSize] = b;          // 同时写入窗口
    ++out_abs_;
}
```

**关键约束**：`pending_dist_ ≤ kWindowSize`（32KB），即 match 不能引用超过 32KB 之前的数据。这是 Deflate RFC1951 的规定，也是滑动窗口方案能工作的前提。

### 输出缓冲与 flush

解码字节先写入 `output_buf_`，当 `output_buf_.size() >= 32768` 时触发 `FLUSH_TO_WRITER` 状态，将缓冲字节通过 `writer_.writeBits()` 输出后清空：

```
DECODE_TOKENS → (output_buf_ 满 32KB) → FLUSH_TO_WRITER → DECODE_TOKENS
```

这保证了解压输出的流式性——不需要等全部解码完才输出。

### 内存占用

| 组件 | 大小 |
|------|------|
| `window_` | 32KB（定长环形缓冲区） |
| `output_buf_` | ≤32KB（满即 flush） |
| Huffman 树 | 动态（FLATE: 2 棵树，3HfMTree: 3 棵树） |

---

## 1.10 全量累积解压

### 适用算法

LZDPDecompress_OutOfCore

### 为什么不能滑动窗口

LZDP 的 match offset 是相对于**完整已解码流开头**的绝对偏移，而非相对于当前位置的滑动窗口距离。这意味着：

```
LZDP match:  copy from output_buffer_[output_buffer_.size() - offset]
Inflate match: copy from window_[(out_abs_ - dist) % kWindowSize]
```

LZDP 的 offset 可以指向任意远的历史位置（受 `offset_bits_` 位宽限制，最大 `2^Ob - 1`），远超 32KB。因此 `output_buffer_` 必须保留全部已解码字节，**从不收缩**。

### 输出策略

`output_buffer_` 累积全部解压数据，通过 `output_flush_idx_` 追踪已输出位置：

```cpp
// 解码循环：不断往 output_buffer_ 追加
output_buffer_.push_back(lit);           // 字面量
output_buffer_.push_back(output_buffer_[copy_start + k]);  // match copy

// 输出：从 output_flush_idx_ 开始写到 writer_
while (output_flush_idx_ < output_buffer_.size()) {
    writer_.writeBits(output_buffer_[output_flush_idx_], 8);
    ++output_flush_idx_;
}
```

**注意**：虽然输出是增量的（`output_flush_idx_` 推进），但 `output_buffer_` 本身从不释放前缀——因为后续 match 可能引用任意历史位置。

### 内存占用

| 组件 | 大小 |
|------|------|
| `output_buffer_` | **全量解压数据**（最大瓶颈） |

这是 LZDP 解压的全文件进内存瓶颈。对于大文件，`output_buffer_` 会增长到与原始文件等大。

---

## 1.11 StreamingDecompressAdapter（解压侧流式包装器）

### 适用算法

LZSSDecompress（静态方法，非状态机解压器）

### 设计

与压缩侧的 `StreamingCompressAdapter`（§1.7）对称。对于**非状态机解压器**（如 `LZSS::decompress` 静态方法），通过 `StreamingDecompressAdapter` 统一包装为流式接口：

```
输入:  [u32 BE chunk_len][chunk_data]...[u32 BE 0]（终止符）
处理:  逐帧解析 → 每帧独立调用 decompress_fn_ → 拼接输出
输出:  原始字节流
```

```cpp
auto StreamingDecompressAdapter::tryDecodeNextChunk() -> bool {
    if (input_buffer_.size() < 4) return false;

    uint32_t chunk_sz = decodeU32BE(input_buffer_.data());

    if (chunk_sz == 0) {           // 终止符
        input_buffer_.erase(..., 4);
        finished_ = true;
        return false;
    }

    if (input_buffer_.size() < 4 + chunk_sz) return false;  // 数据不完整

    auto chunk_data = take(4 + chunk_sz);   // 取出完整一帧
    auto decompressed = decompress_fn_(chunk_data);  // 独立解压
    output_buffer_.insert(..., decompressed);        // 拼接到输出
    return true;
}
```

**关键特性**：
- 每帧独立解压——帧之间无状态共享
- 输入是 `StreamingCompressAdapter` 或 `WholeFileFramedCompressAdapter` 的输出
- 终止符 `[u32 BE 0]` 标记流结束
- 输出拼接：各帧解压结果按顺序拼接为完整原始数据

---

## 1.12 解压状态机

### 与压缩状态机的对比

| 维度 | 压缩状态机 | 解压状态机 |
|------|-----------|-----------|
| 输入 | 原始字节流 | 压缩比特流 |
| 输出 | 压缩比特流 | 原始字节流 |
| 分块策略 | 三分块（匹配）/ 二分块（建树/回溯） | 滑动窗口（Inflate）/ 全量累积（LZDP） |
| 状态数 | 3-4（MATCH→BACKTRACK→BUILD_TREE→EMIT） | 4-7（取决于算法复杂度） |
| 跨块状态 | 哈希链、search window、pending 表 | `window_` + `out_abs_`（Inflate）/ `output_buffer_`（LZDP） |

### 统一入口

与压缩侧相同，所有解压器通过 `handle(AlgorithmStatus&, bool is_last_chunk)` 统一入口，内部 `while(true)` 循环推进状态。`need_input` / `need_output` / `done` 控制让出。

### 解压状态机通用模式

```
READ_HEADER → DECODE_LOOP → FLUSH → DONE
                  ↑            │
                  └────────────┘
```

- **READ_HEADER**：解析压缩头（位宽、编码模式、Huffman 树等）
- **DECODE_LOOP**：循环解码 token（字面量 / match），写入输出
- **FLUSH**：输出缓冲区满时刷新到 writer_
- **DONE**：输入耗尽且输出全部 flush 完毕

---

# 二、算法实现

本章每个算法一节。每节首先列出该算法使用的砖块，然后给出状态机设计和解耦的流式框架架构图。

---

## 2.1 LZDP

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| 三分块（§1.1） | `input_buffer_` 可变长缓冲区形态（COLLECT_INPUT） |
| 二分块（§1.2） | DP 回溯（BACKTRACK）+ 发码（EMIT_TOKENS） |
| 推进式 DP（§1.3） | DP 核，固定位宽代价，`dp_core()` 纯函数 |
| 状态机（§1.4） | 3 状态驱动 |
| 编码方案（§1.5） | Flag / Non-flag 可选 |
| 内存管理（§1.8） | 三分块 + 二分块释放规则 |

> LZDP 无 Huffman 层，因此不使用 Huffman 树策略（§1.6）。

### 状态机

```
COLLECT_INPUT → BACKTRACK → EMIT_TOKENS
```

| 状态 | 分块策略 | 职责 |
|------|---------|------|
| `MATCH` | **三分块**（`input_buffer_`） | 读入原始数据，DP 推进，写 temp A |
| `BACKTRACK` | **二分块**（temp A 逆向读） | 从 temp A 逆向回溯，正向写 temp B |
| `EMIT` | **二分块**（temp B 正向读） | 从 temp B 正向读 token，编码输出 |

### 解耦的流式框架

```
┌─────────────────────────────────────────────────────────────┐
│                      LZDP 流式框架                           │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  三分块管理（input_buffer_ 可变长缓冲区）               │   │
│  │  - 尾部追加新数据（reader_.readBytes）                 │   │
│  │  - 保留 LOOKAHEAD_SIZE 字节作为 lookahead              │   │
│  │  - 处理完毕后头部释放 SEARCH_SIZE 之前字节（erase）     │   │
│  │  - window_abs_pos_ + current_i_ 追踪位置              │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  哈希链管理（head_/prev_buf_ + reseedHashChainPrefix） │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  状态机调度                                           │   │
│  │  MATCH → BACKTRACK → EMIT                           │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Temp File 管理（temp A / temp B）                    │   │
│  │  - Phase 1: spill_a_ 写 packed link 到 temp A        │   │
│  │  - Phase 2: 逆向读 temp A → 正向写 temp B             │   │
│  │  - Phase 2: 正向读 temp B → 编码输出                  │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键设计点**：
- MATCH 使用三分块：`input_buffer_` 中 search window + current + lookahead window 三段合一
- BACKTRACK 和 EMIT 使用二分块：从 temp 文件逐块读取，不需要 search window 和 lookahead window
- DP 推进是纯函数 `dp_core()`，与流式框架解耦

---

## 2.2 DPFlate

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| 三分块（§1.1） | `input_buffer_` 可变长缓冲区形态（MATCH） |
| 二分块（§1.2） | Huffman 建树（BUILD_TREE）+ DP 回溯（BACKTRACK）+ 发码（EMIT） |
| 推进式 DP（§1.3） | DP 核，固定位宽代价 |
| 状态机（§1.4） | 4 状态驱动 |
| 编码方案（§1.5） | Flag / Non-flag 可选 |
| Huffman 树策略（§1.6） | FLATE / 3HfMTree 可选 |
| 内存管理（§1.8） | 三分块 + 二分块释放规则 |

### 状态机

```
COLLECT_INPUT → BACKTRACK → BUILD_TREE → EMIT_TOKENS
```

| 状态 | 分块策略 | 职责 |
|------|---------|------|
| `MATCH` | **三分块**（`input_buffer_`） | 读入原始数据，DP 推进，写 temp A |
| `BACKTRACK` | **二分块**（temp A 逆向读） | 从 temp A 逆向回溯，正向写 temp B，**同时统计 Huffman 频率** |
| `BUILD_TREE` | **非分块阶段** | 由频率表构建 Huffman 树（FLATE 或 3HfMTree） |
| `EMIT` | **二分块**（temp B 正向读） | 从 temp B 正向读 token，Huffman 编码输出 |

### 解耦的流式框架

```
┌─────────────────────────────────────────────────────────────┐
│                     DPFlate 流式框架                         │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  三分块管理（input_buffer_ 可变长缓冲区）               │   │
│  │  同 LZDP §2.1                                        │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  哈希链管理（head_/prev_buf_ + reseedHashChainPrefix） │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  状态机调度                                           │   │
│  │  MATCH → BACKTRACK → BUILD_TREE → EMIT             │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Huffman 熵层（与匹配引擎解耦）                        │   │
│  │  - BACKTRACK 中累加频率（huff_backtrack_accumulate）  │   │
│  │  - BUILD_TREE 中建树（huff_build_tree_and_write）     │   │
│  │  - EMIT 中发码（huff_emit_token_stream）             │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**与 LZDP 的关键区别**：
- 多了 BUILD_TREE 状态：BACKTRACK 中累加 Huffman 频率，BUILD_TREE 中建树
- EMIT 使用 Huffman 编码（而非 LZDP 的固定位宽编码）
- 支持 FLATE 和 3HfMTree 两种树策略

---

## 2.3 Deflate

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| 三分块（§1.1） | `window_` 滑动窗口形态（MATCH） |
| 二分块（§1.2） | Huffman 建树（BUILD_TREE） |
| 状态机（§1.4） | 3 状态驱动 |
| 编码方案（§1.5） | Flag（Huffman 字母表内置）/ Non-flag（bit-packed）可选 |
| Huffman 树策略（§1.6） | FLATE（直接）/ 3HfMTree（通过 DPFlate 内核） |
| 内存管理（§1.8） | `window_` 形态释放规则 |

> Deflate 使用贪心匹配（非 DP），因此不使用推进式 DP（§1.3）。不需要 temp 文件——匹配后直接收集 token，建树后直接发码。

### 状态机

```
MATCH → BUILD_TREE → FLUSH
```

| 状态 | 分块策略 | 职责 |
|------|---------|------|
| `MATCH` | **三分块**（`window_` 滑动窗口） | 贪心匹配（HashChain），收集 token 到 `token_buffer_` |
| `BUILD_TREE` | **非分块阶段** | 统计 token 频率，构建 Huffman 树（FLATE） |
| `EMIT` | **二分块**（无 I/O） | Huffman 编码输出 token，写 EOB |

### 解耦的流式框架

Deflate 的流式框架通过 `window_` 滑动窗口 + `fillWindow()` / `slideWindow()` 实现三分块语义。框架分为三层：窗口管理层、匹配引擎层、熵编码层。

```
┌─────────────────────────────────────────────────────────────┐
│                     Deflate 流式框架                         │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  滑动窗口管理层（三分块）                              │   │
│  │                                                     │   │
│  │  window_ = vector<uint8_t>(2 × SLIDE_SIZE)          │   │
│  │  ┌──────────────┬──────────────────────────────┐    │   │
│  │  │ search window │  current + lookahead         │    │   │
│  │  │  (prev 角色)  │  (current + new 角色)         │    │   │
│  │  │  0..SLIDE_SIZE│  SLIDE_SIZE..2×SLIDE_SIZE    │    │   │
│  │  └──────────────┴──────────────────────────────┘    │   │
│  │                    ↑ cursor_                        │   │
│  │                                                     │   │
│  │  fillWindow(): 尾部追加新数据，扩充 lookahead         │   │
│  │  slideWindow(): memcpy 后半→前半，cursor_ -= SLIDE   │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  哈希链管理层                                         │   │
│  │  - head_[HASH_SIZE]: 桶头数组                        │   │
│  │  - prev_[SLIDE_SIZE]: 前向链数组                     │   │
│  │  - slideWindow() 中同步调整指针偏移                   │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  状态机调度层                                         │   │
│  │  MATCH → BUILD_TREE → FLUSH                           │   │
│  │  - MATCH: 贪心匹配，填 token_buffer_           │   │
│  │  - BUILD_TREE:  统计频率，构建 Huffman 树             │   │
│  │  - EMIT: Huffman 编码输出                    │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  熵编码层（与匹配引擎解耦）                            │   │
│  │  - Flag 模式: Huffman 字母表内置 flag 语义            │   │
│  │  - Non-flag 模式: bit-packed offset/length           │   │
│  │  - 树策略: FLATE（直接）/ 3HfMTree（DPFlate 内核）    │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键设计点**：
- MATCH 使用三分块（`window_` 滑动窗口）：search window + current + lookahead 三段合一
- BUILD_TREE 和 EMIT 使用二分块：逐 token 统计/编码，不涉及跨块搜索
- 贪心匹配（非 DP）：每次选最长匹配，不需要 temp 文件和两阶段管线
- `token_buffer_` 达到 `MAX_BLOCK_TOKENS`（16384）时自动触发 BUILD_TREE，控制单块大小
- Non-flag 模式下跳过 BUILD_TREE，直接从 MATCH 进入 EMIT

### 与 DP 算法的关键区别

| 维度 | Deflate | LZDP / DPFlate |
|------|---------|---------------|
| 匹配策略 | 贪心（每次选最长匹配） | DP（全局最优路径） |
| 三分块形态 | `window_` 定长滑动窗口 | `input_buffer_` 可变长缓冲区 |
| Temp 文件 | 不需要 | temp A + temp B |
| 管线 | 单遍（匹配→建树→发码） | 两阶段（Phase 1 → Phase 2） |
| 块触发 | token_buffer_ 满 16384 | 输入耗尽 |

---

## 2.4 LZSS

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| 三分块（§1.1） | 全文件内存（静态方法天然完整窗口） |
| 编码方案（§1.5） | Flag / Non-flag 可选 |
| StreamingCompressAdapter（§1.7） | 流式包装（每块独立压缩） |

> LZSS 使用贪心匹配（非 DP），无 Huffman 层，无状态机（静态方法一次调用完成）。

### 解耦的流式框架

LZSS 当前为**静态方法** `LZSS::compress`，全文件在内存中处理。流式场景通过 `StreamingCompressAdapter` 包装为两层架构：

```
┌─────────────────────────────────────────────────────────────┐
│                   LZSS 流式框架（两层）                       │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  流式包装层：StreamingCompressAdapter                 │   │
│  │                                                     │   │
│  │  输入: 原始字节流（通过 process() 喂入）              │   │
│  │                                                     │   │
│  │  ┌─────────────────────────────────────────────┐    │   │
│  │  │ input_buffer_ 累积                            │    │   │
│  │  │  ↓ 满 chunk_size_ 时切分                      │    │   │
│  │  │ chunk_data = take_front(chunk_size_)          │    │   │
│  │  │  ↓ 独立调用                                   │    │   │
│  │  │ compressed = compress_fn_(chunk_data)          │    │   │
│  │  │  ↓ 帧封装                                     │    │   │
│  │  │ [u32 BE len][compressed_data]                 │    │   │
│  │  └─────────────────────────────────────────────┘    │   │
│  │                                                     │   │
│  │  最后一块后追加终止符: [u32 BE 0]                     │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │ 调用                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  压缩核心层：LZSS::compress（静态方法）               │   │
│  │                                                     │   │
│  │  输入: vector<uint8_t>（完整分块数据）                │   │
│  │                                                     │   │
│  │  内部三分块语义（全文件在内存中）:                     │   │
│  │  ┌──────────────┬──────────────────────────────┐    │   │
│  │  │ search window │  current + lookahead         │    │   │
│  │  │ search_start  │  cursor .. input.size()      │    │   │
│  │  │ .. cursor     │                              │    │   │
│  │  └──────────────┴──────────────────────────────┘    │   │
│  │                                                     │   │
│  │  贪心匹配: 在 search window 中找最长匹配              │   │
│  │  编码输出: Flag 或 Non-flag                          │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**设计说明**：
- LZSS 静态方法内部天然拥有完整的三分块语义（全文件在内存中，search window 和 lookahead 都完整）
- `StreamingCompressAdapter` 将输入按 `chunk_size_` 切分，每块独立调用 `LZSS::compress`
- 块边界处 search window 重置——跨块匹配丢失，但每块内部匹配完整
- 不需要状态机——静态方法一次调用完成
- 输出格式：`[u32 BE chunk_len][chunk_data]...[u32 BE 0]`

### 未来演进：LZSS 状态机

若需要跨块匹配连续性（避免 `StreamingCompressAdapter` 的块边界匹配丢失），可将 LZSS 改造为状态机，使用 `window_` 滑动窗口（同 Deflate §2.3）：

```
FIND_MATCHES → EMIT_TOKENS

FIND_MATCHES: 三分块（window_），贪心匹配，收集 token
EMIT_TOKENS:  二分块，编码输出 token
```

当前实现选择 `StreamingCompressAdapter` 包装静态方法，是因为 LZSS 的简单性使得跨块匹配丢失的影响可接受，且避免了状态机的复杂度。

---

## 2.5 Inflate（Deflate 解压）

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| 滑动窗口解压（§1.9） | `window_` 32KB 环形缓冲区 + `output_buf_` 缓冲 |
| 解压状态机（§1.12） | 6 状态驱动 |

> Inflate 是标准 Deflate（RFC1951）的解压器。输入为连续的 Deflate 比特流（含块头 + Huffman 树 + 编码 token），输出为原始字节流。

### 状态机

```
READ_BLOCK_HEADER → READ_TREES → DECODE_TOKENS → COPY_MATCH → FLUSH_TO_WRITER
                       ↑              ↑               │              │
                       │              └─── 字面量 ─────┘              │
                       │              └─── EOB(256) ─────────────────┘
                       └── btype=1/2（静态/动态 Huffman）────────────┘
```

| 状态 | 职责 |
|------|------|
| `READ_BLOCK_HEADER` | 读取 3-bit 块头（`bfinal` + `btype`），判断块类型 |
| `READ_TREES` | 从比特流反序列化 literal/length 树 + distance 树 |
| `DECODE_TOKENS` | 逐符号解码：字面量(0-255)→写入输出；长度码(257-285)→进入 COPY_MATCH；EOB(256)→回到 READ_BLOCK_HEADER |
| `COPY_MATCH` | 从 `window_` 环形缓冲区复制 match 字节到 `output_buf_` |
| `FLUSH_TO_WRITER` | `output_buf_` 满 32KB 时刷新到 `writer_`，然后回到 DECODE_TOKENS |
| `STORED_COPY` | btype=0 时逐字节复制（无压缩块） |

### 解耦的流式框架

```
┌─────────────────────────────────────────────────────────────┐
│                     Inflate 流式框架                         │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  滑动窗口层（§1.9）                                    │   │
│  │                                                     │   │
│  │  window_ = vector<uint8_t>(32768)                   │   │
│  │  out_abs_: 全局已解码字节计数（单调递增）              │   │
│  │                                                     │   │
│  │  appendDecodedByte(b):                               │   │
│  │    output_buf_.push_back(b)                         │   │
│  │    window_[out_abs_ % 32768] = b                    │   │
│  │    ++out_abs_                                       │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  状态机调度层                                         │   │
│  │                                                     │   │
│  │  READ_BLOCK_HEADER → READ_TREES → DECODE_TOKENS     │   │
│  │       ↑                  ↑             │            │   │
│  │       │                  └── btype=1/2─┘            │   │
│  │       │                                │            │   │
│  │       │              ┌─ lit(0-255) ────┘            │   │
│  │       │              ├─ len(257-285) → COPY_MATCH   │   │
│  │       │              └─ EOB(256) ─────┘             │   │
│  │       └── bfinal=0 ──────────────────┘              │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  输出缓冲层                                           │   │
│  │  output_buf_ 满 32KB → FLUSH_TO_WRITER → 清空        │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键设计点**：
- 32KB 滑动窗口保证 match copy 的正确性（RFC1951 规定最大回退距离 32768）
- `out_abs_` 单调递增，永不归零——环形缓冲区通过取模寻址
- `output_buf_` 满 32KB 即 flush，保证流式输出
- Huffman 树按块重建（每个 Deflate block 有独立的树），块之间无状态共享
- 支持 btype=0（无压缩）、btype=1（固定 Huffman）、btype=2（动态 Huffman）三种块类型

---

## 2.6 Inflate3HM（3HfMTree 解压）

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| 滑动窗口解压（§1.9） | `window_` 32KB 环形缓冲区 + `output_buf_` 缓冲 |
| 解压状态机（§1.12） | 7 状态驱动 |

> Inflate3HM 是 3HfMTree 编码比特流的解压器。输入为 `HuffmanTree3HM::serialize()` 产出的格式（4 字节头 + 3 棵序列化 Huffman 树 + 编码 token），输出为原始字节流。

### 状态机

```
READ_HEADER → DECODE_OFFSET
                ├─ offset=0 → DECODE_RUN_LEN → DECODE_LITERALS × N → DECODE_OFFSET
                └─ offset>0 → DECODE_MATCH_LEN → COPY_MATCH → DECODE_OFFSET
                
FLUSH_TO_WRITER: output_buf_ 满时触发，刷新后回到 DECODE_OFFSET
```

| 状态 | 职责 |
|------|------|
| `READ_HEADER` | 反序列化 3HfMTree（字面量树 + offset 树 + length 树） |
| `DECODE_OFFSET` | 解码 offset：0→字面量游程；>0→match |
| `DECODE_RUN_LEN` | 解码字面量游程长度 |
| `DECODE_LITERALS` | 逐字面量解码（通过字面量树），写入输出 |
| `DECODE_MATCH_LEN` | 解码 match 长度 |
| `COPY_MATCH` | 从 `window_` 环形缓冲区复制 match 字节 |
| `FLUSH_TO_WRITER` | `output_buf_` 满时刷新到 `writer_` |

### 解耦的流式框架

```
┌─────────────────────────────────────────────────────────────┐
│                   Inflate3HM 流式框架                        │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  滑动窗口层（§1.9，同 Inflate）                        │   │
│  │  window_ 32KB + out_abs_ + output_buf_               │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  状态机调度层                                         │   │
│  │                                                     │   │
│  │  READ_HEADER（一次性，反序列化 3 棵树）               │   │
│  │       ↓                                             │   │
│  │  DECODE_OFFSET ←──────────────────────────────┐     │   │
│  │    ├─ offset=0 → DECODE_RUN_LEN                │     │   │
│  │    │                ↓                          │     │   │
│  │    │           DECODE_LITERALS × N ────────────┘     │   │
│  │    │                                                │   │
│  │    └─ offset>0 → DECODE_MATCH_LEN                   │   │
│  │                      ↓                              │   │
│  │                   COPY_MATCH ───────────────────────┘     │   │
│  │                                                           │
│  │  FLUSH_TO_WRITER: 任意状态 output_buf_ 满时插入刷新        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键设计点**：
- 与 Inflate 共享滑动窗口层（§1.9），match copy 逻辑完全相同
- 3HfMTree 的树结构在 READ_HEADER 一次性反序列化，后续解码不再变化（不像 Inflate 按块重建树）
- Non-flag 编码语义：offset=0 表示字面量游程，offset>0 表示 match
- 字面量通过独立的字面量 Huffman 树逐字节解码（非固定 8-bit）

---

## 2.7 LZDPDecompress_OutOfCore

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| 全量累积解压（§1.10） | `output_buffer_` 从不收缩 |
| 解压状态机（§1.12） | 2 阶段（读头 + 解码循环） |

> LZDPDecompress_OutOfCore 是 LZDP 压缩比特流的解压器。输入为 2 字节原始头 + bit-packed token 流，输出为原始字节流。

### 状态机

```
READ_HEADER → DECODE_LOOP → DONE
```

| 阶段 | 职责 |
|------|------|
| `READ_HEADER` | 读取 2 字节原始头（`hdr0` 含 offset_bits + flag 标记，`hdr1` 含 length_bits） |
| `DECODE_LOOP` | 循环解码 token：flag 模式读 1-bit flag 判断字面量/match；non-flag 模式读 offset 判断（0=字面量游程，>0=match） |

### 解耦的流式框架

```
┌─────────────────────────────────────────────────────────────┐
│                LZDPDecompress_OutOfCore 流式框架             │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  全量累积层（§1.10）                                  │   │
│  │                                                     │   │
│  │  output_buffer_: 从不收缩，累积全部已解码字节          │   │
│  │  output_flush_idx_: 已输出位置（增量 flush）           │   │
│  │                                                     │   │
│  │  match copy:                                        │   │
│  │    copy_start = output_buffer_.size() - offset       │   │
│  │    output_buffer_.push_back(output_buffer_[copy_start + k])│
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  解码层                                              │   │
│  │                                                     │   │
│  │  READ_HEADER: 跨 chunk 累积 2 字节原始头              │   │
│  │       ↓                                             │   │
│  │  DECODE_LOOP:                                       │   │
│  │    Flag 模式:  读 1b flag → lit(9b) / match(1+Ob+Lb)│   │
│  │    Non-flag:   读 Ob+Lb → offset=0 字面量 / >0 match │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键设计点**：
- `output_buffer_` 从不收缩——LZDP 的 match offset 是相对于完整已解码流开头的绝对偏移
- 这是 LZDP 解压的全文件进内存瓶颈：`output_buffer_` 会增长到与原始文件等大
- 头解析支持跨 chunk 累积（`lzdp_hdr_acc_` 缓冲区），适应流式输入
- Flag / Non-flag 两种编码模式由头字节的 bit7 自动识别
- 安全熔断：`lzdp_dec_iter_ > 10000000` 时强制终止，防止死循环

---

## 2.8 LZSSDecompress

### 砖块使用表

| 砖块 | 使用方式 |
|------|---------|
| StreamingDecompressAdapter（§1.11） | 流式包装（每帧独立解压） |

> LZSS 解压为**静态方法** `LZSS::decompress`，全帧数据在内存中处理。流式场景通过 `StreamingDecompressAdapter` 包装。

### 解耦的流式框架

LZSS 解压采用与压缩对称的两层架构：

```
┌─────────────────────────────────────────────────────────────┐
│                 LZSSDecompress 流式框架（两层）               │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  流式包装层：StreamingDecompressAdapter（§1.11）      │   │
│  │                                                     │   │
│  │  输入: [u32 BE len][chunk]...[u32 BE 0]              │   │
│  │                                                     │   │
│  │  ┌─────────────────────────────────────────────┐    │   │
│  │  │ input_buffer_ 累积                            │    │   │
│  │  │  ↓ 解析 u32 BE 帧长                          │    │   │
│  │  │ 帧长=0 → 终止符，finished_=true               │    │   │
│  │  │ 帧长>0 → 取出完整帧                           │    │   │
│  │  │  ↓ 独立调用                                   │    │   │
│  │  │ decompressed = decompress_fn_(chunk_data)      │    │   │
│  │  │  ↓ 拼接                                       │    │   │
│  │  │ output_buffer_ += decompressed                 │    │   │
│  │  └─────────────────────────────────────────────┘    │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │ 调用                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  解压核心层：LZSS::decompress（静态方法）              │   │
│  │                                                     │   │
│  │  输入: vector<uint8_t>（完整帧数据）                  │   │
│  │                                                     │   │
│  │  1. 读取 4 字节 original_size                        │   │
│  │  2. 循环解码:                                        │   │
│  │     Flag 模式:  8 flag bits/byte，逐 bit 判断        │   │
│  │     Non-flag:   2 字节 token，position=0 为字面量     │   │
│  │  3. match copy: result.push_back(result[start + k]) │   │
│  │  4. 返回完整解压结果                                  │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键设计点**：
- 每帧独立解压——帧之间无状态共享，与 `StreamingCompressAdapter` 的独立压缩对称
- 帧格式：`[u32 BE original_size][编码数据]`（LZSS 静态方法的内部格式）
- 解压结果按帧顺序拼接为完整原始数据
- 不需要滑动窗口——每帧数据完整在内存中，match copy 直接引用 `result` 向量
- 与压缩侧一样，未来可演进为状态机实现跨帧匹配连续性

---

# 三、砖块-算法对照总表

## 压缩侧

| 砖块 | LZDP | DPFlate | Deflate | LZSS |
|------|:----:|:-------:|:-------:|:----:|
| 三分块（§1.1） | ✓ `input_buffer_` | ✓ `input_buffer_` | ✓ `window_` | ✓ 全文件内存 |
| 二分块（§1.2） | ✓ 回溯+发码 | ✓ 建树+回溯+发码 | ✓ 建树 | — |
| 推进式 DP（§1.3） | ✓ | ✓ | — | — |
| 状态机（§1.4） | ✓ 3 状态 | ✓ 4 状态 | ✓ 3 状态 | — |
| 编码方案（§1.5） | ✓ Flag/Non-flag | ✓ Flag/Non-flag | ✓ Flag/Non-flag | ✓ Flag/Non-flag |
| Huffman 树（§1.6） | — | ✓ FLATE/3HfM | ✓ FLATE/3HfM | — |
| StreamingCompressAdapter（§1.7） | — | — | — | ✓ |
| 内存管理（§1.8） | ✓ | ✓ | ✓ | — |

## 解压侧

| 砖块 | Inflate | Inflate3HM | LZDPDecompress | LZSSDecompress |
|------|:-------:|:----------:|:--------------:|:--------------:|
| 滑动窗口解压（§1.9） | ✓ 32KB | ✓ 32KB | — | — |
| 全量累积解压（§1.10） | — | — | ✓ `output_buffer_` | — |
| StreamingDecompressAdapter（§1.11） | — | — | — | ✓ |
| 解压状态机（§1.12） | ✓ 6 状态 | ✓ 7 状态 | ✓ 2 阶段 | — |

---

# 四、关键设计要点

## 压缩侧

| 方面 | 错误做法 | 正确做法 |
|------|---------|---------|
| DP 算法 | dp[i] 依赖 dp[i-1], dp[i-2], ... | 推进式 DP：dp[i] 只从 dp[i-1] 获得 base，向前推送 |
| 匹配阶段分块 | 简单分块无窗口携带，块边界匹配截断 | 三分块：prev(搜索) + current(处理) + new(前瞻) |
| 建树/回溯分块 | 整文件加载到内存 | 二分块：逐块读入、处理、释放 |
| 分块实现（input_buffer_） | 移动/复制数据 | 可变长缓冲区 + `window_abs_pos_` 位置追踪 |
| 分块实现（window_） | 每块独立压缩无历史 | 2×SLIDE_SIZE 滑动窗口 + `lookahead_` 变量 |
| 三分块边界处理 | 块边界哈希链断裂，匹配断档 | `reseedHashChainPrefix` 重建 search window 哈希链（input_buffer_）/ `slideWindow` 同步偏移指针（window_） |
| 二分块边界处理 | 假设块边界需要特殊处理 | 频率累加交换律 + 回溯纯顺序读取 → 块边界无影响 |
| 中断取消（三分块） | 在 DP 推进中途强行中断，状态不一致 | 在 `handleCollectInput` 入口和 DP 循环内每 2048 次迭代检查 cancel flag，协作退出 |
| 中断取消（二分块） | 在 BitWriter 未封口时中断 | 在 chunk 读取前检查 cancel flag，协作退出 |
| 中断现场保存 | 尝试保存 L3 编解码状态（dp_states_、哈希链、temp 游标） | 仅保存 L2 管线政务现场（bytes_read、current_state、freq_map），整次重跑 |
| 架构 | 算法与框架紧耦合 | 砖块解耦 + 算法组合砖块 |
| 流式包装（非状态机） | 每个算法自己实现流式 | `StreamingCompressAdapter` 统一包装 |

## 解压侧

| 方面 | 错误做法 | 正确做法 |
|------|---------|---------|
| match copy（滑动窗口） | 保留全部历史字节 | 32KB 环形缓冲区 + `out_abs_` 取模寻址 |
| match copy（全量累积） | 强行用滑动窗口限制回退距离 | 保留全量 `output_buffer_`（LZDP offset 语义要求） |
| 输出缓冲 | 等全部解码完一次性输出 | `output_buf_` 满阈值即 flush（Inflate: 32KB） |
| 流式包装（非状态机） | 每个解压器自己实现流式 | `StreamingDecompressAdapter` 统一包装 |
| 头解析（流式输入） | 假设头字节一次性到达 | 跨 chunk 累积头字节（`lzdp_hdr_acc_`） |
| 安全防护 | 无限循环 | 迭代计数器熔断（`> 10000000` 强制终止） |