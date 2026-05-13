# 流式压缩设计文档

## 一、核心概念

### 1.1 推进式 DP（Push-style DP）

dp[i] **不依赖** 序号小于 i 的 dp 值。dp[i-1] 的决策会"向前推送"到 dp[i+L-1]：

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

### 1.2 Triple（算法原生决策）

Triple 是算法的原生输出，**与编码方案无关**：

```
Triple(offset, length, literal)

offset=0, length=0:  字面量，字节值 = literal
offset>0, length>0:  匹配，距离 = offset，长度 = length
```

### 1.3 两种编码方案

编码方案是将 token 序列转为比特流的策略，**不影响 temp file 格式**。

#### 方案 A：Flag 编码

```
字面量: [flag=1 : 1 bit][byte : 8 bits]          = 9 bits
匹配:   [flag=0 : 1 bit][offset : Ob][length : Lb] = 1+Ob+Lb bits
```

#### 方案 B：Non-flag 编码（offset=0 哨兵）

```
字面量游程: [offset=0 : Ob][run_len : Lb][byte_0 : 8]...[byte_N-1 : 8]
匹配:       [offset>0 : Ob][length : Lb]
```

### 1.4 两种树策略

编码方案之上，还有两种树策略决定如何构建 Huffman 树：

| 树策略 | 树结构 | 总符号数 | 适用编码 |
|--------|--------|---------|---------|
| FLATE | 字面量/长度树(286) + 距离树(30) | 316（固定） | Flag / Non-flag |
| 3HfMTree | 字面量树(256) + offset 树(2^{offset_huffman_bwidth}) + length 树(2^{length_huffman_bwidth}) | 256 + 2^{b_o} + 2^{b_l}（`b_o`/`b_l` 为两配置槽宽，可不同） | Flag / Non-flag |

