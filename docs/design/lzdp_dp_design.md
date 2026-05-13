# LZDP / DPFlate 动态规划（DP）算法设计

## 1. 核心思想与概念

**问题定义**：给定字节流 `S[0..m-1]`，在 LZ77 算法的滑动窗口范式下，将其分割为若干 Token（匹配引用 或 字面量），使得压缩后的整体比特开销最小。

**动态位宽与最小匹配限制（Dynamic Min Match）**：
LZDP 支持动态决定 offset_bits 和 length_bits。这意味着匹配 Token 的位宽是可变的：`sizeof(token) = offset_bits + length_bits`。
因此，为了确保“匹配”比“直接输出字面量”更节省空间，算法根据配置动态计算最小匹配长度：
`min_match_len = (offset_bits + length_bits) / 8 + 1`
只有匹配长度 `>= min_match_len`，才会形成真正的 "match_token"，否则回退为字面量处理。

**DP 状态定义**：
> `dp[i]` = 能够到达第 `i` 个字节（即覆盖 `S[0..i-1]`）所需的最少 Token 数（或最小比特成本）
- `dp[0] = 0`：起点。
- `dp[m]`：覆盖全部 `m` 个字节的最少成本（终点）。

## 2. 状态转移

对于位置 `i`，其前置状态的 token 数为 `tokennum = dp[i-1]`（假设 `dp` 以 `i-1` 表示处理完第 `i-1` 个字节后的状态）。

### 2.1 字面量兜底（Literal Fallback）
为了防止某些位置没有任何有效的匹配，或者匹配由于各种原因不可用，需要一个默认的单字符推进行为：
```text
if dp[i].tokennum > tokennum + literal_cost:
    dp[i].tokennum = tokennum + literal_cost
    dp[i].match = Literal(s[i])  // (offset=0)
```

### 2.2 匹配转移（Match Transition）
获取当前位置最多 `dp_top` 个候选匹配（通过 KMP 或 HashChain），直接跨跃匹配长度进行转移：
```text
matches = get_matches(s, i, search_window, lookahead_window, dp_top)
for match in matches:
    idx = i + match.length - 1  // 覆盖到的目标索引
    if dp[idx].tokennum > tokennum + match_cost:
        dp[idx].tokennum = tokennum + match_cost
        dp[idx].match = match
```
*注：严格遵守最精简的 DP 拓扑，不进行冗余的子匹配展开（Sub-match expansion），只依赖底层引擎提取的优质长匹配直接跃迁。*

## 3. 算法流程设计

### 3.1 LZDP (位打包直接编码方案)
LZDP 采用了“纯净”的位拼接编码，其核心理念是**将连续的字面量合并为字面量游程 (Literal Run)**，由 `offset == 0` 来标识。

**编码流程**:
1. **DP 路径求解**：通过上述 DP 状态转移（优先使用 KMP 引擎），寻找一条使得 Token 数量最少的覆盖路径。
2. **回溯解析**：从 `dp[m]` 一路回溯到 `dp[0]`，提取出一条包含 `(offset, length)` 的最优解析链。
3. **字面量游程合并 (Literal Run Grouping)**：
   由于字面量被统一标识为 `offset == 0`，算法在写入缓冲流前，会将连续的 `offset == 0` 聚合成块：
   ```text
   buffer = []
   for token in tokens:
       if token.offset == 0:
           buffer.push(token.literal)
       else:
           if not buffer.empty():
               // 遇到匹配，立即刷出之前积攒的字面量游程
               buffer.flush() 
           write(token)
   ```
4. **游程刷出 (`flush()`)**：
   受限于 `length_bits` 的最大容量（例如 8 bits 最大表示 255），一次字面量游程最多携带 `max_run` 个字符。如果积攒的 `buffer` 过长，会自动切片发出多个 `(0, chunk_len)` 的头，然后紧跟真实字符（不必浪费每个字符的 Token 位宽）。

