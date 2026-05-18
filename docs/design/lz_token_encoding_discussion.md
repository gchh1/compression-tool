# LZ Token 编码方案大讨论

## 1. 问题提出

当前 LZDP 的 token 编码为三元组 `(offset, length, literal)`，每个 token 包含：

| 字段 | 含义 | 位宽 |
|------|------|------|
| flag | 1=字面量, 0=匹配 | 1 bit |
| offset | 匹配距离 | offset_bits |
| length | 匹配长度 | length_bits |
| literal | 字面量字节 | 8 bits |

编码时：
- 字面量 token：`1` + `8-bit literal` = **9 bits**
- 匹配 token：`0` + `offset_bits` + `length_bits`

**核心质疑：既然已经去掉了 next_byte 设计，为什么匹配 token 还要携带一个从不使用的 literal 字段？为什么字面量需要一个独立的 flag bit 来区分？**

---

## 2. 提案：offset=0 哨兵编码

### 2.1 方案描述

将 token 统一为二元组 `(offset, length)`，利用 `offset=0` 作为字面量游程的哨兵：

```
Token := (offset, length)

若 offset == 0:
    后跟 length 个字面量字节（literal run）
    语义：连续 length 个无匹配的单字符

若 offset > 0:
    匹配 token
    语义：从当前位置向前 offset 字节处，复制 length 字节
```

### 2.2 位布局

```
字面量游程: [offset=0 (offset_bits)] [run_length (length_bits)] [byte_0] [byte_1] ... [byte_{N-1}]
匹配:       [offset>0 (offset_bits)] [match_length (length_bits)]
```

### 2.3 与当前方案对比

设 `B_o = offset_bits`, `B_l = length_bits`。

| 场景 | 当前方案 | 提案方案 | 差值 |
|------|---------|---------|------|
| 单字面量 (N=1) | 1 + 8 = 9 | B_o + B_l + 8 | 提案多 B_o+B_l-1 |
| 字面量游程 (N) | N × 9 | B_o + B_l + N×8 | 提案节省 N - (B_o+B_l) |
| 匹配 | 1 + B_o + B_l | B_o + B_l | 提案节省 1 bit |

---

## 3. 严谨论证

### 3.1 字面量游程盈亏分析

**盈亏平衡点**：`N = B_o + B_l`

典型配置 `B_o=12, B_l=8` → 平衡点 N=20。

| 游程长度 N | 当前 (bits) | 提案 (bits) | 节省 |
|-----------|------------|------------|------|
| 1 | 9 | 28 | **-19** |
| 5 | 45 | 60 | **-15** |
| 10 | 90 | 100 | **-10** |
| 20 | 180 | 180 | **0** |
| 50 | 450 | 420 | **+30** |
| 100 | 900 | 820 | **+80** |
| 255 (max) | 2295 | 2060 | **+235** |

### 3.2 为什么短字面量游程的损失可以接受

**论证 1：DP 天然倾向匹配**

DP 的目标是最小化 token 数。只要存在长度 ≥ MIN_MATCH 的匹配，DP 就会选择匹配而非字面量。因此，在可压缩数据中，孤立的短字面量游程极少出现——它们会被 DP 吸收到相邻的匹配中（通过子匹配展开）。

**论证 2：不可压缩数据的极限情况**

对于完全随机数据（无任何匹配），整个输入就是一个字面量游程。此时：
- 当前方案：`N × 9` bits
- 提案方案：`B_o + B_l + N × 8` bits

对于 N > B_o + B_l（即 N > 20），提案方案**始终更优**。对于任意非平凡输入（N >> 20），提案方案节省约 11% 的空间。

**论证 3：匹配 token 的确定性收益**

每个匹配 token 节省 1 bit（flag bit）。在压缩率良好的数据中，匹配 token 占绝大多数，这个节省是稳定的正向收益。

### 3.3 最坏情况分析

最坏情况：数据呈现"交替模式"——字面量和匹配交替出现，且字面量游程长度均为 1。

例如：`L M L M L M ...`（L=单字面量, M=匹配）

- 当前方案：每个 L=9 bits, 每个 M=1+B_o+B_l bits
- 提案方案：每个 L=B_o+B_l+8 bits, 每个 M=B_o+B_l bits

每对 (L, M)：
- 当前：9 + 1 + B_o + B_l = 10 + B_o + B_l
- 提案：B_o + B_l + 8 + B_o + B_l = 8 + 2(B_o + B_l)

差值：提案多出 `(B_o + B_l) - 2` bits 每对。

以 B_o=12, B_l=8 计：每对多出 18 bits。

**但这种模式在实际数据中几乎不可能出现**，因为：
1. 如果数据有大量匹配，说明数据是高度冗余的——冗余数据中字面量游程往往较长
2. 如果数据没有匹配，那就全是字面量游程（回到论证 2）
3. 交替模式意味着数据在"可匹配"和"不可匹配"之间高频切换，这在自然数据中极为罕见

