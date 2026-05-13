# FLATE 编码设计

## 一、背景

### 1.1 Deflate 的两棵树

Deflate 把编码拆为两棵树：

| 树       | 符号数 | 内容                                |
| ------- | --- | --------------------------------- |
| 字面量/长度树 | 286 | 0-255 字面量 + 256 EOB + 257-285 长度码 |
| 距离树     | 30  | 0-29 距离码                          |

总符号数：286 + 30 = 316

### 1.2 问题：组合爆炸

如果把 match token 视为一个整体（offset + length 对），可能的符号数为：

```
offset_count × length_count
```

例如 offset 有 64 种、length 有 32 种 → 64 × 32 = 2048 种 match token。每种频率极低，哈夫曼编码几乎没有压缩效果。

两种解法：

- **FLATE（Deflate 方案）**：把 length 并入字面量树、offset 单独建树，避免组合爆炸
- **3HfMTree（三树哈夫曼）**：把 match token 拆为 offset 和 length 两个独立符号，分别建树

### 1.3 问题延伸：单棵树符号数过大

即使拆为 offset 和 length 两棵树，当搜索窗口很大时（如 65536），offset 树仍有 65536 个符号。一棵 65536 叶子的哈夫曼树：

- 建树开销大（O(N log N)）
- 序列化开销大（每个符号需要存储码长）
- 压缩效果下降（大量低频符号拉高平均码长）

**解法：多级哈夫曼查询** — 把 offset / length 的语义位流按可配槽宽切成多段，**各用一棵小树**（字母表为固定 `w` 位模式）做哈夫曼；**同一棵树**对同一数值**查多次**拼回，而不是为每一段各建一棵树。

对偶关系：若已知语义位宽 `B`（即 Ob 或 Lb）与段数目标 `k`，则槽宽 `w = ceil(B / k)`；若已知槽宽 `w`，则段数 `k = ceil(B / w)`。二者等价，择一配置即可由另一者导出。

***

## 二、两种编码方案

### 2.1 Flag 编码

```
字面量: [flag=1 : 1 bit][byte : 8 bits]          = 9 bits
匹配:   [flag=0 : 1 bit][offset : Ob][length : Lb] = 1+Ob+Lb bits
```

flag 位标记当前 token 类型。支持两种树策略：

- **FLATE**：字面量/长度树 + 距离树
- **3HfMTree**：字面量树 + offset 树 + length 树（**各一棵**；多级时对 offset/length 树**多查**）

> DPFlate 和 Deflate 默认使用 non-flag 编码以保证速度。Flag 编码作为可选方案。

### 2.2 Non-flag 编码

```
字面量游程: [offset=0 : Ob][run_len : Lb][byte_0 : 8]...[byte_N-1 : 8]
匹配:       [offset>0 : Ob][length : Lb]
```

offset=0 作为字面量游程的哨兵。支持两种树策略：

- **3HfMTree（默认）**：字面量树 + offset 树 + length 树（各一棵；多级多查）
- **FLATE**：字面量/长度树 + 距离树

> DPFlate 和 Deflate 默认使用 non-flag + 3HfMTree 编码。这是速度与压缩率的最佳平衡。

***

## 三、两种树策略

### 3.1 FLATE（Deflate 风格）

| 树       | 符号数 | 内容                                |
| ------- | --- | --------------------------------- |
| 字面量/长度树 | 286 | 0-255 字面量 + 256 EOB + 257-285 长度码 |
| 距离树     | 30  | 0-29 距离码                          |

适用于 flag 编码和 non-flag 编码。

### 3.2 3HfMTree（三棵哈夫曼树 + 多级查询）

3HfMTree 在结构上就是 **三棵哈夫曼树**（不是 `1+2k` 棵）：

1. **字面量树**（256 叶子，字节 0–255）  
2. **offset 树**（**一棵**；符号字母表为固定 **`w_ob` 位宽** 的槽值，至多 `2^{w_ob}` 个不同符号）  
3. **length 树**（**一棵**；同理，槽宽 **`w_lb`**，至多 `2^{w_lb}` 个符号）