### 3.2 DPFlate (融合 Huffman 的标准兼容方案)
与 LZDP 直接进行位打包不同，DPFlate 旨在利用 DP 来找到一条**最优的 DEFLATE 流**解析路径，其产物可以被任意标准解压器（如 gzip、zlib）直接识别。

**重构后的编码流程**:
1. **HashChain 高速引擎**：弃用 KMP，采用支持重叠匹配且时间复杂度更低（接近 O(N)）的 Hash 链引擎进行匹配搜索。
2. **比特级代价 DP**：DP 的代价不再是粗略的 Token 个数，而是近似的 DEFLATE 比特代价（字面量大约 9 bits，匹配大约 15 bits）。
3. **标准 Huffman 生成**：DP 提取出最少比特成本的路径后，抛弃 `offset=0` 的游程兜底做法，而是直接将其转换为 DEFLATE 标准的 Length Token 和 Distance Token。最后直接挂载标准的 Huffman Tree 进行序列化。

## 4. 底层引擎比较：KMP 与 HashChain 的重叠匹配支持

LZ77 常常会遇到**重叠匹配 (Overlapping Match)**的情况：当匹配的模式由于循环规律（如无数个空格）超出搜索窗口（甚至越入前瞻缓冲区自身）时，仍能够被 LZ77 算法合法表示（因为解码时是前向一边复制一边解析，`length` 可以大于 `distance`）。

在标准 Deflate 所使用的 **HashChain** 引擎中，由于匹配是简单的向后指针遍历，天然支持这种跨越 `pos` 边界的重叠匹配，因此其压缩率往往表现优异。

而在之前 LZDP 的原生 **KMP** 实现中，受到模式串必须在搜索域边界内的限制，一旦遇到这种重叠结构，就会被生硬打断成无数个短 Token，严重拖累压缩率。
为了解决这一劣势，我们对 KMP 引擎也进行了**底层升级**：通过延长 next 数组的计算并放宽前瞻限制，使得 KMP 现在也能完美支持跨越窗口的重叠匹配。

因此，目前 LZDP 和 DPFlate 提供两种引擎选择：
- **KMP 引擎**：已升级支持重叠匹配，通过严格的前缀计算寻找所有匹配，理论严谨但速度稍慢。
- **HashChain 引擎**：天然支持重叠匹配，通过哈希链表快速定位近期匹配点并支持长度截断，速度极快且压缩率极高（默认推荐）。

**创新改进**:
我们成功扩展了 KMP 在 LZ77 中的应用：通过在迭代时**打破 `search_len` 边界**，允许字符比对跨入前瞻缓冲区（`lookahead_buffer`），同时拦截非法的 `distance == 0` 结果，使得基于 KMP 的 LZDP 也能够完美发现长游程重叠匹配。这不仅修补了算法理论的盲区，还在实测中大幅提振了文本压缩率！

## 5. 总结

- **LZDP** 展现了精妙的自包含编码哲学，通过 `offset=0` 哨兵实现字面量游程的高效位打包，同时结合动态位宽判定来淘汰无效短匹配。
- **DPFlate** 则展现了极强的向后兼容性，用 DP 与 HashChain 强强联合，使得输出流满足 DEFLATE 规范且压缩率登顶。
- 两者通过共享一套坚实的动态规划（DP）底座与特征选择机制（如 ADE 下发 `use_flag_encoding`），灵活适配了多样的文件分布场景。

---

## 6. 算子设计：dp_core + 两个方向封装

### 6.1 设计思想

将 DP 算法内核抽象为一个**算子** `dp_core`，它只关心"在给定的连续字节流上，从位置 A 到位置 B 做 DP 推进"，不感知调用者是流式还是非流式。

在算子的基础上做**两个方向的封装**：
1. **非流式封装 `compress()`**：硬边界，`core_begin=0, core_end=input.size()`，一次处理整个输入
2. **流式封装 `compress_streaming()`**：软边界，传入 `core_begin`/`core_end` 限定 current_chunk 范围，search/lookahead 可延伸到相邻分块

### 6.2 算子定义：`dp_core`