### 3.4 信息论视角

当前方案中，flag bit 和 literal byte 存在信息冗余：
- 当 flag=0（匹配）时，literal 字段不携带任何信息，是纯粹的浪费
- 当 flag=1（字面量）时，offset 和 length 字段不携带任何信息，也是浪费

提案方案通过 `offset=0` 哨兵统一了两种语义：
- `offset=0` 既是"无匹配"的声明，也是字面量游程的入口
- `offset>0` 既是匹配距离，也隐含了"这是一个匹配"的信息

这符合信息论的最小描述长度（MDL）原则：用最少的 bit 区分两种状态。

---

## 4. 潜在反驳与回应

### 反驳 1："offset=0 作为哨兵，损失了 offset=0 这个合法值"

**回应**：offset=0 在 LZ77 语义中本来就不是合法值——匹配距离不可能为 0（不能从当前位置复制自身）。因此 offset=0 是天然的哨兵值，不损失任何编码空间。

### 反驳 2："短字面量游程会膨胀"

**回应**：承认。对于长度 < B_o+B_l 的字面量游程，提案方案确实更大。但：
1. DP 优化会最小化短字面量游程的出现
2. 即使出现，膨胀的绝对值很小（每个短游程多几十 bits）
3. 匹配 token 的节省和长游程的节省远大于短游程的损失

### 反驳 3："length_bits 限制了字面量游程的最大长度"

**回应**：确实。如果 length_bits=8，单次字面量游程最大 255 字节。对于更长的字面量序列，需要拆分为多个游程。每个额外游程开销 B_o+B_l bits。对于极长的不可压缩数据（如 1MB 随机数据），需要约 4000 个游程，额外开销约 80K bits = 10KB，相比 1MB 原始数据仅增加 1%。可以接受。

**改进方案**：如果未来需要，可以引入"游程延续"机制——当 length 达到最大值时，下一个 token 如果是 offset=0，则延续当前游程。

---

## 5. 对 DP 算法的影响

**DP 状态转移不变**。DP 仍然以单字节为粒度决策（字面量 or 匹配），游程合并是后处理步骤：

```
DP 输出: [L, L, L, M(len=5), L, M(len=3), L, L]
         ↓ 后处理合并
编码:    [lit_run(len=3), bytes..., match(offset=X, len=5), lit_run(len=1), byte, match(offset=Y, len=3), lit_run(len=2), bytes...]
```

DP 不需要感知游程合并——它只负责找到 token 数最少的路径。编码器在回溯后扫描连续字面量并合并。

---

## 6. LZDP 与 DPFlate 编码统一

### 6.1 LZDP（bit-packing 直编码）

```
Header: [offset_bits:8] [length_bits:8]
Token:  [offset: offset_bits] [length: length_bits]
        + 若 offset==0: [length 个字面量字节]
```

### 6.2 DPFlate（DEFLATE Huffman 编码）

DPFlate 使用 DEFLATE 兼容的 Huffman 编码，其 LZ 层语义等价于：

```
字面量: 直接编码字节值（alphabet 0-255）
匹配:    (length_code, distance_code) 对
```

DPFlate 的 DEFLATE 编码本身就是"字面量/匹配分离"的设计，与提案方案在语义上一致。DPFlate 不需要修改编码层，只需要将匹配引擎从 hash 链替换为 KMP。

### 6.3 统一后的 LZ 层语义

| 层面 | LZDP | DPFlate |
|------|------|---------|
| 匹配算法 | KMP + MatchRing (top-n) | KMP + MatchRing (top-n) |
| DP 策略 | dp[i] = 最少 token 数 | dp[i] = 最少 token 数 |
| 子匹配展开 | MIN_MATCH ~ len-1 | MIN_MATCH ~ len-1 |
| LZ token 语义 | (offset, length) | (literal / length+dist) |
| 字面量表示 | offset=0 游程 | DEFLATE literal code |
| 熵编码 | 无（bit-packing） | Huffman |

---

## 7. 结论

**提案方案（offset=0 哨兵）在理论上是更优的编码方案**：

1. 消除了 flag bit 和匹配 token 中无用 literal 字段的双重浪费
2. 字面量游程合并带来批量压缩收益
3. 短游程的损失在 DP 优化下被最小化，且绝对值很小
4. offset=0 是天然的哨兵值，不损失编码空间
5. 符合信息论的 MDL 原则

**建议实施**：
- LZDP：采用 offset=0 哨兵编码，替换当前 flag+literal 方案
- DPFlate：匹配引擎从 hash 链替换为 KMP，编码层保持 DEFLATE 兼容
- 两者在 LZ 层语义统一：`(offset, length)` 二元组，offset=0 表示字面量