**多级**的含义：把语义上的 `Ob` 位 offset（或 `Lb` 位 length）按 `w_ob`（或 `w_lb`）切成 **`k_ob = ceil(Ob / w_ob)`**（或 `k_lb`）个槽；**频率统计**时每个槽都是 `w_ob`（或 `w_lb`）位模式——若某段不足 `w_ob` 位语义，在**打包进槽时高位补 0**，仍归入**同一棵** offset 树的字母表。例如把若干 offset 的位串切成 `1110`、`0111` 等 4 位模式，统计得到「`1110` 出现 2 次、`0111` 出现 2 次」等，**只建一棵 offset 树**覆盖这些符号；编码/解码时对 **同一棵 offset 树连续查 `k_ob` 次**（length 侧同理 **`k_lb` 次查同一棵 length 树**），再把各次读出的槽值移位拼回整值。

```
k_ob = ceil(Ob / w_ob)    r_ob = Ob - (k_ob - 1) × w_ob    // 1 ≤ r_ob ≤ w_ob，末段语义位数
k_lb = ceil(Lb / w_lb)    r_lb = Lb - (k_lb - 1) × w_lb
```

**末段截断（与「哪一次查询」对应）**：凡存在 `r_* < w_*`，必有一次读出的槽值只取低 **`r_*` 位**参与拼值（掩码 `(1<<r_*)-1`）。**设计叙述**里常说「第一次查询后截断」——指**按解码顺序最先遇到「含残语义」的那一段**（与段从高到低还是从低到高打包有关）。**当前仓库** `HuffmanTree3HM::decodeMultiLevel` 为 **LSB 段先读**，掩码落在 **最后一次迭代**（见源码）；文档以「**对有效位不足 `w_*` 的那一次槽值**做掩码」为准，避免与实现细节冲突。

**`w_ob` 与 `w_lb` 分存、可调**：二者为**高级参数**，规格里**不写死**为某一常数；元数据应能**分别**保存（实现上暂可共用 `huffman_chunk_bits_`，见文末脚注）。

> **与 GUI `huffman_split_k` 的关系**：若只给共用段数目标 `k`，可预览 `w_ob=ceil(Ob/k)`、`w_lb=ceil(Lb/k)`（二者可不同）。若直接配置槽宽，则用 `k_ob=ceil(Ob/w_ob)` 等。

| 树        | 棵数 | 符号表规模（上界） | 说明 |
| -------- | --- | ----------- | --- |
| 字面量      | 1   | 256         | 单字节 |
| offset   | 1   | ≤ `2^{w_ob}` | **一棵**树；**`k_ob` 次**查表拼 offset |
| length   | 1   | ≤ `2^{w_lb}` | **一棵**树；**`k_lb` 次**查表拼 length / 游程长 |

**总符号数（叶子上界，三树相加）**：

```
256 + 2^{w_ob} + 2^{w_lb}
```

（若实现为稀疏符号或截断未用叶子，实际序列化可少于上界。）

**示例**（演示用，非写死）：Ob=16, `w_ob=8` → `k_ob=2`：仍是 **一棵** offset 树（至多 256 符号），对同一树查两次。

> **实现（`HuffmanTree3HM`）**：`offset_tree_`、`length_tree_` 各 `std::make_unique<HuffmanTree>(...)` **各一棵**；`encodeMultiLevel` / `decodeMultiLevel` 对**同一字典**循环 `num_chunks` 次，与上文「一树多查」一致。`huffman_chunk_bits_` 当前为 offset/length **共用**槽宽；规格允许将来分存 `w_ob`、`w_lb`。

适用于 flag 编码和 non-flag 编码。

### 3.3 组合矩阵

| 编码方案                | 树策略                          | 适用场景                    |
| ------------------- | ---------------------------- | ----------------------- |
| Flag + FLATE        | 字面量/长度树 + 距离树                | flag 可选                 |
| Flag + 3HfMTree     | 字面量树 + offset 树 + length 树（各一棵；多级多查） | flag 可选                 |
| Non-flag + FLATE    | 字面量/长度树 + 距离树                | non-flag 可选             |
| Non-flag + 3HfMTree | 字面量树 + offset 树 + length 树（各一棵；多级多查） | **默认**（DPFlate/Deflate） |