详见[算法洞察](#八算法洞察)章节。

---

## 二、三分块环形队列

### 2.1 设计思想

3 个分块各司其职，构成一个滑动窗口：

| 分块 | 角色 | 用途 |
|------|------|------|
| prev_chunk | 历史窗口 | 为 current_chunk 开头提供 search window（已处理字节） |
| current_chunk | 正在处理 | DP 指针 i 从位置 0 推进到末尾 |
| new_chunk | 前瞻窗口 | 为 current_chunk 提供 lookahead 字节 |

**为什么需要 3 个分块？**

当 i 在 current_chunk 的**开头**（位置 0）时，search window 需要来自**前一个分块**末尾的已处理字节。二分块没有保留 prev_chunk，会导致 search window 断档，current_chunk 开头无法找到匹配。

### 2.2 数据结构

```
ring[0], ring[1], ring[2]   // 3 个定长槽位
cur_idx: 指向 current_chunk（i 指针正在处理）

prev_idx = (cur_idx - 1 + 3) % 3   // 指向 prev_chunk（提供 search window）
new_idx  = (cur_idx + 1) % 3       // 指向 new_chunk（提供 lookahead）
```

### 2.3 流式配置参数

```
struct StreamParams:
    chunk_size:        size_t    // 环形队列分块大小（字节）
    search_size:       size_t    // 搜索窗口大小（字节）
    lookahead_size:    size_t    // 前瞻窗口大小（字节）
    range:             size_t    // 每位置最多考虑的匹配数（top-K）
    use_flag_encoding: bool      // 是否使用 Flag 编码
    min_match:         size_t    // 最小匹配长度（0 表示自动计算）
```

**实现口径（统一分块）**：产品里磁盘读入、`MemoryPool` 槽、``StreamingCompressAdapter`` 的明文段长、以及单次喂给 ``LZDP_OutOfCore`` / ``DPFlate`` 的 ``process()`` 读入，共用 ``compressor::processor::effective_stream_chunk_bytes``（``StreamChunkPolicy.hpp``：默认 1 MiB，clamp 64 KiB–128 MiB）。LZDP、LZSS、DPFlate、Deflate 的文件流式不得各自使用另一套 I/O 分块大小；与 ``LzdpWholeFileParams`` 中的 ``search_size`` / ``lookahead_size``（算法窗口）是不同概念。

### 2.4 StreamReader 类

`read_chunk()` 需要维护文件句柄、当前偏移、分块大小等流式状态，封装为类方法：

```
class StreamReader:
    // 打开文件，记录流式配置（从中提取 chunk_size）
    StreamReader(path: string, params: StreamParams)

    // 读取下一个分块，返回字节数组
    // 文件末尾返回空数组（或不足 chunk_size 的剩余字节）
    read_chunk() -> byte[]

    // 当前文件偏移（已读取字节数）
    tell() -> size_t

    // 文件总大小
    size() -> size_t

    // 是否已到文件末尾
    eof() -> bool
```

### 2.5 主循环（环形队列滚动机制）

```
reader = StreamReader(input_path, params)

初始化:
  ring[0] = nullptr           // prev（第一个分块没有历史）
  ring[1] = reader.read_chunk()      // current
  ring[2] = reader.read_chunk()      // new（lookahead）
  cur_idx = 1

while ring[cur_idx] is not empty:
    current = ring[cur_idx]
    prev    = ring[(cur_idx - 1 + 3) % 3]  // 可能为 nullptr（开头）
    new     = ring[(cur_idx + 1) % 3]       // 可能为 nullptr（结尾）

    // ★ 构造连续缓冲区（具体实现见 Phase 1 forward_dp）
    buffer = (prev ?? []) + current + (new ?? [])[:lookahead]

    // ★ 调用 dp_core 算子处理 current 范围（具体实现见 Phase 1）
    current_triples = dp_core(buffer, ..., core_begin, core_end)

    // ★ 将 triples 写入 temp A（位于 workspace/tmp/）

    // ★ 刷入磁盘，确保 temp A 数据已落盘再释放 prev
    temp_A.flush()

    // ★ 滚动分块
    prev_idx = (cur_idx - 1 + 3) % 3
    free(ring[prev_idx])              // 释放 prev（不再需要）

    cur_idx = (cur_idx + 1) % 3       // old new → current
    ring[(cur_idx + 1) % 3] = reader.read_chunk()  // 复用 freed slot → new
    // 下一轮 while 检查 ring[cur_idx]（新的 current）是否非空
```

### 2.6 示例追踪

```
分块大小 = 6, lookahead = 2, search_size = 4

初始化:
  ring[0] = null               (prev,  cur_idx=1 → prev=(1-1)%3=0, 开头无历史)
  ring[1] = [0,1,2,3,4,5]     (current, cur_idx=1)
  ring[2] = [6,7,8,9,10,11]   (new, cur_idx=1 → new=(1+1)%3=2)

迭代 1: current=ring[1]=[0,1,2,3,4,5], prev=null, new=ring[2]=[6,7,8,9,10,11]

  i=0: current[0]=0
       search = []（prev 为 null，开头无历史）
       lookahead = current[1:3] + new[:0] = [1,2]
       → 开头无匹配字典，只能输出字面量

  i=1: current[1]=1
       search = current[:1] = [0]
       lookahead = current[2:4] + new[:0] = [2,3]
       → 在 [0] 中找 1 的匹配

  i=5: current[5]=5
       search = current[:5] = [0,1,2,3,4]
       lookahead = new[:2] = [6,7]
       → 可能找到匹配 (offset=5, length=2) 等

  ★ current 完全处理 → 滚动
  // prev=ring[0]=null 释放无影响
  // current=ring[1] 保留 → 成为下一轮的 prev
  // new=ring[2] 成为新的 current
  // 释放的 ring[0] 读入新分块 → 成为新的 new
  free(ring[0])  // null, 无操作
  cur_idx = 2                // old new → current
  ring[(2+1)%3] = ring[0] = reader.read_chunk()  // 读入 [12,13,14,15,16,17]

迭代 2: current=ring[2]=[6,7,8,9,10,11], prev=ring[1]=[0,1,2,3,4,5], new=ring[0]=[12,13,14,15,16,17]

  i=0: current[0]=6
       search = prev[-4:] = [2,3,4,5]
       lookahead = current[1:3] + new[:0] = [7,8]
       → 在 [2,3,4,5] 中找 6 的匹配

  i=5: current[5]=11
       search = prev[-4:] + current[:5] = [2,3,4,5,6,7,8,9,10]
       lookahead = new[:2] = [12,13]
       → 可能找到匹配 (offset=5, length=2) 等

  ★ current 完全处理 → 滚动
  // prev=ring[1] 不再需要 → 释放
  // current=ring[2] 保留 → 成为下一轮的 prev
  // new=ring[0] 成为新的 current
  // 释放的 ring[1] 读入新分块 → 成为新的 new
  free(ring[1])
  cur_idx = 0                // old new → current
  ring[(0+1)%3] = ring[1] = reader.read_chunk()  // 读入 [18,19,20,21,22,23]
```

### 2.7 关键设计要点

- **不移动、不复制**：只移动 cur_idx 指针，分块数据原地不动
- **释放时机**：prev 不再需要时立即释放（delete），确保内存不膨胀
- **槽位复用**：`ring[(cur_idx+1)%3]` 总是写入刚释放的 prev 槽位，3 个槽位循环使用
- **`read_chunk()`**：封装为 [StreamReader](#24-streamreader-类) 类方法，维护文件句柄和分块状态
- **search window**：`prev[-search_size:] + current[:i]`，保证 i=0 时也有历史
- **pending 表**：携带跨分块的 DP 推送状态，在下一分块开头被消费
- **未处理语义**：i 没走到的地方视为"未处理"，即使推进式 DP 更新了 pending 表

### 2.8 边界处理

流式分块不一定刚好凑齐文件大小，三种边界情况：

**开头（无 prev）**：
```
ring = [null, chunk_0, chunk_1]    // cur=chunk_0, new=chunk_1
prev = null → search_window = []    // 开头无历史字典
```
第一个分块没有 prev，search window 为空，只能输出字面量。

**中间（标准三块）**：
```
ring = [prev, cur, new]             // 三个分块齐全
```
标准滚动，prev 提供 search window，new 提供 lookahead。

**结尾（无 new，cur 可能不足 chunk_size）**：
```
ring = [prev, last_chunk, null]     // cur=last_chunk, new=null
new = null → lookahead = []         // 结尾无前瞻
```
`read_chunk()` 在文件末尾返回剩余字节（可能 `< chunk_size`），
`len(current)` 就是实际大小，`core_end` 自动适配。

### 2.9 VirtualBuffer（零拷贝"假"拼接）

DP Core 需要连续的逻辑地址空间来统一处理 search window + current + lookahead，但物理上它们是三个独立内存块。`VirtualBuffer` 通过下标运算实现"假"拼接：

```
逻辑视图:  [--- search window ---][--- current ---][--- lookahead ---]
            seg0                  seg1              seg2
            ↑                     ↑                 ↑
            |                     |                 |
物理内存:   prev 尾部             当前分块           new 头部
            (独立分配)             (独立分配)         (独立分配)
```

**接口**：

```
class VirtualBuffer:
    // 构造：接收"装容器的容器"，任意数量内存段
    VirtualBuffer(segments: vector<span<byte>>)

    size() -> size_t              // 全部分段总长
    operator[](i: size_t) -> byte // 下标运算：i → 对应分段 + 偏移
    begin() -> iterator           // 随机访问迭代器（支持 KMP）
    end() -> iterator
```

**下标映射**（`operator[]` 核心逻辑）：

```
function resolve(i):
    for each seg in segments:
        if i < seg.size: return seg[i]
        i -= seg.size
    throw out_of_range
```

**迭代器**：自定义随机访问迭代器，内部持有 VirtualBuffer 引用 + 当前下标，`operator*` 调用 `vb[idx]`。KMP 模板以迭代器为参数，无需连续内存。

---

## 三、两阶段管线

### 3.1 Phase 1：前向 DP → Temp File A

```
输入:  原始文件
输出:  workspace/tmp/temp_A.dat（最优路径 token 序列，正向顺序，紧凑位级编码）
内存:  三分块环形队列
算法:  调用 DP Core（纯函数，不感知流式）
```

#### 设计思想

Phase 1 不再自己实现 push-style DP 逻辑，而是调用 **DP Core**——一个纯函数，输入字节流，输出 `ROLListNode`（含 `triples`、`literal_count`、`match_count`）。

流式框架只做三件事：
1. **管理三分块**：读入、滚动、释放
2. **构造输入**：将 `search_window + current_chunk + lookahead_window` 拼成连续缓冲区
3. **调用 DP Core**：获取 `ROLListNode` 容器，提取 current_chunk 范围的 triples 写入 temp A，同时**累加全局 literal_count / match_count**（跨分块汇总，用于 Phase 2 预计算输出大小）

DP Core 负责所有算法逻辑：
- Push-style DP 推进
- Match finding（KMP / HashChain）
- 代价计算
- 最优路径回溯

#### 接口定义

```
// === DP Core 算子接口 ===
// 输入:  连续字节流 (search_window + current_chunk + lookahead_window)
//         + search_size, lookahead_size, range 等参数
//         + core_begin, core_end 限定 i 指针活动范围
// 输出:  整个输入的最优 Triple 路径
// 语义:  不感知流式分块，纯粹在 [core_begin, core_end) 上做 DP 推进

function dp_core(
    buffer: byte[],              // search_window + current_chunk + lookahead_window
    search_size: size_t,
    lookahead_size: size_t,
    range: size_t = 3,
    core_begin: size_t = 0,      // i 指针起始位置（默认 0）
    core_end: size_t = SIZE_MAX  // i 指针结束位置（默认末尾）
) -> Triple[]:
    // 由具体算法实现（LZDP::dp_core 等）
    // 内部实现 push-style DP，在 [core_begin, core_end) 上推进
    // 回溯从 core_end 开始，返回最优路径
```

#### 辅助函数

```
// flagwrite：将 Triple 按 flag-at-end 格式写入 temp A
// 不能直接写入 ROLListNode 的原始编码，必须通过此函数调整 flag 位置
//
// ROLListNode 存储的是算法原生 Triple(offset, length, literal)，
// 没有 flag 位。flagwrite 负责：
//   1. 识别 token 类型（offset==0 → 字面量，否则匹配）
//   2. 先写数据字段
//   3. 再写 1 bit flag 在末尾
//
// 这样 Phase 2 逆向读取时，先读末尾 flag 确定记录宽度，再读数据。
function flagwrite(writer, triple, Ob, Lb):
    if triple.offset == 0:
        // 字面量: [byte:8][flag=0:1] = 9 bits
        writer.write_bits(triple.literal, 8)
        writer.write_bits(0, 1)
    else:
        // 匹配: [offset:Ob][length:Lb][flag=1:1] = 1+Ob+Lb bits
        writer.write_bits(triple.offset, Ob)
        writer.write_bits(triple.length, Lb)
        writer.write_bits(1, 1)
```

#### 伪代码

```
function forward_dp(input_path, params, Ob, Lb) -> (temp_A_path, total_literals, total_matches):
    // Ob, Lb 由编码模块根据 search_size/lookahead_size 计算后传入
    // 返回 temp_A_path 和全局 token 计数
    // total_literals / total_matches 供 Phase 2 预计算输出文件大小

    // 临时文件位于 workspace/tmp/（由 WEBCOMPRESS_WORKSPACE 环境变量指定）
    workspace = getenv("WEBCOMPRESS_WORKSPACE") ?? os_temp_dir()
    temp_A_path = workspace + "/tmp/temp_A.dat"

    // 初始化三分块（边界处理）
    reader = StreamReader(input_path, params)
    ring = [null, null, null]
    ring[1] = reader.read_chunk()   // current: 第一个分块
    ring[2] = reader.read_chunk()   // new: 第二个分块（前瞻）
    cur_idx = 1                        // ring[0] = null（开头无历史）

    // 初始化 temp A（位级写入，正向顺序）
    temp_A = BitWriter(temp_A_path)
    total_literals = 0                 // 全局字面量计数
    total_matches = 0                  // 全局匹配计数

    while ring[cur_idx] is not empty:   // current 非空才处理
        current = ring[cur_idx]
        prev    = ring[(cur_idx - 1 + 3) % 3]  // 可能为 null（开头）
        new     = ring[(cur_idx + 1) % 3]       // 可能为 null（结尾）
        // 结尾时 current 可能不足 chunk_size，len(current) 自动适配

        // ★ 构造 VirtualBuffer（"假"拼接，零拷贝）
        //    prev/current/new 是三个独立内存块，VirtualBuffer 通过下标运算
        //    将它们映射为连续逻辑地址，避免物理拼接。
        //    prev 可能为 null（开头无历史），此时 search_window 为空
        segments = []
        if prev is not null:
            segments.append(prev[-params.search_size:])  // search window
        segments.append(current)                          // current chunk
        if new is not null:
            segments.append(new[:params.lookahead])       // lookahead
        vb = VirtualBuffer(segments)
        search_win_len = (prev is not null) ? min(len(prev), params.search_size) : 0

        // ★ 调用 dp_core 算子（传入 core_begin/core_end 限定 current 范围）
        dp_result = dp_core(
            vb,
            params.search_size,
            params.lookahead_size,
            params.range,
            search_win_len,                    // core_begin: 跳过 search window
            search_win_len + len(current)      // core_end:   限定 current 范围
        )
        // dp_core 内部：
        //   - i 指针在 [core_begin, core_end) 上推进
        //   - search 可回溯到 buffer 开头（search_window）
        //   - lookahead 可延伸到 buffer 末尾（new_chunk）
        //   - 回溯从 core_end 开始，只返回 current 范围内的 Triple
        //   - dp_result.literal_count / match_count 是 current 范围内的计数

        // ★ 累加全局计数（跨分块汇总，用于 Phase 2 预计算输出大小）
        // ★ 将 dp_result.triples 写入 temp A（正向顺序，flag 在末尾）
        //
        // 为什么 flag 在末尾？
        //   Flag 编码有两种位宽：字面量 9 bits，匹配 1+Ob+Lb bits。
        //   Phase 2 从 temp A 尾部逆向读取，flag 在末尾意味着：
        //     先读 1 bit flag → 知道记录宽度 → 再读数据字段
        //   如果 flag 在开头，逆向读取时需要先猜宽度，读错了还要回退。
        //
        //   temp A 的 flag-at-end 是内部格式，仅用于 Phase 1→Phase 2 传递。
        //   输出文件使用标准 flag-at-front 编码（见 Phase 2 回溯）。
        //
        //   必须使用 flagwrite 写入，不能直接写 ROLListNode 的原始编码。
        for triple in dp_result.triples:
            flagwrite(temp_A, triple, Ob, Lb)
            if triple.offset == 0:
                total_literals += 1
            else:
                total_matches += 1

        // ★ 刷入磁盘，确保 temp_A 数据已落盘
        //    后续 free(ring[prev_idx]) 释放 prev 分块内存，
        //    但 prev 的 search window 数据已被 dp_core 消费完毕，
        //    temp_A 中的 token 序列是持久化结果，不依赖 prev 内存。
        temp_A.flush()

        // ★ current 完全处理 → 滚动分块
        prev_idx = (cur_idx - 1 + 3) % 3
        free(ring[prev_idx])           // 释放 prev（不再需要）

        cur_idx = (cur_idx + 1) % 3    // old new → 新的 current
        ring[(cur_idx + 1) % 3] = reader.read_chunk()  // 读入新分块 → 新的 new
        // 下一轮 while 检查 ring[cur_idx]（新的 current）是否非空

    temp_A.flush()
    temp_A.close()
    return (temp_A_path, total_literals, total_matches)
```

#### 为什么这样设计？

| 方面 | 旧设计（手动 DP） | 新设计（dp_core 算子） |
|------|------------------|-----------------------|
| 算法逻辑 | 流式框架自己实现 push-style DP | dp_core 算子，框架不感知 |
| Match finding | 框架调用 match engine | dp_core 内部处理 |
| Pending 表 | 框架手动管理跨分块推送 | dp_core 内部 DP 数组隐式处理 |
| 范围限定 | 框架手动提取 current_triples | dp_core 通过 core_begin/core_end 自动限定 |
| 代价计算 | 框架计算 lit_cost/match_cost | dp_core 内部处理 |
| 代码复用 | 每个算法重新实现 | 复用 `dp_core` 等现有函数 |
| 可测试性 | 需要流式环境才能测试 | dp_core 可独立单元测试 |
| 算法切换 | 需要重写流式主循环 | 只需替换 dp_core 实现 |

### 3.2 Phase 2：回溯 → Temp File B → 输出文件

```
输入:  workspace/tmp/temp_A.dat
中间:  workspace/tmp/temp_B.dat（最优路径 token，flag-at-end 格式）
输出:  压缩文件（WCX 封装）
```

#### 设计思想

Phase 2 从 **temp A** 尾部逆向读取 token（得到正向顺序），按编码方案（flag / non-flag）写入 **temp B**，最后封装为 WCX 输出文件。

**为什么需要 temp A → temp B 两步？**

Phase 1 的 forward_dp 按分块顺序将 token 写入 temp A，但 token 的最终输出顺序是正向的。为了从文件末尾开始逆向写入输出文件（配合 `ReverseBitWriter` 预分配），需要先逆向读取 temp A 得到正向 token 顺序，再正向写入 temp B。

**格式转换**：Temp A 使用 flag-at-end 格式（`[data...][flag]`），输出文件使用标准 flag-at-front 格式（`[flag][data...]`）。逆向读取 temp A 时先读末尾 flag 确定记录宽度，然后按输出格式将 flag 写在数据前面。

```
Temp B 记录:     [data...][flag]      ← flag 在末尾（或已与输出一致的中间格式）
                       │
                       ▼  正向读取 + flag 位重排到输出约定
输出文件记录:   [flag][data...]       ← flag 在开头，标准编码
```

**预计算输出大小**：Phase 1 返回的 `total_literals` / `total_matches` 用于预计算输出文件大小，配合 `ReverseBitWriter` 从文件末尾向前写，消除 `literal_run` buffer 和 `prepend` 操作。

```
// Flag 编码：每个 token 位宽固定
total_bits = total_literals * 9
           + total_matches * (1 + Ob + Lb)

// Non-flag 编码：需模拟游程合并，但同样可预计算
// header 打包交给打包器，backtrack 只输出纯编码数据

// 3HfMTree 编码：使用 Huffman 码长而非固定位宽
// 预计算需要 Phase 1 先统计频率，建树后计算精确码长
// total_bits = Σ literal_code_len + Σ offset_code_len + Σ length_code_len
```

#### 二分块逆向读取 temp A

Phase 2 从 temp A 尾部逆向读取，不能一次性加载整个文件。采用 **二分块** 设计，从文件末尾向前分块读取、逐块处理、及时释放：

```
分块大小: CHUNK_BITS（例如 512 Kbits = 64 KB）
```

**为什么不需要重叠区？**

用 VirtualBuffer 拼接 `cur_raw + next_raw`，读取 token 时越界访问自动路由到下一段。cursor（指针）从末尾向前走，记录当前位置，无需预计算 overlap。

**边界处理**：

```
开始回溯时:
  cur_chunk  = 文件末尾完整 chunk（CHUNK_BITS）
  next_chunk = 前一个 chunk（CHUNK_BITS）
  // 用 VirtualBuffer 拼接，cursor 从末尾开始

中间:
  处理 cur_chunk → 释放 → cur_chunk = next_chunk → 读入新 next_chunk
  // 每个 chunk 都是完整大小

结束时:
  cur_chunk  = 剩余数据（可能 < CHUNK_BITS）
  next_chunk = null
  // 剩余数据不足一个完整 chunk，直接读取全部剩余
  // cursor 从实际大小开始，完美覆盖不会越界
```

#### 辅助函数

```
// 从指定 bit 位置逆向读取一条记录（flag 在末尾）
// 调用时 cur 指向记录末尾之后，返回时 cur 已回退到记录开头
function read_token_at(bit_reader, cur, Ob, Lb) -> (token, record_bits):
    // 先读末尾 1 bit flag
    flag = bit_reader.read_bits_at(cur - 1, 1)

    if flag == 0:
        // 字面量: [byte:8][flag=0:1] = 9 bits
        literal = bit_reader.read_bits_at(cur - 9, 8)
        record_bits = 9
        return (Triple(0, 0, literal), record_bits)
    else:
        // 匹配: [offset:Ob][length:Lb][flag=1:1] = 1+Ob+Lb bits
        record_bits = 1 + Ob + Lb
        offset = bit_reader.read_bits_at(cur - record_bits, Ob)
        length = bit_reader.read_bits_at(cur - record_bits + Ob, Lb)
        return (Triple(offset, length, 0), record_bits)
```

#### 伪代码

```
function backtrack(temp_A_path, output_path, params, Ob, Lb, total_literals, total_matches,
                    tree_strategy, huffman_trees):
    temp_A = FileBitReader(temp_A_path)   // 可 seek，不加载全部到内存

    // 0. 预计算输出大小，预分配文件
    if tree_strategy == FLATE:
        if params.use_flag_encoding:
            total_bits = total_literals * 9 + total_matches * (1 + Ob + Lb)
        else:
            total_bits = estimate_nonflag_size(temp_A, Ob, Lb)
    else:  // 3HfMTree
        total_bits = huffman_trees.get_total_encoded_bits()

    output = ReverseBitWriter(output_path, total_bits)  // 从末尾向前写

    // 1. 二分块逆向读取 temp A（FileBitReader + VirtualBuffer 跨分块越界访问）
    CHUNK_BITS   = 512 * 1024 * 8     // 64 KB 分块
    file_end     = temp_A.size_in_bits()

    // 初始化：从文件末尾读第一个 chunk
    chunk_end  = file_end
    chunk_size = min(CHUNK_BITS, chunk_end)
    cur_raw    = temp_A.read_chunk(chunk_end - chunk_size, chunk_size)
    chunk_end -= CHUNK_BITS

    // 预读第二个 chunk
    if chunk_end > 0:
        chunk_size = min(CHUNK_BITS, chunk_end)
        next_raw   = temp_A.read_chunk(chunk_end - chunk_size, chunk_size)
        chunk_end -= CHUNK_BITS
    else:
        next_raw = null

    // 用 VirtualBuffer 拼接 cur + next，cursor 从末尾开始
    // 越界访问自动路由到 next，无需 overlap 预读
    vb  = VirtualBuffer([cur_raw, next_raw].filter_not_null)
    pos = vb.size_in_bits()            // cursor：从末尾向前走

    while cur_raw is not empty:
        while pos > 0:
            // 逆向读取 token，VirtualBuffer 自动处理跨分块边界
            (token, record_bits) = read_token_at(vb, pos, Ob, Lb)
            pos -= record_bits

            // 只处理属于 cur_raw 的 token（起始位置在 cur_raw 范围内）
            // 属于 next_raw 的 token 留到下一轮处理
            if pos < cur_raw.size_in_bits():
                if tree_strategy == FLATE:
                    encode_token_flate(token, output, params, Ob, Lb)
                else:  // 3HfMTree
                    encode_token_3hfmtree(token, output, huffman_trees)

        // ★ 当前 chunk 处理完毕 → 刷入磁盘，再释放内存
        output.flush()
        free(cur_raw)
        cur_raw = next_raw

        if chunk_end > 0:
            chunk_size = min(CHUNK_BITS, chunk_end)
            next_raw   = temp_A.read_chunk(chunk_end - chunk_size, chunk_size)
            chunk_end -= CHUNK_BITS
        else:
            next_raw = null

        // 重新拼接 VirtualBuffer，cursor 重置到末尾
        vb  = VirtualBuffer([cur_raw, next_raw].filter_not_null)
        pos = vb.size_in_bits()

    // 刷出末尾字面量游程（non-flag 模式）
    if not params.use_flag_encoding and tree_strategy == FLATE:
        flush_literal_run_rev(output, literal_run, Ob, Lb)

    output.flush()

    // 2. WCX 封装
    wrap_wcx(output_path)

// === FLATE 编码（两树哈夫曼）===
function encode_token_flate(token, output, params, Ob, Lb):
    if params.use_flag_encoding:
        if token.offset == 0:
            output.write_bits_rev(1, 1)          // flag=1 字面量
            output.write_bits_rev(token.literal, 8)
        else:
            output.write_bits_rev(0, 1)          // flag=0 匹配
            output.write_bits_rev(token.offset, Ob)
            output.write_bits_rev(token.length, Lb)
    else:
        if token.offset == 0:
            literal_run.prepend(token.literal)
        else:
            flush_literal_run_rev(output, literal_run, Ob, Lb)
            literal_run.clear()
            output.write_bits_rev(token.offset, Ob)
            output.write_bits_rev(token.length, Lb)

// === 3HfMTree 编码（三树哈夫曼 + 多级查询）===
function encode_token_3hfmtree(token, output, huffman_trees):
    if token.offset == 0:
        // 字面量游程：先缓存，遇到 match 或末尾时刷出
        literal_run.append(token.literal)
    else:
        // 刷出缓存的字面量游程
        flush_literal_run_3hm(output, literal_run, huffman_trees)
        literal_run.clear()
        // 编码 match：offset + length
        huffman_trees.encode_offset(token.offset, output)
        huffman_trees.encode_length(token.length, output)

function flush_literal_run_3hm(output, run_bytes, huffman_trees):
    max_run = (1 << huffman_trees.length_bits) - 1
    offset = len(run_bytes)
    while offset > 0:
        chunk = min(offset, max_run)
        run_start = offset - chunk
        // 编码游程头：offset=0 + run_length
        huffman_trees.encode_offset(0, output)
        huffman_trees.encode_length(chunk, output)
        // 编码字面量字节
        for i in range(run_start, offset):
            huffman_trees.encode_literal(run_bytes[i], output)
        offset = run_start

function flush_literal_run_rev(output, run_bytes, Ob, Lb):
    max_run = (1 << Lb) - 1
    offset = len(run_bytes)
    while offset > 0:
        chunk_start = max(0, offset - max_run)
        chunk = offset - chunk_start
        output.write_bits_rev(0, Ob)           // offset=0 哨兵
        output.write_bits_rev(chunk, Lb)       // 游程长度
        for i in range(chunk - 1, -1, -1):     // 逆向写入字节
            output.write_bits_rev(run_bytes[chunk_start + i], 8)
        offset = chunk_start
```

---

## 四、Temp File 格式

### 4.1 Temp A（Phase 1 输出）

- 位置：`workspace/tmp/temp_A.dat`
- 格式：紧凑位级编码，flag-at-end
- 写入方式：正向顺序，分块写入
- 读取方式：逆向分块读取（Phase 2 回溯）

### 4.2 Temp B（Phase 2 中间产物）

- 位置：`workspace/tmp/temp_B.dat`
- 格式：标准 flag-at-front 编码（与输出文件一致）
- 写入方式：正向顺序（配合 ReverseBitWriter 从末尾向前写）
- 读取方式：正向读取（用于 WCX 封装）

---

## 五、解耦的流式框架架构（DP Core 模式）

### 5.1 核心思想

流式框架与 DP 算法通过 **DP Core 纯函数接口** 解耦：

```
┌─────────────────────────────────────────────────────────────┐
│                    Streaming Framework                       │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              三分块环形队列管理                        │  │
│  │      (分块 I/O / 构造 buffer / 滚动 / 释放)            │  │
│  └───────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              Temp File 管理                            │  │
│  │      (位级读写 / 随机访问 / temp_B)                    │  │
│  └───────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │           Phase 管线调度                               │  │
│  │   Phase 1(前向DP) → Phase 2(回溯)                     │  │
│  └───────────────────────────────────────────────────────┘  │
└──────────────────────┬──────────────────────────────────────┘
                       │ 调用 dp_core(buffer, params) → ROLListNode[]
                       │
              ┌────────┴────────┐
              │    DP Core      │  ← 纯函数接口，不感知流式
              │  (算法内核)      │
              └────────┬────────┘
                       │
          ┌────────────┼────────────┐
          │            │            │
     LZDP Core    DPFlate Core    Other...
          │            │
     dp_core()    (待实现)
```

### 5.2 DP Core 接口定义

```
// === 通用 DP Core 接口 ===
// 所有 DP 系列算法实现此接口

interface IDpCore {
    // Triple：算法原生决策（与编码无关）
    struct Triple {
        size_t offset;    // 0 = 字面量, >0 = 匹配距离
        size_t length;    // 0 = 字面量, >0 = 匹配长度
        uint8_t literal;  // 字面量字节值
    };

    // ROLListNode：DP 状态节点
    struct ROLListNode {
        Triple triple;        // 当前位置的最优决策
        size_t offset;        // 字节位置
        size_t literal_count; // 累计字面量数
        size_t match_count;   // 累计匹配数
    };

    // 核心函数：输入字节流，输出 DP 状态容器
    // buffer:      search_window + current_chunk + lookahead_window
    // search_size: 最大搜索距离
    // lookahead:   最大前瞻距离
    // range:       DP top-k 范围
    // core_begin:  i 指针起始位置（默认 0）
    // core_end:    i 指针结束位置（默认 buffer.size()）
    // 返回:        装 ROLListNode 的 DP 容器
    virtual std::vector<ROLListNode> process(
        const std::vector<uint8_t>& buffer,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3,
        size_t core_begin = 0,
        size_t core_end = SIZE_MAX
    ) = 0;

    // 查询参数（用于 temp file 位宽计算）
    virtual size_t get_offset_bits() const = 0;
    virtual size_t get_length_bits() const = 0;

    // 树策略查询（用于 Phase 2 编码）
    // FLATE: 返回字面量/长度树 + 距离树
    // 3HfMTree: 返回字面量树 + offset 树族 + length 树族
    virtual TreeStrategy get_tree_strategy() const = 0;
    virtual const HuffmanTree3HM* get_huffman_trees_3hm() const { return nullptr; }
};
```

### 5.3 LZDP 的 DP Core 实现

LZDP 已有现成的实现：`LZDP::dp_core`

```
// LZDP 的 DP Core 适配
// 已有函数：
//
// std::vector<ROLListNode> LZDP::dp_core(
//     const std::vector<uint8_t>& input,  // 连续字节流
//     size_t search_size,                  // 搜索窗口大小
//     size_t lookahead_size,               // 前瞻窗口大小
//     size_t range = 3,                    // DP top-k
//     size_t core_begin = 0,               // i 指针起始位置
//     size_t core_end = SIZE_MAX           // i 指针结束位置
// );
//
// ROLListNode 定义：
//   struct ROLListNode {
//       Triple triple;            // 当前位置的最优决策
//       size_t offset;            // 字节位置
//       size_t literal_count;     // 累计字面量数
//       size_t match_count;       // 累计匹配数
//   };
//
// 内部实现：
//   1. dp[core_begin] = Node(num=0, literal_count=0, match_count=0, ...)
//   2. for pos = core_begin..core_end-1:
//        - 字面量: dp[pos+1] = min(dp[pos+1], dp[pos].num + 1)
//                   literal_count = dp[pos].literal_count + 1
//                   match_count   = dp[pos].match_count
//        - 匹配:   dp[pos+L] = min(dp[pos+L], dp[pos].num + 1)
//                   literal_count = dp[pos].literal_count
//                   match_count   = dp[pos].match_count + 1
//   3. 从 dp[core_end] 回溯得到最优路径
//   4. 返回装 ROLListNode 的 DP 容器
//
// literal_count + match_count = tokennum（总 token 数）
// 这两个计数用于 Phase 2 预计算输出文件大小，实现逆向写入
```

### 5.4 DPFlate 的流式状态机实现

DPFlate 在**现有实现**里用**四阶段状态机**（`COLLECT_INPUT → BACKTRACK → BUILD_TREE → EMIT_TOKENS`）做流式压缩：Push-style DP、落盘与频率统计、建树、发码写在同一套状态里推进，**没有像 §5.1 那样**把「对一段 buffer 做 DP、返回 `ROLListNode` 容器」拆成可单独调用的 `dp_core` / `IDpCore::process` 边界再交给框架写 temp。**以下「流式分块与四状态」表与 5.4.2 / 5.4.4 伪代码**把 **`CHUNK_BITS`、`FileBitReader`、`VirtualBuffer`、`flush`/`free`/块滚动**写进设计，避免把 temp 读写误读成整文件进内存。

这里要区分两件事：

- **工程拆分**：上文说的是「当前代码有没有抽出纯函数边界」，不是「DPFlate 能不能用同一套 DP 数学」。推进式 DP、匹配枚举、代价比较与 LZDP 一样都适用；若把「当前分块上的 DP 推进」提炼成与环形 buffer 范围无关的核，**完全可以**与 §5.2 的 `dp_core` 形态对齐，只是 DPFlate 现状选择了单体状态机而已。
- **packed link（紧凑 DP 链节）**：Phase1 **正向**把 **最优路径上的 token** 写入 `temp_A`（紧凑位级；流式 LZDP 以 **§3.1** `forward_dp` + `flagwrite` 为准）。常用 `(length, offset)` 表述一条记录（`length = 0` 表示字面量、该端常承载字面量字节；否则为匹配的长度与距离，与 Triple 字段对应）。记录**不含 Huffman 码**；语义上可从文件末尾沿链回溯，故口语称 **packed link**（`packedline` 同指）。

#### 流式分块与四状态（I/O 形态）

四状态机里的「代码」若只写 `while cur > 0` / `readPair`，容易看起来像**整文件进内存**的单遍算法。工程上应显式区分下面几层（与 **§3.1 / §3.2 / §6.2 / §6.4** 一致）：

| 状态 | 流式分块角色 | 典型 I/O / 内存 |
|------|-------------|----------------|
| **COLLECT_INPUT** | **输入侧三分块**环形队列 + 每轮只把 **current** 上 `dp_core` 结果 `flagwrite` **追加**到 `temp_A`（顺序写、`flush` 后可释放已滚出的 `prev`） | 内存 ≈ 3×分块；**§3.1 `forward_dp`**、**§二** |
| **BACKTRACK** | **`temp_A` 逆向二分块**读（`FileBitReader` + `CHUNK_BITS` + `cur_raw`/`next_raw` + **`VirtualBuffer`**，`pos` 从尾块边界向文件头扫）；**`temp_B` 正向追加**写（`BitWriter`，可按块 `flush`） | 内存 ≈ 2×`temp_*` 分块 + 频率表；**§3.2「二分块逆向读取 temp A」**、**§6.2** |
| **BUILD_TREE** | **无** temp 分块 I/O；仅在内存中对已累满的 `freq_*` / `*_freq_3hm` 建树 | 与分块无关 |
| **EMIT_TOKENS** | **`temp_B` 正向二分块**读（与 BACKTRACK 写 `temp_B` 顺序一致；同样 **`VirtualBuffer`** 处理跨块变长记录）；压缩比特流写出（`ReverseBitWriter` 等属**输出比特布局**，不改变 `temp_B` 的读向） | 内存 ≈ 2×分块 + 编码缓冲；**§6.2** |

**频率表**：`BACKTRACK` 在扫 `temp_A` 时**边读边累加**到内存中的全局表；不要求按块存盘。下文伪代码用 `readerA`/`writerB`/`CHUNK_BITS`/`vb`/`pos` 等名字，就是要把**分块读写的控制面**写进设计，而不是省略成整文件抽象。

```
状态机: COLLECT_INPUT → BACKTRACK → BUILD_TREE → EMIT_TOKENS

与流式 LZDP 两阶段、分块结构的对应（概念对齐；DPFlate 实现细节以代码为准）：
- **COLLECT_INPUT** ≈ 流式 LZDP **Phase 1（`forward_dp`，§3.1）**：**三分块**环形缓冲、`VirtualBuffer`、`dp_core`、`flagwrite` → `temp_A`（与上文「packed link」语义一致）。
- **BACKTRACK** ≈ 流式 LZDP **Phase 2（回溯前半）**：对 `temp_A` **二分块逆向**读取链节，正向写出 `temp_B`（token 流，正向落盘），并统计 Huffman 频率。
- **BUILD_TREE**：由频率表建树（与分块 I/O 无关）。
- **EMIT_TOKENS** ≈ 流式 LZDP **Phase 2（发码后半）**：对 `temp_B` **二分块正向**读取（与 BACKTRACK 写盘顺序一致），按 token 顺序 Huffman 发码；**不再**对 `temp_B` 做逆向分块 I/O——逆向已在 BACKTRACK 对 `temp_A` 完成。

COLLECT_INPUT:
  输入:  原始字节流（分块读取）
  输出:  temp_A.dat（紧凑链节 / token 流，正向顺序）
  操作:  **同 §3.1 `forward_dp`**：三分块、`VirtualBuffer`、`dp_core`、`flagwrite` → temp A（不在此重复 DP 细节）

BACKTRACK:
  输入:  temp_A.dat（逆向读取）
  输出:  temp_B.dat（packed link 格式，正向顺序）
         freq_map / dist_freq（FLATE 频率统计）
         literal_freq_3hm / offset_freq_3hm / length_freq_3hm（3HfMTree 频率统计；各一棵树一维表）
  操作:  逆向回溯得到正向 token 序列，同时统计频率；**二分块**逆向读 `temp_A`

BUILD_TREE:
  输入:  频率统计表
  输出:  序列化的 Huffman 树（FLATE 两树或 3HfMTree 三树）
  操作:  根据 use_3hfmtree_ 标志选择树策略

EMIT_TOKENS:
  输入:  temp_B.dat（正向读取）+ Huffman 树
  输出:  压缩比特流
  操作:  **正向**顺序读 `temp_B`（二分块滚动），按与 token 解压顺序一致的次序发 Huffman 码；压缩体在文件中的**物理**排布若仍用 `ReverseBitWriter` 从尾向前长，由**写端**处理，**不**要求对 `temp_B` 再做逆向读
```

**为何 EMIT 用正向读 `temp_B`**：BACKTRACK 已从 `temp_A` 完成沿链的**逆向**遍历，并把结果按**时间正序**写入 `temp_B`；`temp_B` 上语义已是「从第一个 token 到最后一个」。再对 `temp_B` 逆向读会与该语义重复，且多一次逆向 I/O。正向读与解压消费顺序一致，**语义清晰、顺序扫描更利于缓存与吞吐**。若实现仍用 `ReverseBitWriter` 从压缩区物理末尾向前写比特，可在**比特写指针**或小块缓冲上适配，而不必以「逆向读 `temp_B`」为代价。

#### 5.4.1 COLLECT_INPUT 阶段

**即流式 LZDP 的 Phase 1**：算法、分块滚动、`dp_core` 与 `flagwrite` 落盘等**一律以 §3.1「Phase 1：前向 DP → Temp File A」为准**（含 `forward_dp` 伪代码与 `VirtualBuffer` / `core_begin`–`core_end` 约定）。DPFlate 只是把该段收进 `COLLECT_INPUT` 状态，**不要**在此处另写「逐位置 `dp[i]` + 当场 `write_packed_link`」类伪代码——既与 §3.1「DP 在 `dp_core`、框架管三分块与 I/O」分工矛盾，也不符合真实写入时机（对 **`dp_core` 返回的整条** optimal triple 流顺序落盘，而非每个下标即时写链节）。

#### 5.4.2 BACKTRACK 阶段

**分块要点**：`temp_A` **不可**一次性载入；**逆向二分块**与 **§3.2** `backtrack()` 中「`FileBitReader` + `CHUNK_BITS` + `VirtualBuffer` + 从尾向头扫 `pos`」同构。区别仅在于：解析出 `(length, offset)` 后**正向追加**写入 **`temp_B`**，并在内存中累加 **FLATE / 3HfM** 频率表（见上表）。

```
CHUNK_BITS   = 512 * 1024 * 8                    // 与 §3.2 一致；比特为单位的分块窗口
readerA      = FileBitReader(temp_A_path)      // 可 seek，不全量 mmap
writerB      = BitWriter(temp_B_path)          // 正向追加 temp_B；大文件时块末 flush

file_end     = readerA.size_in_bits()
chunk_end    = file_end
// 首轮：从文件尾读 cur_raw、预读更靠近文件头的 next_raw；vb 拼接；pos = vb.size_in_bits()
// （初始化 8～12 行与 §3.2 L603–620 相同，仅把 temp_A 换成本路径的 readerA）

while cur_raw is not empty:
    while pos > 0:
        // 在 vb 上从 pos 逆向解析一条 packed link（flag-at-end；语义同 §3.2 read_token_at）
        (length, offset, record_bits) = read_packed_link_at(vb, pos, Ob, Lb)
        pos -= record_bits

        // 只处理「起点落在当前 cur_raw 段（vb 的前 U 个逻辑比特）」内的记录；起点落在 next_raw 段的留到换窗后再提交（§3.2 L628–630；数值例见 §5.4.5）
        if pos < cur_raw.size_in_bits():
            writePackedLink(writerB, length, offset)
            total_tokens_++

            if length == 0:
                literal_code = map_byte_to_flate_literal(offset)
                freq_map_[literal_code]++
                literal_freq_3hm_[offset]++
            else:
                getLengthCode(length, code, ...)
                getDistCode(offset, dist_code, ...)
                freq_map_[code]++
                dist_freq_[dist_code]++

                Ob = offset_bits_3hm()
                Lb = length_bits_3hm()
                mask_ob = (1 << offset_huffman_bwidth) - 1
                mask_lb = (1 << length_huffman_bwidth) - 1
                n_ob = ceil(Ob / offset_huffman_bwidth)   // ceil：向上取整（ceiling）
                n_lb = ceil(Lb / length_huffman_bwidth)
                for i = 0 .. n_ob-1:
                    off_chunk = (offset >> (i * offset_huffman_bwidth)) & mask_ob
                    offset_freq_3hm_[off_chunk]++
                for j = 0 .. n_lb-1:
                    len_chunk = (length >> (j * length_huffman_bwidth)) & mask_lb
                    length_freq_3hm_[len_chunk]++

    writerB.flush()
    free(cur_raw)
    cur_raw = next_raw
    // 按 §3.2 L641–650：从 chunk_end 再向文件头读入新的 next_raw；重建 vb；pos = vb.size_in_bits()
```

#### 5.4.3 BUILD_TREE 阶段

**配置**：**`offset_huffman_bwidth`**、**`length_huffman_bwidth`** 为 Huffman 多级槽的**固定位宽**（比特）；两棵树的**槽数上界**分别为 **`(1 << offset_huffman_bwidth)`**、**`(1 << length_huffman_bwidth)`**。不再使用分裂因子 `k`、`huffman_split_k` 或由 k 反推的 `w_ob` / `w_lb`。

```
if use_3hfmtree_:
    // 3HfMTree：字面量 + offset + length 各一棵哈夫曼树；槽宽由 offset_huffman_bwidth / length_huffman_bwidth 固定
    huffman_tree_3hm_.buildTrees(
        literal_freq_3hm_,
        offset_freq_3hm_,
        length_freq_3hm_,
        (1 << offset_huffman_bwidth),   // offset 树 alphabet（槽数）
        (1 << length_huffman_bwidth),   // length 树 alphabet（槽数）
        offset_bits_3hm(),              // Ob
        length_bits_3hm(),              // Lb
        chunk_bits)                     // 当前实现仍为单一 chunk_bits；与 offset/length 双槽宽对齐策略以代码为准，演进后此处应分传两宽
    huffman_tree_3hm_.serialize(writer_)
else:
    // FLATE：两树哈夫曼（标准 Deflate）
    freq_map_[256] = 1  // EOB
    huffman_tree_ = HuffmanTree(freq_map_, 286, 9)
    dist_tree_ = HuffmanTree(dist_freq_, 30, 5)
    huffman_tree_.serializeTree(writer_)
    dist_tree_.serializeTree(writer_)
```

#### 5.4.4 EMIT_TOKENS 阶段

**分块要点**：`temp_B` **正向二分块**读，与 **§6.2** 对偶于 BACKTRACK：内存仍 **`cur_raw` + `next_raw` + `VirtualBuffer`**，`pos` 从 **0** 向 **`vb.size_in_bits()`** 递增；仅当记录**起点**落在当前前块 `cur_raw` 内时再发码（与 §3.2 中 `pos < cur_raw.size_in_bits()` 判断**同一形式**，只是读向相反；数值例见 **§5.4.5 例 D**）。压缩比特写 `ReverseBitWriter` 时属另一层，不在这里展开。

```
CHUNK_BITS   = 512 * 1024 * 8
readerB      = FileBitReader(temp_B_path)
writerOut    = ...  // BitWriter / ReverseBitWriter；与 temp_B 分块读解耦

chunk_off = 0
literal_run = []     // 3HfM：游程缓冲须为 EMIT 状态机成员，跨「正向二分块」外循环保留
// 首轮：从文件头读 cur_raw、再读 next_raw；vb = VirtualBuffer([cur_raw, next_raw]); pos = 0

while cur_raw is not empty:
    while pos < vb.size_in_bits() and emitted_tokens_ < total_tokens_:
        (length, offset, record_bits) = read_packed_link_forward_at(vb, pos, Ob, Lb)
        if pos < cur_raw.size_in_bits():
            if use_3hfmtree_:
                if length == 0:
                    literal_run.push_back(offset)   // offset 端存字面量字节
                else:
                    flush_literal_run(literal_run, writerOut, huffman_tree_3hm_)
                    literal_run.clear()
                    huffman_tree_3hm_.encodeMatch(offset, length, writerOut)
            else:
                if length == 0:
                    encode_literal_flate(offset, writerOut, ...)
                else:
                    encode_match_flate(offset, length, writerOut, ...)
            emitted_tokens_++
        pos += record_bits

    writerOut.flush()
    free(cur_raw)
    cur_raw = next_raw
    // 从 chunk_off 顺推读入下一块为 next_raw；重建 vb；pos 归零；游程缓冲不丢

if use_3hfmtree_:
    flush_literal_run(literal_run, writerOut, huffman_tree_3hm_)
else:
    encode_huffman(writerOut, dictionary_[256])   // EOB
```

#### 5.4.5 二分块 + VirtualBuffer：数值示例（消歧义）

以下与 **§3.2**、上文 **5.4.2 / 5.4.4** 采用**同一约定**：

- `vb = VirtualBuffer([cur_raw, next_raw])`：`vb` 的**逻辑比特下标** `[0, U)` 映射到 **`cur_raw`**，`[U, U+V)` 映射到 **`next_raw`**，其中 `U = cur_raw.size_in_bits()`，`V = next_raw.size_in_bits()`（`next_raw` 可为空窗，略）。
- **逆向 BACKTRACK**：`pos` 从 `U+V` 向 `0` 递减；一条 packed link 在 `vb` 上占据半开区间 **`[s, e)`**，`e` 为解析前 cursor（即「记录末端之后」），`s = e - record_bits` 为**起点**；`read_packed_link_at` 在 **`[s, e)` 内**读比特，**可跨越下标 `U`**，由 VirtualBuffer 路由到两段物理存储——**一次调用即整条记录**，不存在「半条记录拆到两次外循环才拼完」。
- **条件 `pos < cur_raw.size_in_bits()`**（解析后把 `pos` 设为 `s`）：语义是 **`s < U`**，即**本条记录的起点落在当前窗的 `cur_raw` 段**。它不禁止「记录的比特尾部在 `next_raw` 段」；禁止的是「**起点**已在 `next_raw` 段、却仍在本轮当作 `cur_raw` 责任提交」——那样会在换窗前**漏写**或换窗后**重复写**。

**例 A：整条落在 `next_raw` 段（本窗不提交）**

- 设 `U = 1000`，`V = 800`，`pos` 初值 `1800`。某次解析得 `record_bits = 40`，且该链节完全位于 `[1160, 1800)` 内，故 `s = 1760`。
- `s = 1760 ≥ U`：起点在 **`next_raw`**。本窗外循环**不** `writePackedLink`。
- 外层 `free(cur_raw)`、`cur_raw := next_raw`、读入新 `next_raw` 后，该链节在新 `vb` 里落入 **`cur_raw` 段**，下一轮再提交。**不是**「半条没读」；是「**归属**仍算旧 `next_raw` 上的链节，等滑窗后再归账」。

**例 B：跨 `U` 边界、一次解析且本窗提交（消除「跨块=拆两次」的误解）**

- 仍设 `U = 1000`。`e = 1050`（`next_raw` 段内靠近边界），`record_bits = 120`，则 `s = 930 < U`。
- 比特区间 `[930, 1050)` 跨越逻辑下标 `U=1000`：`[930,1000)` 在 **`cur_raw`**，`[1000,1050)` 在 **`next_raw`**。`read_packed_link_at(vb, e, …)` 内部对 `vb` 随机访问，**无需**把文件再读一遍或到外循环拼接。
- 解析后 `pos = 930`，满足 `930 < U`，本窗 **`writePackedLink` + 累频**。

**例 C：何时可以 `free(cur_raw)`**

- 当内层循环在本窗内已处理完所有「`s < U`」且应在本窗提交的链节后，`pos` 可能仍 `>0`，但余下未提交的链节均满足 **`s ≥ U`**（起点全在旧 `next_raw`）。此时释放旧 `cur_raw` 不会丢掉任何「起点仍落在被释放块」的数据——因为 **vb 里 `cur_raw` 段与物理上的这一块一一对应**，未提交的链节起点全在**另一段** `next_raw`，换窗后变为新的 `cur_raw`。

**例 D：EMIT 正向（与上对偶）**

- `pos` 从 `0` 递增；`read_packed_link_forward_at(vb, pos, …)` 返回的链节占据 `[pos, pos+record_bits)`。**仅当**「本条起点」`pos < U` 时发码，再 `pos += record_bits`。
- 若某链节为 `[820, 980)` 跨越 `U=900`：一次正向解析可读满；若 `820 < U`，本窗发码，**尾部在 `next_raw` 段无歧义**。
- **3HfM 字面量游程**：`literal_run` 为状态机成员，**跨正向二分块外循环保留**；换块时**不** `clear()`，避免字面量游程在物理块边界被错误切断（见 5.4.4 伪代码注释）。

### 5.5 框架与 DP Core 的数据流

```
每次分块迭代:

┌──────────┐   ┌──────────┐   ┌──────────┐
│  prev    │   │ current  │   │   new    │
│ (已处理)  │   │ (待处理)  │   │ (前瞻)   │
└────┬─────┘   └────┬─────┘   └────┬─────┘
     │              │              │
     └──────────────┴──────────────┘
                    │
                    ▼
          ┌─────────────────┐
          │  构造 buffer     │
          │ search+current+  │
          │   lookahead      │
          └────────┬────────┘
                   │
                   ▼
          ┌─────────────────┐
          │    DP Core      │  ← 纯函数调用
          │  (dp_core)      │
          └────────┬────────┘
                   │
                   ▼
          ┌─────────────────┐
          │  提取 current   │
          │  范围的 Triple   │
          └────────┬────────┘
                   │
                   ▼
          ┌─────────────────┐
          │  写入 temp A    │
          │  (位级编码)      │
          └─────────────────┘
```

### 5.6 框架职责（与算法无关）

流式框架只关心：

| 职责 | 说明 | 与算法的关系 |
|------|------|-------------|
| 分块 I/O | 从输入文件读分块，管理 3 个槽位 | 无关 |
| 构造 buffer | 拼装 search_window + current + lookahead | 无关 |
| 调用 DP Core | 传入 buffer，获取 ROLListNode 容器 | 通过 IDpCore 接口 |
| 提取范围 | 从全量 ROLListNode 中提取 current 部分 | 无关（纯位置计算） |
| 写 temp A | 将 Triple 按位级编码写入，同时累加 literal/match 计数 | 依赖 Triple 格式（Ob/Lb） |
| Phase 2 | **BACKTRACK**：`temp_A` **二分块逆向**读链节 → 正向写 `temp_B`；**EMIT**：`temp_B` **二分块正向**读并发码；输出文件可预分配 + `ReverseBitWriter`（比特级逆向与 temp_B 读向无关） | 依赖编码方案（flag/non-flag）和树策略（FLATE/3HfMTree） |

### 5.7 算法职责（与流式无关）

DP Core 只关心：

| 职责 | 说明 |
|------|------|
| Push-style DP | 从位置 0 开始推进到末尾，同时累加 literal/match 计数 |
| Match finding | 在 search window 中查找匹配 |
| 代价计算 | 字面量/匹配的比特代价 |
| 最优路径 | 回溯找到 token 数最少的路径，输出装 ROLListNode 的 DP 容器 |
| DP 状态节点 | 每个 ROLListNode 包含 triple（当前位置最优决策）、offset（字节位置）、literal_count（累计字面量数）、match_count（累计匹配数） |

---

## 六、内存管理

### 6.1 三分块释放规则

```
每次循环结束，current 完全处理后:
    // 1. 先将本轮 triples 刷入 temp_A 磁盘文件（位于 workspace/tmp/）
    temp_A.flush()

    // 2. 再释放 prev 分块内存（数据已持久化，不再依赖内存）
    prev_idx = (cur_idx - 1 + 3) % 3
    free(ring[prev_idx])

    // 3. 滚动指针
    //    current（ring[cur_idx]）保留 → 成为下一轮的 prev（提供 search window）
    //    new（ring[(cur_idx+1)%3]）成为新的 current
    //    释放的 prev 槽位读入新分块 → 成为新的 new
```

### 6.2 二分块释放规则（Phase 2：`temp_A` 回溯 / `temp_B` 发码）

**适用**：从 `temp_A` **逆向**分块读（BACKTRACK），以及从 `temp_B` **正向**分块读（EMIT）。二者都是「两块在内存、顺序推进、flush 后释放」，仅**读指针方向**不同。

```
每次 chunk 处理完毕:
    // 1. 先将输出刷入磁盘（output 是最终压缩文件，或中间缓冲）
    output.flush()

    // 2. 再释放当前 chunk 内存（数据已写入下游，不再依赖）
    free(cur_chunk)

    // 3. 滚动到下一个 chunk
    cur_chunk = next_chunk
    // 读入下一块：BACKTRACK 从 temp_A 尾向头；EMIT 从 temp_B 头向尾
```

### 6.3 C++ 释放方式

```
// 正确释放（真正释放内存，不只是 clear）
delete ring[prev_idx];                          // 如果是指针
std::vector<uint8_t>().swap(ring[prev_idx]);    // 强制释放 vector 内存
ring[prev_idx].shrink_to_fit();                 // C++11 建议释放
```

### 6.4 内存上限

任何时候内存中最多有：
- **Phase 1**：3 个分块（三分块环形队列）
- **Phase 2（BACKTRACK / EMIT）**：2 个分块在内存中滚动（二分块）
- 1 个 pending 表（大小 ≤ lookahead，主要在 Phase 1）
- 少量临时缓冲区

总内存 ≈ 3 × chunk_size + O(lookahead)

---

## 七、关键设计要点总结

| 方面 | 错误做法 | 正确做法 |
|------|---------|---------|
| DP 算法 | dp[i] 依赖 dp[i-1], dp[i-2], ... | 推进式 DP：dp[i] 只从 dp[i-1] 获得 base，向前推送 |
| 分块管理 | 二分块，search window 断档 | 三分块：prev(搜索) + current(处理) + new(前瞻) |
| 分块滚动 | 移动/复制数据 | 定长环形队列，只移动 head/tail 指针 |
| 槽位复用 | 读入新分块占用新内存 | tail 写入刚释放的 prev 槽位，3 槽循环 |
| 内存释放 | `vector.clear()` 不释放内存 | `delete` 或 `swap` 真正释放 |
| Temp file 格式 | `fwrite` sizeof 结构体 | 紧凑位级编码（BitWriter） |
| Temp file 内容 | 编码耦合的 match_offset/match_length | 算法原生的 token（Triple） |
| 编码方案 | 耦合到 cell 结构 | 编码阶段独立选择 flag/non-flag |
| 树策略 | 固定一种树结构 | 动态选择 FLATE / 3HfMTree（含多级查询） |
| 大窗口支持 | 单树 65536 符号 | 固定槽宽多级查询（offset/length 各 2^{b} 量级树槽，b 独立配置） |
| 架构 | 算法与框架紧耦合 | 算法适配器接口，框架与算法解耦 |
| 未处理语义 | i 更新了 pending 表就算处理 | i 没走到的地方就是未处理 |

---

## 八、算法洞察

### 8.1 LZ 算法（匹配查找引擎）

LZ 系列算法的核心是**在 search window 中查找与 lookahead 匹配的最长/最优子串**。本项目支持两种匹配引擎：

#### 8.1.1 KMP 匹配引擎

基于 KMP（Knuth-Morris-Pratt）算法的精确匹配查找。对每个位置 i，在 search window 中搜索与 `input[i..i+L-1]` 匹配的所有子串。

```
function kmp_search(buffer, pos, search_size, lookahead_size, top_k, min_match):
    pattern = buffer[pos : pos + lookahead_size]
    text    = buffer[pos - search_size : pos]

    // KMP 预处理 pattern 的 failure function
    // 在 text 中扫描，记录所有匹配位置和长度

    results = []
    for each match (offset, length):
        if length >= min_match:
            results.push((offset, length))

    // 按 DP top-k 筛选最优匹配
    return top_k(results)
```

**特点**：
- 精确匹配，无遗漏
- 时间复杂度 O(search_size + lookahead_size) 每次
- 适合小窗口（search_size ≤ 4096）

#### 8.1.2 HashChain 匹配引擎

基于滚动哈希的链式匹配查找。对每个 3 字节前缀计算哈希，相同哈希的位置形成链。

```
hash = (buf[i] << 10) ^ (buf[i+1] << 5) ^ buf[i+2]
slot = hash % SEARCH_SIZE

head[slot] → pos_1 → pos_2 → ... → pos_n  // 哈希链

// 沿链查找最长匹配
for each candidate in chain:
    match_len = 0
    while buf[pos + match_len] == buf[candidate + match_len]:
        match_len++
    if match_len > best_len:
        best_len = match_len
        best_offset = pos - candidate
```

**特点**：
- 近似匹配（哈希碰撞可能遗漏）
- 时间复杂度 O(chain_length) 每次
- 适合大窗口（search_size ≥ 4096）
- 链长度由 `max_chain_length` 参数控制

#### 8.1.3 匹配引擎选择

> **（非规范性，仅供参考）** 下表仅为常见工程经验的归纳，**不构成本仓库的硬性规范或选型承诺**；以实际代码、配置与上层策略为准。若与实现不一致，以代码为准。

| 场景 | 推荐引擎 | 原因 |
|------|---------|------|
| search_size ≤ 4096 | KMP | 精确匹配，无遗漏 |
| search_size ≥ 4096 | HashChain | 速度快，内存占用低 |
| 流式分块 | HashChain | 支持增量 reseed，跨分块维护哈希链 |

#### 8.1.4 流式分块中的哈希链维护

流式分块中，每次滚动后需要**增量重建**哈希链：

```
// 分块滚动后，input_buffer 前 keep_start_idx 字节被移除
// 哈希链需要从 current_i_ 位置开始重建

function reseedHashChainPrefix(end_exclusive):
    head[] = UINT32_MAX          // 清空哈希表
    for i = 0 to end_exclusive:  // 只重建保留部分的哈希链
        slot = hash(buf[i], buf[i+1], buf[i+2])
        prev_buf[i] = head[slot]
        head[slot] = i
```

增量重建保证哈希链始终与当前 input_buffer 内容一致，无需重新扫描整个文件。

### 8.2 DP 算法（代价模型与推进式 DP）

#### 8.2.1 代价模型

DP 的目标仍是**最小化压缩输出总比特数**。这里要区分两类量，**不要**把「单步边际比特」和「带频率的全局账」混成一种写法。

**（1）DP 转移里用的边际代价（每条边、每一步）**  
推进式 DP 在比较「从 `i` 走一步字面量 / 走一步匹配」时，常用**与当前 Huffman 表无关**的**固定位宽代理**（建树前即可常数时间比较），例如：

```
// 固定位宽代理（每条边各计一次；频率通过「同一条边被最优路径选多少次」间接体现）
字面量一步  ≈ 8 bits（non-flag）或 9 bits（flag）
匹配一步    ≈ Ob + Lb bits（non-flag）或 1 + Ob + Lb bits（flag）
```

这里的「代价」是**单条决策边**上的比特上界/近似，**不在公式里再乘**「全文件该符号的总频率」——总频率已经体现在路径长度与重复 token 的条数之中。

**（2）建树之后、或给整段流对账时的总比特（显式乘频率）**  
一旦 Huffman 表已定，第 *i* 个符号族对**有效载荷**（不含树本身与头的写法的工程细节）的贡献可写为：

```
总载荷_i ≈ Σ_s freq_i(s) × len(code_i(s))
```

其中 `s` 为该族里的**词**（例如 FLATE 的长度码索引、3HfM 的某个字面量字节、某个 offset 槽值、某个 length 槽值），`freq_i(s)` 为 `s` 在对应 token/槽统计流中的出现次数，`len(code_i(s))` 为建树后分配给 `s` 的码字长度。  
对 **3HfM 多级槽**：一次 match 会对 offset 树贡献 `ceil(Ob / b_o)` 次查询、对 length 树贡献 `ceil(Lb / b_l)` 次查询，**每一次查询**各对应槽符号 `s` 的一次出现，因此在统计阶段就要按槽分别 `freq++`（见 Phase 2 回溯），总比特即各级 `Σ freq×len` 之和；字面量游程头里的 `offset=0` 与 `run_length` 同理按槽进各自的树。

**（3）与「哈夫曼代价」一句话写法的关系**  
口语说「字面量代价 = `huffman_code(literal).bit_length`」时，若指**单步 DP**，应理解为**该步若发一个字面量字节，其边际比特≈该字节在当前树上的码长**；若指**整段流**，则应写成 **`freq(literal)×len`** 并对所有出现的字面量求和。匹配侧「`offset_code` + `length_code`」同理：多级时是一串槽，每槽各用自己的 `freq×len`。

其中 Ob 和 Lb 由窗口大小决定：

```
Ob = ceil(log2(search_size + 1))   // offset 语义位宽
Lb = ceil(log2(lookahead_size + 1)) // length 语义位宽
```

> **实现提示（DPFlate）**：`COLLECT_INPUT` 里当前实现无论是否启用 3HfM，DP 均用上述**固定位宽** `lit_cost_` / `match_cost_` 更新 `dp_states_`，**未**在 DP 内联用 `Σ freq×len` 的哈夫曼码长；3HfM 的真实 `freq×len` 只在 **BACKTRACK 统计 + BUILD_TREE 建树 + EMIT 发码** 中体现。若要让 DP 目标与哈夫曼后总比特严格一致，需要二遍/迭代码长等更强耦合，见实现选型。

#### 8.2.2 推进式 DP 状态转移

```
dp[i] = 到达位置 i 的最小累计代价

// 初始化
dp[0] = 0

// 推进
for i = 0 to n-1:
    base_cost = dp[i].cost

    // 字面量：从 i 推到 i+1
    if base_cost + lit_cost < dp[i+1].cost:
        dp[i+1].cost = base_cost + lit_cost
        dp[i+1].length = 0          // length=0 表示字面量
        dp[i+1].offset = buf[i]     // offset 存储字面量字节值

    // 匹配：从 i 推到 i+L
    for each match (offset, L) at position i:
        target = i + L
        if base_cost + match_cost < dp[target].cost:
            dp[target].cost = base_cost + match_cost
            dp[target].length = L
            dp[target].offset = offset
```

**关键特性**：dp[i] 只从 dp[i-1] 获得 base cost，不依赖更早的状态。这使流式分块成为可能——每个分块独立推进，跨分块用 pending 表衔接。

#### 8.2.3 Pending 表（跨分块状态衔接）

流式分块中，分块 A 末尾的 DP 推进可能"推入"分块 B 的范围。这些**未完成的推进**需要记录在 pending 表中，在分块 B 开头被消费。

```
分块 A 处理范围: [0, chunk_size)
分块 B 处理范围: [chunk_size, 2*chunk_size)

// 分块 A 末尾的推进
dp[chunk_size - 1] 的匹配可能推到 chunk_size + L - 1
这个推进落在分块 B 范围内

// pending 表记录
pending[pos] = (cost, length, offset)  // pos 是分块 B 内的位置
```

**pending 表大小** ≤ lookahead_size（最大匹配长度），内存开销可忽略。

#### 8.2.4 回溯

DP 推进完成后，从末尾向前回溯得到最优路径：

```
function backtrack(dp, end_pos):
    tokens = []
    cur = end_pos
    while cur > 0:
        token = dp[cur]  // 到达 cur 的决策
        tokens.push_front(token)
        if token.length == 0:
            cur -= 1      // 字面量
        else:
            cur -= token.length  // 匹配
    return tokens
```

回溯结果写入 temp file（Temp A），供 Phase 2 编码使用。

### 8.3 FLATE 编码（两树哈夫曼）

#### 8.3.1 设计来源

FLATE 继承自 Deflate 的 Huffman 编码方案，使用两棵树：

| 树 | 符号数 | 内容 |
|----|--------|------|
| 字面量/长度树 | 286 | 0-255 字面量 + 256 EOB + 257-285 长度码 |
| 距离树 | 30 | 0-29 距离码 |

总符号数：286 + 30 = 316（固定）

#### 8.3.2 长度码与距离码

Deflate 使用**码-附加值**两级编码：

```
长度码（字面量/长度树）:
  257-264: 长度 3-10（无附加值）
  265-268: 长度 11-18（1 bit 附加值）
  269-272: 长度 19-34（2 bit 附加值）
  ...
  285:     长度 258（无附加值）

距离码（距离树）:
  0-3:   距离 1-4（无附加值）
  4-5:   距离 5-8（1 bit 附加值）
  6-7:   距离 9-16（2 bit 附加值）
  ...
  28-29: 距离 16385-32768（13 bit 附加值）
```

这种设计将大量可能的长度/距离值映射到少量符号，避免组合爆炸。

#### 8.3.3 两阶段编码

```
Phase 1（频率统计 + 建树）:
  扫描 token 序列，统计：
    - 字面量/长度树：每个字面量字节和长度码的出现频率
    - 距离树：每个距离码的出现频率
  根据频率构建两棵 Huffman 树

Phase 2（编码输出）:
  再次扫描 token 序列，对每个 token：
    - 字面量：编码字面量/长度树中的对应符号
    - 匹配：编码长度码（+附加值）+ 距离码（+附加值）
  最后编码 EOB（256）标记结束
```

#### 8.3.4 流式分块中的 FLATE

流式分块中，FLATE 的编码范围是**整个文件**而非单个分块：

```
// 所有分块的 token 统一建树
Phase 1（跨分块）:
  for each chunk:
    forward_dp(chunk) → tokens → temp_A
    同时累加 freq_map 和 dist_freq

Phase 2（统一编码）:
  build_tree(freq_map, dist_freq)  // 全局一棵树
  for each chunk (逆向读取 temp_A):
    encode_tokens(tokens, tree) → temp_B → output
```

**为什么全局建树？** Huffman 树需要全局频率统计才能达到最优压缩率。分块独立建树会损失跨分块的统计信息。

### 8.4 3HfMTree 编码（三棵哈夫曼树 + 独立固定位宽多级查询）

#### 8.4.1 核心思想

将 match token 拆为 offset 与 length，**各一棵** Huffman 树（与 `flate-encoding-design.md` 一致）。多级查询使用两个**独立配置**的固定位宽：

- **`offset_huffman_bwidth`**：offset 槽宽（比特），字母表大小 **`2^{offset_huffman_bwidth}`**；段数 **`ceil(Ob / offset_huffman_bwidth)`**。
- **`length_huffman_bwidth`**：length / 游程槽宽（比特），字母表 **`2^{length_huffman_bwidth}`**；段数 **`ceil(Lb / length_huffman_bwidth)`**。

**废弃**：不再使用「分裂因子 `K` / `huffman_split_k`」从段数反推槽宽；槽宽**直接配置**，两侧可不同。

| 树 | 符号数（上界） | 内容 |
|----|---------------|------|
| 字面量树 | 256 | 字节值 0-255 |
| offset 树 | `2^{offset_huffman_bwidth}` | 每槽 `offset_huffman_bwidth` 位；同一棵树查多遍拼 offset |
| length 树 | `2^{length_huffman_bwidth}` | 每槽 `length_huffman_bwidth` 位；同一棵树查多遍拼 length |

总符号数（叶子上界）：`256 + 2^{offset_huffman_bwidth} + 2^{length_huffman_bwidth}`

**载荷比特（建树后，显式频率）**：三棵树各自满足 `Σ_s freq(s)·len(s)`；一次 token 若在多棵树上各查多级，则把**每一级**产生的槽符号都计入对应树的 `freq` 后再乘码长求和（与 §8.2.1「总比特」一致）。

**与当前实现（`DPFlate` + `HuffmanTree3HM`）的对照**：`HuffmanTree3HM::buildTrees` / `deserialize` 已支持 **`offset_chunk_bits` 与 `length_chunk_bits` 独立**（实现名与 `DPFlate::huffman_offset_chunk_bits_` / `huffman_length_chunk_bits_` 对应）。`DPFlate::set_huffman_chunk_bits(k)` 仍同时将两侧设为 `k`；需要不对称槽宽时调用 `set_huffman_offset_chunk_bits` / `set_huffman_length_chunk_bits`。工厂 / GUI 若仍只配置单一 `huffman_chunk_bits`，等价于两侧相同。

**关键设计**：语义位宽 **`Ob` / `Lb`** 写入文件头；末段槽值按 **`Ob % offset_huffman_bwidth`** / **`Lb % length_huffman_bwidth`**（为 0 时视为满宽）做掩码，去掉打包补零。

#### 8.4.2 固定槽宽多级查询（`offset_huffman_bwidth` / `length_huffman_bwidth`）

当 `Ob` 较大时，若单棵 offset 树叶子 `2^Ob` 会爆炸。将 `Ob` 位按 **`offset_huffman_bwidth`** 分段，每段符号进入**同一** `offset_freq` / **同一棵** offset 树；length 侧用 **`length_huffman_bwidth`** 独立计算。

```
// 例：Ob=15, offset_huffman_bwidth=4
// LSB 起每 4 位一槽，高位不足补 0；频率可出现 1110、0111 等模式
num_ob = ceil(Ob / offset_huffman_bwidth)
mask_ob = (1 << offset_huffman_bwidth) - 1
for i = 0 to num_ob-1:
    chunk = (offset >> (i * offset_huffman_bwidth)) & mask_ob
    encode_huffman(offset_tree, chunk)

// length 同理，用 length_huffman_bwidth、Lb、length_tree
```

**符号数对比**（仅 offset 侧；`Ob=16`，扫不同槽宽 `b = offset_huffman_bwidth`）：

| b | 单棵 offset 树符号上界 | 与单棵 2^16 叶相比 |
|---|----------------------|------------------|
| 2 | 4 | 大幅缩减 |
| 4 | 16 | 约 4096× 少叶子 |
| 8 | 256 | 约 256× 少叶子 |
| 16 | 65536 | 不退化 |

length 侧把上表 `Ob` 换 `Lb`、`b` 换 `length_huffman_bwidth` 即可。

**与旧「分裂因子」文档的关系**：历史上用单一 `k` 同时约束 offset/length 的段宽（`w=ceil(Ob/k)` 等）已废弃；现以 **`offset_huffman_bwidth`、`length_huffman_bwidth`** 为唯一槽宽来源，`ceil(Ob/b)` 仅表示**查询次数**，不增加树棵数。

#### 8.4.3 解码自动机（Non-flag 模式）

offset=0 天然充当哨兵，区分字面量游程和匹配：

```
                    ┌──────────────────────────────────────────┐
                    │                                          │
                    ▼                                          │
            ┌──────────────┐                                   │
            │  DECODE_OFFSET│  ← 一切回到 offset                │
            │  (多级查询)   │                                   │
            └──────┬───────┘                                   │
                   │                                           │
          ┌────────┴────────┐                                  │
          ▼                  ▼                                 │
     offset=0           offset>0                                │
          │                  │                                  │
          ▼                  ▼                                  │
  ┌──────────────┐  ┌──────────────┐                           │
  │DECODE_RUN_LEN│  │DECODE_MATCH  │                           │
  │ (多级查询)   │  │_LEN          │                           │
  └──────┬───────┘  │ (多级查询)   │                           │
         │          └──────┬───────┘                           │
         ▼                 │                                   │
  ┌──────────────┐         │                                   │
  │DECODE_LITERALS│         │                                   │
  │(字面量树×N次) │         │                                   │
  └──────┬───────┘         │                                   │
         │                 │                                   │
         └─────────────────┴───────────────────────────────────┘
```

#### 8.4.4 字面量游程的统计与截断

原始 token 序列中，连续的字面量需要合并为游程：

```
原始 token: lit(a) lit(b) lit(c) match(3,3) lit(d) lit(e) match(5,2)

游程分组:
  run1: [lit(a), lit(b), lit(c)] → offset=0, run_length=3
  match(3,3)
  run2: [lit(d), lit(e)]         → offset=0, run_length=2
  match(5,2)
```

当游程长度超过 length 树的最大编码值时，需要截断：

```
max_run = (1 << Lb) - 1   // Lb：length / 游程语义位宽，最大可表示 2^Lb-1

// 实际游程 300 个字节，Lb=8, max_run=255
编码:
  [offset=0][255][byte_0]...[byte_254]
  [offset=0][45][byte_255]...[byte_299]
```

#### 8.4.5 三数考量（动态合并策略）

三种符号族的总符号数中，取最小的两个合并为一族树：

```
例 1: 字面量=256, length 树 ≤ 2^length_huffman_bwidth, offset 树 ≤ 2^offset_huffman_bwidth
      → 最小的两个常为 length 侧 + 字面量 → 合并后与 offset 树族比较（见 flate-encoding-design §5）

例 2: 当两槽宽较小时，offset / length 两树上界接近，可按「三数考量」任选两族合并，第三族单独
```

### 8.5 编码方案选择矩阵

| 编码方案 | 树策略 | 适用场景 | 默认 |
|----------|--------|---------|------|
| Flag + FLATE | 字面量/长度树 + 距离树 | 兼容 Deflate | 可选 |
| Non-flag + FLATE | 字面量/长度树 + 距离树 | 兼容 Deflate + 无 flag | 可选 |
| Flag + 3HfMTree | 字面量树(256) + offset 树(2^offset_huffman_bwidth) + length 树(2^length_huffman_bwidth) | 大窗口 + flag 编码 | 可选 |
| Non-flag + 3HfMTree | 字面量树(256) + offset 树(2^offset_huffman_bwidth) + length 树(2^length_huffman_bwidth) | **大窗口 + 高压缩率** | **默认** |

### 8.6 流式分块中的算法协作

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  LZ 引擎    │    │  DP 引擎    │    │ 编码引擎    │
│  (匹配查找)  │───→│  (最优路径)  │───→│  (Huffman)  │
└─────────────┘    └─────────────┘    └─────────────┘
     │                   │                   │
     │ 每位置查找匹配     │ 推进式 DP 决策     │ 两阶段编码
     │ HashChain/KMP     │ pending 表跨块    │ FLATE/3HfMTree
     │ 增量 reseed       │ temp A 持久化     │ 全局建树
     ▼                   ▼                   ▼
  分块内查找          分块内推进          跨分块统计
  跨分块维护          跨分块衔接          全局编码
```

**分块精神贯穿始终**：
1. **LZ 引擎**：在每个分块内查找匹配，跨分块用增量 reseed 维护哈希链
2. **DP 引擎**：在每个分块内推进 DP，跨分块用 pending 表衔接状态
3. **编码引擎**：跨分块统计全局频率，统一建树后逐分块编码输出