```cpp
struct DpCoreResult {
    std::vector<Triple> triples;   // 最优路径（正向顺序）
    size_t literal_count;          // 字面量 token 数
    size_t match_count;            // 匹配 token 数
};

DpCoreResult dp_core(
    const std::vector<uint8_t>& input,   // 连续字节流
    size_t search_size,                   // 最大搜索距离
    size_t lookahead_size,                // 最大前瞻距离
    size_t range = 3,                     // DP top-k
    size_t core_begin = 0,                // i 指针起始位置（默认 0）
    size_t core_end = SIZE_MAX            // i 指针结束位置（默认末尾）
);
```

**语义**：
- DP 在整个 `input` 上运行，search/lookahead 不截断
- i 指针只在 `[core_begin, core_end)` 范围内推进
- 回溯从 `core_end` 开始
- 默认值 `core_begin=0, core_end=SIZE_MAX` 等价于硬边界（行为不变）
- 返回的 `literal_count` / `match_count` 是 `[core_begin, core_end)` 范围内的计数，用于 emit 阶段预计算输出文件大小

**实现要点**：
- `dp[core_begin]` 初始化为 `Node(num=0, literal_count=0, match_count=0, ...)`
- HashChain 在独立循环中为所有位置构建（包括 `core_begin` 之前的位置）
- i 指针循环范围：`for (pos = core_begin; pos < core_end; pos++)`
- 字面量推进：`literal_count = dp[pos].literal_count + 1`
- 匹配推进：`match_count = dp[pos].match_count + 1`
- 回溯起点：`dp[core_end]`（替代 `dp[in_len]`）

### 6.3 封装一：非流式 `compress()`（硬边界）

```
compress(input, search, lookahead, range):
    dp_result = dp_core(input, search, lookahead, range)
    // core_begin=0, core_end=input.size()（默认值）
    // search/lookahead 在 input 边界处硬截断
    // dp_result.literal_count / match_count 可用于预计算输出大小
    return encode(dp_result.triples)
```

**行为**：与原来的 `dp_core`（默认参数）完全一致。一次处理整个输入，边界即数据真实边界。

### 6.4 封装二：流式 `compress_streaming()`（软边界）

```
compress_streaming(buffer, search, lookahead, range,
                   search_win_size, current_size):
    core_begin = search_win_size
    core_end   = search_win_size + current_size
    dp_result = dp_core(buffer, search, lookahead, range,
                        core_begin, core_end)
    // i 指针只在 current_chunk 范围内推进
    // search 可回溯到 search_window（buffer 开头）
    // lookahead 可延伸到 new_chunk（buffer 末尾）
    // dp_result.literal_count / match_count 是 current_chunk 范围的计数
    return encode(dp_result.triples)
```

**行为**：
- `buffer = search_window + current_chunk + lookahead_window`
- i 指针从 `search_win_size` 开始，到 `search_win_size + current_size` 结束
- search window 提供历史匹配字典（不截断）
- lookahead window 提供前瞻字节（不截断）
- 回溯从 `core_end` 开始，只返回 current_chunk 范围内的 Triple

### 6.5 两种封装对比

| 方面 | `compress()`（非流式） | `compress_streaming()`（流式） |
|------|----------------------|-------------------------------|
| core_begin | 0（默认） | search_win_size |
| core_end | input.size()（默认） | search_win_size + current_size |
| 边界行为 | 硬截断 | 软边界，window 延伸 |
| 输入含义 | 完整数据 | search + current + lookahead |
| 使用场景 | 内存压缩 | 流式分块压缩 |

### 6.6 调用关系

```
非流式:  compress()
           └→ dp_core(input, search, lookahead, range)
                └→ core_begin=0, core_end=input.size()  ← 硬边界

流式:    compress_streaming()
           └→ dp_core(buffer, search, lookahead, range,
                       core_begin, core_end)
                └→ core_begin=search_win_size            ← 跳过 search window
                └→ core_end=search_win_size+current_size  ← 限定 current
```