### 3.4 多级哈夫曼查询与分段参数

#### 3.4.1 动机

当搜索窗口很大时（如 search window = 65536），offset 位宽 Ob = 16，一棵 offset 树需要 65536 个叶子节点。这不仅建树慢，而且大量低频符号导致 Huffman 编码几乎无效。

#### 3.4.2 可配置槽宽与 K／w 对偶

- **槽宽不写死**：`w_ob`、`w_lb`（实现中可对应 `chunk_bits_ob` / `chunk_bits_lb` 或单一 `huffman_chunk_bits_`）均为**高级可调**，规格与 GUI 不得把某一数值（如 8）写成唯一合法值。
- **两侧独立**：offset 与 length **各自**保存语义位宽（`Ob`、`Lb`）与**各自**的槽宽（`w_ob`、`w_lb`）；末段掩码分别用 **`r_ob`、`r_lb`**，统称「**末段语义掩码**」，**不要**误称为「Ob 掩码」——length 路径与 Ob 无关。
- **K／w 对偶**：若配置「目标段数」`k`（如 `huffman_split_k`）且对 offset、length **共用**该 `k`，则 `w_ob = ceil(Ob/k)`、`w_lb = ceil(Lb/k)`，二者数值可不同；若配置的是槽宽，则 `k_ob = ceil(Ob/w_ob)`、`k_lb = ceil(Lb/w_lb)`。两种参数化**等价**，文档与工具应标明当前 UI 暴露的是哪一种。

#### 3.4.3 核心思想（一树多查）

offset 侧：**一棵** offset 哈夫曼树，字母表为所有出现过的 **`w_ob` 位槽值**（不足 `w_ob` 位语义时在槽内**高位补 0** 再统计）。还原一个 offset 时，对该树 **连续解码 `k_ob` 次**，每次得到一个槽符号，再按约定移位拼接；**不是** `k_ob` 棵不同的 offset 树。length 侧同理：**一棵** length 树 + **`k_lb` 次**查询。

**`Ob`、`Lb` 由文件头约定**；禁止把 `k_ob × w_ob` 当成语义位宽。

```
offset = chunk_0 | (chunk_1 << w_ob) | ... | (chunk_{k_ob-1} << (k_ob - 1) × w_ob)
// 各 chunk_i 均来自「同一棵」offset 树的解码；chunk_{k_ob-1} 仅低 r_ob 位有效（与 encode 端补零对称）
```

#### 3.4.4 对比直觉（单棵巨树 vs 一树多查）

下表对比「**单棵** 2^Ob 叶子」与「**单棵** 2^{w_ob} 叶子、查 `k_ob=ceil(Ob/w_ob)` 次」的叶子数乘积感（**不是** `k_ob` 棵树）：

| Ob | 预览用 k | `w_ob=ceil(Ob/k)` | 单棵 offset 树符号上界 | 相对单棵 2^Ob 叶子数 |
| -- | - | ----- | ------------- | ---------------- |
| 16 | 1 | 16    | 65536         | 1×    |
| 16 | 2 | 8     | 256           | 叶子数约为 2^Ob 的 1/256（多查 2 次） |
| 16 | 4 | 4     | 16            | 约为 1/4096 |
| 16 | 8 | 2     | 4             | 约为 1/16384 |

若 **`w_ob` 与 `w_lb` 分存**，length 侧用 `Lb`、`w_lb` 单独对照；**查询次数** `k_lb` 可与 `k_ob` 不同。

#### 3.4.5 边界：频率、补零与末段掩码

**问题**：误以为「不整除就要多一棵树」——正确做法是 **仍一棵树**，槽宽固定，**补零 + 同一频率表**；解码时 **多查几次**，其中**含残语义的那一次**对槽值做掩码。

**约定**：

1. **文件头写明 `Ob`、`Lb` 与 `w_ob`、`w_lb`**（或可由 `huffman_split_k` 导出）。拼值与掩码均以之为准。
2. **频率统计**：所有 offset 的所有 `w_ob` 位槽（含补零后的模式）汇入**同一** `offset_freq`；例：`1110` 计 2 次、`0111` 计 2 次等，**一棵树**一张表。
3. **解码**：对**同一** `offset_tree` 调用 `k_ob` 次解码；对有效位不足 `w_ob` 的那一次输出做 **`r_ob` 掩码**（见 §3.2 关于「第一次 / 最后一次」与实现的说明）。
4. **码长表序列化里的「4 bits」**：指存每个符号的哈夫曼码长时的**存储位宽**，与槽宽 `w_ob` 不是同一概念。

> **与实现对齐**：`HuffmanTree3HM` 中 `offset_tree_` / `length_tree_` 各一；`decodeMultiLevel` 在 **LSB 段先读** 时在**最后一轮**应用 `last_chunk_valid_bits` 掩码。

#### 3.4.6 参数配置

```
// 规格推荐：槽宽可独立落盘（示例字段名）
chunk_bits_ob   // 1..Wmax，offset 分段槽宽
chunk_bits_lb   // 1..Wmax，length 分段槽宽

// GUI 常见：单一 huffman_split_k = k → 预览用
w_ob = ceil(Ob / k),  w_lb = ceil(Lb / k)
```

- `k=1`：等价 `w_ob=Ob`、`w_lb=Lb`（单段，整值一棵树的统计粒度依实现）。
- `k=2`：在 Ob=16 时 `w_ob=8` 等——仅为**示例演算**，非强制配置。
- **末段**：凡 `Ob mod w_ob ≠ 0`（或 length 侧同理）必须走 §3.4.5 掩码逻辑。

GUI 中若只暴露 `huffman_split_k`，应**同时预览** `w_ob`、`w_lb`（二者可能不同），并提示「与独立配置两侧槽宽时的段数 `k_ob`、`k_lb` 可能不一致」。

```
w_ob = ceil(Ob / huffman_split_k)
w_lb = ceil(Lb / huffman_split_k)
k_ob = ceil(Ob / w_ob),  k_lb = ceil(Lb / w_lb)   // 自洽校验
```

***

## 四、3HfMTree 核心设计

### 4.1 关键洞察

**不需要 flag 位**。offset=0 天然充当哨兵，把解码流程分成两条路径：

```
offset=0 → 字面量游程（从 **length 树** 连续解码 `k_lb` 次拼游程长，再从字面量树解码 N 个字节）
offset>0 → 匹配（从 **length 树** 连续解码 `k_lb` 次拼匹配长）
```

解码器永远从 **offset 树**（`k_ob` 次）开始，最终回到 offset。**一切回到 offset**。

**多级查询**：对 **同一棵** offset 树解码 **`k_ob` 次**拼 offset；对 **同一棵** length 树解码 **`k_lb` 次**拼 length。

### 4.2 编码格式

```
字面量游程: [off 槽码 × k_ob][len 槽码 × k_lb][byte_0码]...[byte_N-1码]
匹配:       [off 槽码 × k_ob][len 槽码 × k_lb]
```

其中：

- **`[off 槽码 × k_ob]`**：对 **同一棵** offset 树连续输出 `k_ob` 个哈夫曼码字，对应 offset 的 `k_ob` 个 `w_ob` 位槽（与 `encodeMultiLevel` 一致）。
- **`[len 槽码 × k_lb]`**：对 **同一棵** length 树连续 `k_lb` 个码字。
- `byte` 码：字面量树。

**没有 flag 位**。offset=0 是字面量游程的哨兵，offset>0 是匹配的标记。

**示例**（`w_ob=8`，Ob=16 → `k_ob=2`）：

```
offset 值 0x1A3B → 低 8 位 0x3B、高 8 位 0x1A（LSB 槽先写，与实现一致）
编码: [第1次查 offset 树：59 的码][第2次查 offset 树：26 的码]
解码: 两次均走 **同一** offset_tree → 拼回 0x1A3B
```

### 4.3 解码自动机

```
                    ┌──────────────────────────────────────────┐
                    │                                          │
                    ▼                                          │
            ┌──────────────┐                                   │
            │  DECODE_OFFSET│  ← 一切回到 offset                │
            │ (`k_ob` 次)   │                                   │
            └──────┬───────┘                                   │
                   │                                           │
          ┌────────┴────────┐                                  │
          ▼                  ▼                                 │
     offset=0           offset>0                                │
          │                  │                                  │
          ▼                  ▼                                  │
  ┌──────────────┐  ┌──────────────┐                           │
  │DECODE_RUN_LEN│  │DECODE_MATCH  │                           │
  │(`k_lb` 次)   │  │_LEN          │                           │
  └──────┬───────┘  │ (`k_lb` 次)  │                           │
         │          └──────┬───────┘                           │
         ▼                 │                                   │
  ┌──────────────┐         │                                   │
  │DECODE_LITERALS│         │                                   │
  │(字面量树×N次) │         │                                   │
  └──────┬───────┘         │                                   │
         │                 │                                   │
         └─────────────────┴───────────────────────────────────┘
```

**多级查询伪代码**（offset / length 对称；`w_ob`、`w_lb` 来自配置或 §3.4.6 预览）：

```
decode_offset(bitstream):
    value = 0
    w = w_ob
    k = ceil(Ob / w)
    r = Ob - (k - 1) * w
    for i = 0 to k-1:
        chunk = offset_tree.decode(bitstream)   // 每次同一棵 offset_tree
        if i == k-1:
            chunk = chunk & ((1 << r) - 1)
        value = value | (chunk << (i * w))
    return value

decode_match_length(bitstream):
    value = 0
    w = w_lb
    k = ceil(Lb / w)
    r = Lb - (k - 1) * w
    for i = 0 to k-1:
        chunk = length_tree.decode(bitstream)   // 每次同一棵 length_tree
        if i == k-1:
            chunk = chunk & ((1 << r) - 1)
        value = value | (chunk << (i * w))
    return value
```

与 `HuffmanTree3HM` 一致处：`offset_tree` / `length_tree` 各一棵，`decode` 实为沿树走比特；**掩码轮次**见 §3.2 / §3.4.5。

### 4.4 解码过程示例

```
原始 token 序列: lit(61) lit(62) match(offset=3,len=3) match(offset=5,len=2)

参数: Ob=6, Lb=5；共用槽宽 `w_ob = w_lb = 3`（例如由 `huffman_split_k=2` 预览得 `ceil(6/2)=3`、`ceil(5/2)=3`）→ `k_ob = k_lb = 2`

编码后的比特流（3HfMTree + non-flag, k=2）:
[off0=0码][off1=0码][len0=2码][len1=0码][61码][62码]
[off0=3码][off1=0码][len0=3码][len1=0码]
[off0=5码][off1=0码][len0=2码][len1=0码]

解码过程:
1. DECODE_OFFSET:
   - 第 1 次查 offset 树 → 0；第 2 次查 offset 树 → 0 → 拼回 0
2. DECODE_RUN_LEN:
   - 第 1 次查 length 树 → 2；第 2 次查 length 树 → 0 → 拼回 2
3. DECODE_LITERALS → 字面量树解码 × 2 → 61, 62
4. DECODE_OFFSET:
   - 两次查 **同一** offset 树 → 3, 0 → 拼回 3 (>0)
5. DECODE_MATCH_LEN:
   - 两次查 **同一** length 树 → 3, 0 → 拼回 3 → 输出 match(3,3)
6. DECODE_OFFSET:
   - 两次查 offset 树 → 5, 0 → 拼回 5 (>0)
7. DECODE_MATCH_LEN:
   - 两次查 length 树 → 2, 0 → 拼回 2 → 输出 match(5,2)
8. DECODE_OFFSET → ...
```

### 4.5 Flag 编码 + 3HfMTree

Flag 编码使用 3HfMTree 时，flag 位仍然保留，但树结构为 **三棵**哈夫曼树 + 多级多查。

```
字面量: [flag=1 : 1 bit][byte 的哈夫曼码（字面量树）]
匹配:   [flag=0 : 1 bit][offset_0码]...[offset_{k_ob-1}码][length_0码]...[length_{k_lb-1}码]
```

解码自动机：

```
READ_FLAG
  ├─ flag=1 → DECODE_LITERAL（字面量树）→ READ_FLAG
  └─ flag=0 → DECODE_OFFSET（`k_ob` 次）→ DECODE_MATCH_LEN（`k_lb` 次）→ READ_FLAG
```

### 4.6 Non-flag 编码 + FLATE

Non-flag 编码使用 FLATE 时，offset=0 哨兵机制不变，但树结构使用 FLATE 的两棵树：

```
字面量游程: [offset=0 的距离树码][run_length 的字面量/长度树码][byte_0 的字面量/长度树码]...[byte_N-1 的字面量/长度树码]
匹配:       [offset>0 的距离树码][match_length 的字面量/长度树码]
```

关键点：

- **距离树**的 0 符号充当字面量游程的哨兵
- **字面量/长度树**同时编码：字面量字节（0-255）、游程长度（257-285 长度码）、匹配长度（257-285 长度码）
- 字面量游程中的每个字节仍然使用字面量/长度树编码，而非原始 8 位

解码自动机：

```
                    ┌──────────────────────────────────────┐
                    │                                      │
                    ▼                                      │
            ┌──────────────┐                               │
            │ DECODE_OFFSET│  ← 一切回到 offset            │
            │ (距离树)      │                               │
            └──────┬───────┘                               │
                   │                                       │
          ┌────────┴────────┐                              │
          ▼                  ▼                             │
     offset=0           offset>0                            │
          │                  │                              │
          ▼                  ▼                              │
  ┌──────────────┐  ┌──────────────┐                       │
  │DECODE_RUN_LEN│  │DECODE_MATCH  │                       │
  │(字面量/长度树)│  │_LEN          │                       │
  └──────┬───────┘  │(字面量/长度树)│                       │
         │          └──────┬───────┘                       │
         ▼                 │                               │
  ┌──────────────┐         │                               │
  │DECODE_LITERALS│         │                               │
  │(字面量/长度树×N)│       │                               │
  └──────┬───────┘         │                               │
         │                 │                               │
         └─────────────────┴───────────────────────────────┘
```

解码过程示例：

```
原始 token 序列: lit(61) lit(62) match(offset=3,len=3) match(offset=5,len=2)

编码后的比特流（FLATE + non-flag）:
[0距离码][2长度码][61字面码][62字面码] [3距离码][3长度码] [5距离码][2长度码]

解码过程:
1. DECODE_OFFSET → 距离树解码 → 0
2. DECODE_RUN_LEN → 字面量/长度树解码 → 2
3. DECODE_LITERALS → 字面量/长度树解码 × 2 → 61, 62
4. DECODE_OFFSET → 距离树解码 → 3 (>0)
5. DECODE_MATCH_LEN → 字面量/长度树解码 → 3 → 输出 match(3,3)
6. DECODE_OFFSET → 距离树解码 → 5 (>0)
7. DECODE_MATCH_LEN → 字面量/长度树解码 → 2 → 输出 match(5,2)
8. DECODE_OFFSET → ...
```

***

## 五、三数考量（动态合并策略）

### 5.1 三个数字

三种符号族的总符号数：

| 类别        | 符号数（叶子上界） | 决定因素 |
| --------- | ----------- | ---- |
| 字面量       | 256         | 固定 |
| length 树 | ≤ `2^{w_lb}` | 槽宽 `w_lb`（一棵） |
| offset 树 | ≤ `2^{w_ob}` | 槽宽 `w_ob`（一棵） |

**合计**：`256 + 2^{w_ob} + 2^{w_lb}`（与 §3.2 一致）。合并策略比较「三数」时，应使用**各仅一棵**树的符号上界，**不要**再乘 `k_ob`、`k_lb`。

若实现为「物理槽 = `2^{w_ob}` / `2^{w_lb}` 全量存码长」，序列化体积按满槽计；频率上未出现的符号叶码长可为 0 或省略依格式。

### 5.2 合并策略

取三个数中**最小的两个**合并为一族树，最大的单独成族：

```
例 1: 字面量=256, length 单树 ≤16（w_lb=4）, offset 单树 ≤256（w_ob=8, Ob=16）
      → 最小的两个是 length(16) + 字面量(256) = 272 符号
      → 合并树族(272) + offset 树(256)

例 2: 字面量=256, length 单树 ≤256（w_lb=8, Lb=16）, offset 单树 ≤4（w_ob=2, Ob=3）
      → 最小的两个是 offset(4) + 字面量(256) = 260 符号
      → 合并树族(260) + length 树(256)

例 3: 字面量=256, length 单树 ≤4（w_lb=2, Lb=3）, offset 单树 ≤4（w_ob=2, Ob=3）
      → 最小的两个是 length(4) + offset(4) = 8 符号
      → 合并树族(8) + 字面量树(256)
```

总符号数远小于 Deflate 的 316，且树结构根据数据特征动态调整。

### 5.3 合并后的解码顺序

合并后的自动机（以合并 offset + length 为例）：

```
合并树族（offset + length）:
  解码 offset（`k_ob` 次查询）→ 如果 offset=0 → 进入 DECODE_RUN_LEN（`k_lb` 次查询，仍在合并树族中）
                          → 如果 offset>0 → 进入 DECODE_MATCH_LEN（`k_lb` 次查询，仍在合并树族中）

字面量树:
  解码字面量字节值
```

***

## 六、字面量游程的统计与截断

### 6.1 游程统计

原始 token 序列中，连续的字面量需要合并为游程：

```
原始 token 序列: lit(a) lit(b) lit(c) match(3,3) lit(d) lit(e) match(5,2)

游程分组:
  run1: [lit(a), lit(b), lit(c)] → offset=0, run_length=3
  match(3,3)
  run2: [lit(d), lit(e)]         → offset=0, run_length=2
  match(5,2)
```

每个游程产生一个"声明头" `[offset=0, run_length]`：offset=0 经 **`k_ob` 次** offset 树编码；游程长度经 **`k_lb` 次** length 树编码（**各仍是那一棵树**，不是更多棵树）。

### 6.2 游程截断

当游程长度超过 length 树的最大编码值时，需要截断：

```
length 树最大编码值 = (1 << Lb) - 1（Lb 为 length 编码位宽）

例: Lb = 8, 最大游程 = 255
    实际游程 = 300 个连续字面量

    编码:
    [offset=0][255][byte_0]...[byte_254]
    [offset=0][45][byte_255]...[byte_299]
```

截断规则：

1. 游程长度 > 最大值 → 拆为多个游程
2. 每个游程独立编码（各自有 offset=0 声明头）
3. 最后一个游程可能不足最大值

### 6.3 频率统计

游程分组后，统计各树族的频率：

```
offset 侧（一棵 offset 树）:
  将所有 offset 的语义位切成 w_ob 位槽，不足处高位补 0，每个槽值 v ∈ [0, 2^{w_ob}) 计入同一 freq[v]。
  例：槽模式 1110、0111 各出现多次 → 同一频率表里两个符号计数增加。

length 侧（一棵 length 树）:
  同上，用 w_lb、同一 length_freq。

字面量树:
  各字节出现次数（所有字面量）。
```

***

## 七、代价模型

### 7.1 基本代价

哈夫曼编码的代价 = 频率 × 码长：

```
代价(symbol) = frequency(symbol) × code_length(symbol)
```

### 7.2 多级树族的代价

```
字面量代价 = 字面量树中该字节的码长
offset 代价 = Σ_{i=0}^{k_ob-1} code_len( offset_tree, 第 i 个 w_ob 槽符号 )   // 同一棵树查 k_ob 次
length 代价 = Σ_{j=0}^{k_lb-1} code_len( length_tree, 第 j 个 w_lb 槽符号 )

匹配总代价 = offset 代价 + length 代价
字面量游程代价 = offset 代价（offset=0 的 k_ob 个槽） + length 代价（run_length 的 k_lb 个槽） + Σ 各字面量码长
```

### 7.3 DP 中的使用

DP 在决策时用哈夫曼码长代替原始位宽：

```
// 建树前（第一遍扫描）：用原始位宽做近似代价
字面量代价 ≈ 8 bits
匹配代价   ≈ k_ob × w_ob + k_lb × w_lb bits   // 或建树后用真实 Huffman 码长之和；此为槽位上界粗算

// 建树后（第二遍编码）：用哈夫曼码长
字面量代价 = huffman_code(literal).bit_length
匹配代价   = Σ_{i=0}^{k_ob-1} len(第 i 个 offset 槽码字) + Σ_{j=0}^{k_lb-1} len(第 j 个 length 槽码字)   // 各码字来自各自那一棵树，重复 k 次
```

***

## 八、与 Deflate 的对比

| 方面    | Deflate      | FLATE        | 3HfMTree                              |
| ----- | ------------ | ------------ | ------------------------------------- |
| 树数量   | 2 棵（固定）      | 2 棵（固定）      | **3 棵**（字面量 + offset + length）；多级时只对后两棵 **重复查表**，**不**增加树棵数 |
| 字面量树  | 286 符号（含长度码） | 286 符号（含长度码） | 256 符号（纯字面量）                          |
| 长度编码  | 在字面量/长度树中    | 在字面量/长度树中    | **一棵** length 树（槽宽 `w_lb`），每 token **`k_lb` 次**输出码字 |
| 距离编码  | 独立距离树（30 符号） | 独立距离树（30 符号） | **一棵** offset 树（槽宽 `w_ob`），每 token **`k_ob` 次**输出码字 |
| 总符号数  | 316（固定）      | 316（固定）      | `256 + 2^{w_ob} + 2^{w_lb}`（§3.2；**非** `1+2k` 棵树的叶子和） |
| 大窗口支持 | 差（30 距离码限制）  | 差（30 距离码限制）  | 优（多级查询，Ob 可任意大）                       |
| 区分方式  | 符号值范围        | 符号值范围        | offset=0 哨兵                           |
| 字面量编码 | 单字节独立编码      | 单字节独立编码      | 游程合并编码                                |

***

## 九、序列化格式

### 9.1 文件头

```
[编码方案: 1 bit]       // 0=flag, 1=non-flag
[树策略: 1 bit]         // 0=FLATE, 1=3HfMTree
[huffman_split_k: 3 bits]  // 可选：GUI 共用段数预览；与下面两字段二选一或并存时以解码器约定为准
[chunk_bits_ob: 4 bits] // 规格：offset 分段槽宽 w_ob（与实现 `huffman_chunk_bits_` 对齐时可由 k 推导写入）
[chunk_bits_lb: 4 bits] // 规格：length 分段槽宽 w_lb（可与 ob 相同；独立演进时必分存）
[三数考量标志: 2 bits]  // 仅 3HfMTree: 00=三树族, 01=合并offset+length, 10=合并literal+length, 11=合并literal+offset
[Ob: 7 bits]            // offset 语义位宽
[Lb: 7 bits]            // length 语义位宽
[字面量树码长序列: 256 × 4 bits]
[offset 树码长序列: 2^{w_ob} × 4 bits]   // 仅一棵 offset 树；4 bits 为码长存储定宽
[length 树码长序列: 2^{w_lb} × 4 bits]   // 仅一棵 length 树
```

> **头字段与实现对齐**：当前 `HuffmanTree3HM` 反序列化只接收**单一** `chunk_bits`，可令 `chunk_bits_ob == chunk_bits_lb`；规格列出两字段是为**允许将来分立**，解码器应优先使用与编码端一致的字段集合。

### 9.2 编码数据

```
FLATE + flag:     [flag][字面量/长度树码] [flag][距离树码] ...
FLATE + non-flag: [距离树码][字面量/长度树码][字面量/长度树码]...
3HfMTree + flag:  [flag][字面量树码] [flag][offset_0码]...[offset_{k_ob-1}码][length_0码]...[length_{k_lb-1}码] ...
3HfMTree + non-flag: [offset_0码]...[offset_{k_ob-1}码][length_0码]...[length_{k_lb-1}码][字面量树码]...
```

