# Algorithm 模块类/函数文件布局分析

> 用途：梳理 `src/algorithm/` 下所有类与函数的组织关系，识别可模块化的公共组件。

---

## 一、文件布局总览

```
src/algorithm/
├── include/           # 头文件（类声明）
│   ├── IAlgorithm.hpp        # 算法接口基类
│   ├── AlgorithmBase.cpp     # 基类实现（仅此 .cpp 无 .hpp）
│   ├── VirtualBuffer.hpp     # 环形槽位零拷贝拼接（模板，仅头文件）
│   ├── TempFile.hpp          # 临时文件（仅头文件）
│   ├── SpillBitStream.hpp    # TempFile 位写入器（仅头文件）
│   ├── FileBitReader.hpp     # TempFile 位读取器（仅头文件）
│   ├── KMPMatcher.hpp        # KMP 匹配引擎（模板，仅头文件）
│   ├── BlockProfile.hpp      # 块分析数据结构（仅头文件）
│   ├── HuffmanTree.hpp       # Huffman 树（FLATE 风格）
│   ├── HuffmanTree3HM.hpp    # 三树 Huffman（3HfM 风格）
│   ├── Huffman.hpp           # 纯 Huffman 压缩算法
│   ├── LZDP.hpp              # LZDP 算法（Core + Streaming 状态机）
│   ├── LZSS.hpp              # LZSS 算法（Core + Streaming 状态机）
│   ├── Deflate.hpp           # Deflate 算法（状态机）
│   ├── DPFlate.hpp           # DPFlate 算法（状态机）
│   ├── Inflate.hpp           # Deflate 解压（状态机）
│   ├── Inflate3HM.hpp        # 3HfM 解压（状态机）
│   ├── Delta.hpp             # Delta 算法（状态机 + 静态方法）
│   ├── Brotli.hpp            # Brotli 算法（状态机）
│   └── Zstd.hpp              # Zstd 算法（状态机）
├── AlgorithmBase.cpp         # AlgorithmBase::process() 实现
├── LZDP.cpp                  # LZDP / LZDP_Streaming / LZDPDecompress_Streaming
├── LZSS.cpp                  # LZSS / LZSS_Streaming
├── Deflate.cpp               # Deflate
├── DPFlate.cpp               # DPFlate
├── Inflate.cpp               # Inflate
├── Inflate3HM.cpp            # Inflate3HM
├── Huffman.cpp               # Huffman
├── HuffmanTree.cpp           # HuffmanTree
├── HuffmanTree3HM.cpp        # HuffmanTree3HM
├── Delta.cpp                 # Delta / DeltaEncode / DeltaDecode
├── Brotli.cpp                # BrotliCompress / BrotliDecompress
└── Zstd.cpp                  # ZstdCompress / ZstdDecompress
```

---

## 二、按文件详细列表

### 2.1 `IAlgorithm.hpp` — 算法接口层

| 类/结构体 | 类型 | 说明 | 使用者 |
|-----------|------|------|--------|
| `AlgorithmStatus` | struct | 状态机返回值：`bytes_consumed`、`bytes_produced`、`need_input`、`need_output`、`done` | 所有状态机算法 |
| `g_cancel_callback` | 全局函数指针 | 中断回调，外部设置后各算法定期检查 | 所有算法 |
| `IAlgorithm` | 抽象类 | 顶层接口：`process()`、`reset()`、`getBlockProfile()` | AlgorithmFactory、所有算法 |
| `AlgorithmBase` | 抽象类（继承 IAlgorithm） | 提供 `reader_`/`writer_` 成员，`process()` 实现 push/pull 循环调用 `handle()` | Deflate、DPFlate、LZDP_Streaming、LZSS_Streaming、Inflate、Delta、Brotli、Zstd |

**可模块化分析**：`AlgorithmBase` 是状态机算法的公共基类，`process()` 的 push/pull 循环逻辑可独立复用。

---

### 2.2 `VirtualBuffer.hpp` — 环形槽位容器（模板，仅头文件）

| 类 | 类型 | 说明 | 使用者 |
|----|------|------|--------|
| `VirtualBuffer<N>` | 类模板 | N 个 `std::span` 的环形拼接，`operator[]` 随机访问，`slide()`/`append()` 零拷贝 | `LZDP::dp_core`、`LZSS::lzss_core` |

**可模块化分析**：纯数据结构，无算法依赖，可独立抽取为公共组件。

---

### 2.3 `TempFile.hpp` — 临时文件（仅头文件）

| 类/函数 | 类型 | 说明 | 使用者 |
|---------|------|------|--------|
| `detail::workspace_tmp_from_env()` | 函数 | 从 `WEBCOMPRESS_WORKSPACE` 环境变量获取临时目录 | TempFile 构造 |
| `detail::spill_directory()` | 函数 | 获取 spill 目录（优先 workspace，否则 OS 临时目录） | TempFile 构造 |
| `detail::next_temp_suffix()` | 函数 | 原子递增的临时文件后缀 | TempFile 构造 |
| `TempFile` | struct | RAII 临时文件，`write()`/`readAt()`/`size()`/`close()` | `LZDP_Streaming`、`DPFlate`、`SpillBitStream` |

**可模块化分析**：纯 IO 工具，无算法依赖，可独立抽取。

---

### 2.4 `SpillBitStream.hpp` — TempFile 位写入器（仅头文件）

| 类/结构体 | 类型 | 说明 | 使用者 |
|-----------|------|------|--------|
| `PackedDpLinkSpec` | struct | DP link 记录位布局规格：`offset_bits`、`length_bits`、`record_bits`；`writeRecord()`/`readRecord()` | `LZDP_Streaming`、`DPFlate` |
| `TempFileBitAppender` | class | 向 TempFile 追加 packed bits，内部 2MB 缓冲 | `LZDP_Streaming`、`DPFlate` |

**可模块化分析**：与 TempFile 强耦合，可随 TempFile 一起抽取。

---

### 2.5 `FileBitReader.hpp` — TempFile 位读取器（仅头文件）

| 类 | 类型 | 说明 | 使用者 |
|----|------|------|--------|
| `FileBitReader` | class | 封装 TempFile 提供按位随机访问：`read_bits_at()`、`read_chunk()` | `LZDP_Streaming`（Phase 2 回溯） |

**可模块化分析**：与 TempFile 强耦合，可随 TempFile 一起抽取。

---

### 2.6 `KMPMatcher.hpp` — KMP 匹配引擎（模板，仅头文件）

| 类/函数 | 类型 | 说明 | 使用者 |
|---------|------|------|--------|
| `KMPMatchResult` | struct | 匹配结果：`offset`、`length` | `kmpSearch` 返回值 |
| `kmp_detail::MatchRing` | struct | Top-K 匹配环，保留最优匹配 | `kmpSearch` 内部 |
| `kmp_detail::buildNext()` | 函数模板 | 构建 KMP next 数组 | `kmpSearch` 内部 |
| `kmpSearch()` | 函数模板 | KMP 搜索，返回 Top-K 匹配结果 | `LZDP::dp_core`、`LZDP_Streaming`（match_engine=0） |

**可模块化分析**：纯算法模板，无项目依赖，可独立抽取。

---

### 2.7 `BlockProfile.hpp` — 块分析数据结构（仅头文件）

| 结构体 | 类型 | 说明 | 使用者 |
|--------|------|------|--------|
| `BlockInfo` | struct | 单块统计：literal/match 计数、Huffman 树大小、输出字节数、码长数组 | `BlockProfile` 的组成部分 |
| `BlockProfile` | struct | 块分析结果：`std::vector<BlockInfo>` | `IAlgorithm::getBlockProfile()` 返回值 |

**可模块化分析**：纯数据结构，无算法依赖。

---

### 2.8 `HuffmanTree.hpp` — Huffman 树（FLATE 风格）

| 类/结构体/函数 | 类型 | 说明 | 使用者 |
|----------------|------|------|--------|
| `node` | struct | Huffman 树节点：`symbol`、`frequency`、`left`、`right` | `HuffmanTree`、`Inflate`、`BrotliDecompress` |
| `Compare` | class | 节点比较器（用于优先队列） | `HuffmanTree::buildTree` |
| `HuffmanCode` | struct | 编码结果：`code`、`length`、`bits` | `HuffmanTree::buildDictionary` 返回值 |
| `writeHuffmanCode()` | 函数 | 将 HuffmanCode 写入 BitWriter | `Deflate`、`DPFlate` |
| `HuffmanTree` | class | 建树/序列化/反序列化/编码/解码 | `Deflate`、`DPFlate`、`Inflate`、`Brotli`、`Huffman` |

**可模块化分析**：与 `BitReader`/`BitWriter` 耦合，可随位操作工具一起抽取。

---

### 2.9 `HuffmanTree3HM.hpp` — 三树 Huffman（3HfM 风格）

| 类 | 类型 | 说明 | 使用者 |
|----|------|------|--------|
| `HuffmanTree3HM` | class | 三棵独立 Huffman 树（literal/offset/length），多级分块编码 | `DPFlate`（use_3hfmtree=true）、`Inflate3HM` |

**可模块化分析**：依赖 `HuffmanTree`，可随 Huffman 一起抽取。

---

### 2.10 `Huffman.hpp` — 纯 Huffman 压缩算法

| 类 | 类型 | 说明 | 使用者 |
|----|------|------|--------|
| `Huffman` | class | 纯 Huffman 压缩/解压（非状态机，直接操作 `std::span`） | AlgorithmFactory（未注册） |

**可模块化分析**：独立算法，无特殊依赖。

---

### 2.11 `LZDP.hpp` + `LZDP.cpp` — LZDP 算法族

| 类/结构体/函数 | 类型 | 说明 | 使用者 |
|----------------|------|------|--------|
| `LZDP::Triple` | struct | 匹配三元组：`offset`、`length`、`literal` | `LZDP::dp_core` 返回值、`LZDP::encode_triples` 输入 |
| `LZDP::DPCandidate` | struct | DP 候选：`offset`、`length`、`literal`、`is_chosen` | `LZDP::get_dp_visualization` |
| `LZDP::DpCoreResult` | struct | DP 结果：`std::vector<Triple>` | `LZDP::dp_core` 返回值 |
| `LZDP::DPState` | struct | DP 状态可视化 | `LZDP::get_dp_visualization` |
| `LZDP::DPStep` | struct | DP 步骤可视化 | `LZDP::get_dp_visualization` |
| `LZDP::DPVisualization` | struct | DP 全流程可视化 | `LZDP::get_dp_visualization` 返回值 |
| `LZDP::dp_core(VirtualBuffer)` | 方法 | **纯函数 DP 匹配核心**，输入 VirtualBuffer，输出 Triple 列表 | `LZDP_Streaming`（流式）、外部测试 |
| `LZDP::dp_core(vector)` | 方法 | vector 重载，拷贝到 VirtualBuffer 后调用核心 | 非流式场景 |
| `LZDP::encode_triples()` | 方法 | 将 Triple 列表编码为字节流 | `LZDP_Streaming`（EMIT_TOKENS） |
| `LZDP::decompress()` | 方法 | 全量解压 | `LZDPDecompress_Streaming`（未使用，改用状态机） |
| `LZDP::get_dp_visualization()` | 方法 | DP 可视化调试 | 外部调试 |
| `LZDP_Streaming` | class | **流式压缩状态机**：COLLECT_INPUT → BACKTRACK → EMIT_TOKENS | AlgorithmFactory（AlgorithmID::LZDP） |
| `lzdp_streaming_collect_one_index()` | 友元函数 | 单字节 DP 步进（Phase 1） | `LZDP_Streaming::handleCollectInput` |
| `LZDPDecompress_Streaming` | class | **流式解压状态机** | AlgorithmFactory（AlgorithmID::LZDPDecompress） |

**可模块化分析**：
- `LZDP::dp_core`（纯函数匹配核心）可独立于状态机复用
- `LZDP::encode_triples`（编码）可独立复用
- `LZDP_Streaming` 状态机与 `DPFlate` 共享 `COLLECT_INPUT → BACKTRACK → EMIT_TOKENS` 三阶段模式

---

### 2.12 `LZSS.hpp` + `LZSS.cpp` — LZSS 算法族

| 类/结构体/函数 | 类型 | 说明 | 使用者 |
|----------------|------|------|--------|
| `LZSSTriple` | struct | 匹配三元组：`offset`、`length`、`literal` | `LZSS::lzss_core` 返回值 |
| `LZSSCoreResult` | struct | Core 结果：`std::vector<LZSSTriple>` | `LZSS::lzss_core` 返回值 |
| `LZSS::lzss_core()` | **静态方法** | **纯函数贪婪匹配核心**，输入 VirtualBuffer，输出 Triple 列表 | `LZSS_Streaming`（流式）、`LZSS::compress`（非流式） |
| `LZSS::encode_triples()` | **静态方法** | 将 Triple 列表编码为字节流（flag/non-flag） | `LZSS_Streaming`（EMIT_TOKENS） |
| `LZSS::compress()` | **静态方法** | 全量压缩（调用 lzss_core + encode_triples） | AlgorithmFactory（当前通过 SCA 包装） |
| `LZSS::decompress()` | **静态方法** | 全量解压 | AlgorithmFactory（当前通过 SDA 包装） |
| `LZSS_Streaming` | class | **流式压缩状态机**：COLLECT_INPUT → EMIT_TOKENS → DONE | AlgorithmFactory（待切换） |

**可模块化分析**：
- `LZSS::lzss_core`（纯函数匹配核心）与 `LZDP::dp_core` 对等
- `LZSS::encode_triples`（编码）与 `LZDP::encode_triples` 对等
- `LZSS_Streaming` 状态机与 `LZDP_Streaming` 共享 `COLLECT_INPUT → EMIT_TOKENS` 模式

---

### 2.13 `Deflate.hpp` + `Deflate.cpp` — Deflate 算法

| 类/结构体/函数 | 类型 | 说明 | 使用者 |
|----------------|------|------|--------|
| `Token` | struct | Deflate token：`is_literal`、`code`、`length_extra_*`、`dist_*`、`match_len`、`match_dist` | `Deflate` 内部 |
| `Deflate` | class | **流式压缩状态机**：FIND_MATCHES → BUILD_TREE → FLUSH_TOKENS | AlgorithmFactory（AlgorithmID::Deflate） |
| `Deflate::getHash()` | 方法 | 3 字节哈希 | `Deflate` 内部 |
| `Deflate::fillWindow()` | 方法 | 填充滑动窗口 | `Deflate` 内部 |
| `Deflate::slideWindow()` | 方法 | 滑动窗口 | `Deflate` 内部 |
| `Deflate::getLengthCode()` | 方法 | 长度 → RFC1951 码 | `Deflate` 内部 |
| `Deflate::getDistCode()` | 方法 | 距离 → RFC1951 码 | `Deflate` 内部 |

**可模块化分析**：
- `getLengthCode()`/`getDistCode()` 与 `DPFlate` 中的同名方法重复，可抽取为公共工具
- `Token` 结构体与 `DPFlate` 共享

---

### 2.14 `DPFlate.hpp` + `DPFlate.cpp` — DPFlate 算法

| 类/函数 | 类型 | 说明 | 使用者 |
|---------|------|------|--------|
| `DPFlate` | class | **流式压缩状态机**：COLLECT_INPUT → BACKTRACK → BUILD_TREE → EMIT_TOKENS | AlgorithmFactory（AlgorithmID::DPFlate） |
| `dpflate_collect_input_one_index()` | 友元函数 | 单字节 DP 步进（Phase 1） | `DPFlate::handleCollectInput` |
| `DPFlate::getLengthCode()` | 方法 | 长度 → RFC1951 码（与 Deflate 重复） | `DPFlate` 内部 |
| `DPFlate::getDistCode()` | 方法 | 距离 → RFC1951 码（与 Deflate 重复） | `DPFlate` 内部 |
| `DPFlate::huff_reset_entropy_tables()` | 方法 | 重置频率表与树对象 | `DPFlate` 内部 |
| `DPFlate::huff_backtrack_accumulate_token()` | 方法 | 回溯时累计频率 | `DPFlate` 内部 |
| `DPFlate::huff_backtrack_finalize_literals()` | 方法 | 回溯末尾刷出字面量游程 | `DPFlate` 内部 |
| `DPFlate::huff_build_tree_and_write_trees()` | 方法 | 建树并序列化 | `DPFlate` 内部 |
| `DPFlate::huff_emit_token_stream()` | 方法 | 发码 | `DPFlate` 内部 |
| `DPFlateDecompress` | using | `using DPFlateDecompress = Inflate` | AlgorithmFactory |

**可模块化分析**：
- `getLengthCode()`/`getDistCode()` 与 `Deflate` 重复，应抽取公共组件
- `COLLECT_INPUT → BACKTRACK` 阶段与 `LZDP_Streaming` 同构
- `BUILD_TREE → EMIT_TOKENS` 阶段与 `Deflate` 同构

---

### 2.15 `Inflate.hpp` + `Inflate.cpp` — Deflate 解压

| 类/函数 | 类型 | 说明 | 使用者 |
|---------|------|------|--------|
| `Inflate` | class | **流式解压状态机**：READ_TREES → READ_BLOCK_HEADER → DECODE_TOKENS → COPY_MATCH → FLUSH_TO_WRITER | AlgorithmFactory（AlgorithmID::Inflate）、DPFlateDecompress |
| `Inflate::readHuffmanTree()` | 方法 | 从 BitReader 反序列化 Huffman 树 | `Inflate` 内部 |
| `Inflate::decodeLengthCode()` | 方法 | 解码长度码 | `Inflate` 内部 |
| `Inflate::decodeDistCode()` | 方法 | 解码距离码 | `Inflate` 内部 |
| `Inflate::destroyTree()` | 方法 | 销毁 Huffman 树 | `Inflate` 内部 |
| `Inflate::appendDecodedByte()` | 方法 | 追加解码字节到窗口 | `Inflate` 内部 |

**可模块化分析**：
- `decodeLengthCode()`/`decodeDistCode()` 与 `BrotliDecompress` 重复
- `readHuffmanTree()` 与 `BrotliDecompress` 重复

---

### 2.16 `Inflate3HM.hpp` + `Inflate3HM.cpp` — 3HfM 解压

| 类 | 类型 | 说明 | 使用者 |
|----|------|------|--------|
| `Inflate3HM` | class | **流式解压状态机**：READ_HEADER → DECODE_OFFSET → DECODE_RUN_LEN → DECODE_LITERALS → DECODE_MATCH_LEN → COPY_MATCH → FLUSH_TO_WRITER | AlgorithmFactory（未注册） |

---

### 2.17 `Delta.hpp` + `Delta.cpp` — Delta 算法

| 类/函数 | 类型 | 说明 | 使用者 |
|---------|------|------|--------|
| `DeltaEncode` | class | **流式压缩状态机** | AlgorithmFactory（AlgorithmID::DeltaEncode） |
| `DeltaDecode` | class | **流式解压状态机** | AlgorithmFactory（AlgorithmID::DeltaDecode） |
| `Delta::encode()` | **静态方法** | 全量 Delta 编码 | 外部测试 |
| `Delta::decode()` | **静态方法** | 全量 Delta 解码 | 外部测试 |

---

### 2.18 `Brotli.hpp` + `Brotli.cpp` — Brotli 算法

| 类/函数 | 类型 | 说明 | 使用者 |
|---------|------|------|--------|
| `BrotliToken` | struct | Brotli token | `BrotliCompress` 内部 |
| `BrotliCompress` | class | **流式压缩状态机**：FIND_MATCHES → BUILD_TREE → FLUSH_TOKENS | AlgorithmFactory（AlgorithmID::Brotli，通过 SCA 包装） |
| `BrotliDecompress` | class | **流式解压状态机**：READ_LIT_TREE0 → READ_LIT_TREE1 → READ_LEN_TREE → READ_DIST_TREE → DECODE_TOKENS → COPY_MATCH | AlgorithmFactory（AlgorithmID::BrotliDecompress，通过 SDA 包装） |

**可模块化分析**：
- `getLengthCode()`/`getDistCode()` 与 `Deflate`/`DPFlate` 重复
- `readHuffmanTree()`/`decodeLengthCode()`/`decodeDistCode()` 与 `Inflate` 重复
- `destroyTree()` 与 `Inflate` 重复

---

### 2.19 `Zstd.hpp` + `Zstd.cpp` — Zstd 算法

| 类/函数 | 类型 | 说明 | 使用者 |
|---------|------|------|--------|
| `hash3()` | 函数 | 3 字节哈希 | `ZstdCompress` 内部 |
| `ZstdCompress` | class | **流式压缩状态机** | AlgorithmFactory（AlgorithmID::Zstd，通过 SCA 包装） |
| `ZstdCompress::compress()` | **静态方法** | 全量压缩 | AlgorithmFactory（SCA 内部调用） |
| `ZstdDecompress` | class | **流式解压状态机** | AlgorithmFactory（AlgorithmID::ZstdDecompress，通过 SDA 包装） |
| `ZstdDecompress::decompress()` | 方法 | 全量解压 | AlgorithmFactory（SDA 内部调用） |
| `ZstdDecompress::readFrameHeader()` | 方法 | 读取 Zstd 帧头 | `ZstdDecompress` 内部 |

---

## 三、可模块化的公共组件

### 3.1 已识别重复代码

| 组件 | 出现位置 | 建议 |
|------|---------|------|
| `getLengthCode()` / `getDistCode()` | `Deflate.cpp`、`DPFlate.cpp`、`Brotli.cpp` | 抽取为 `LengthCodec` 或 `DeflateCodes` 公共工具 |
| `decodeLengthCode()` / `decodeDistCode()` | `Inflate.cpp`、`BrotliDecompress` | 抽取为公共解码工具 |
| `readHuffmanTree()` | `Inflate.cpp`、`BrotliDecompress` | 抽取为 `HuffmanTree::deserialize` 的静态方法 |
| `destroyTree()` | `Inflate.cpp`、`BrotliDecompress` | 抽取为 `HuffmanTree` 的静态方法 |
| `COLLECT_INPUT → BACKTRACK` 模式 | `LZDP_Streaming`、`DPFlate` | 可抽取为 DP 流式基类 |
| `FIND_MATCHES → BUILD_TREE → FLUSH_TOKENS` 模式 | `Deflate`、`BrotliCompress` | 可抽取为 LZ77 流式基类 |

### 3.2 可独立抽取的工具组件

| 组件 | 文件 | 依赖 |
|------|------|------|
| `VirtualBuffer<N>` | `VirtualBuffer.hpp` | 无 |
| `TempFile` | `TempFile.hpp` | 无 |
| `TempFileBitAppender` | `SpillBitStream.hpp` | TempFile、BitWriter |
| `FileBitReader` | `FileBitReader.hpp` | TempFile、BitReader |
| `PackedDpLinkSpec` | `SpillBitStream.hpp` | BitWriter、BitReader |
| `KMPMatcher` | `KMPMatcher.hpp` | 无 |
| `BlockProfile` | `BlockProfile.hpp` | 无 |

### 3.3 纯函数 Core（可同时服务流式和非流式）

| Core 函数 | 所在类 | 对等关系 |
|-----------|--------|---------|
| `LZDP::dp_core(VirtualBuffer)` | LZDP | DP 最优匹配 |
| `LZSS::lzss_core(VirtualBuffer)` | LZSS | 贪婪最长匹配 |
| `LZDP::encode_triples()` | LZDP | 编码 Triple → 字节流 |
| `LZSS::encode_triples()` | LZSS | 编码 Triple → 字节流 |

---

## 四、状态机模式对照

| 算法 | 压缩状态机 | 解压状态机 | 状态数 |
|------|-----------|-----------|--------|
| LZDP | `COLLECT_INPUT → BACKTRACK → EMIT_TOKENS` | `READ_HEADER → DECODE_TOKENS → FLUSH` | 3 |
| LZSS | `COLLECT_INPUT → EMIT_TOKENS → DONE` | 无状态机（SDA 包装） | 3 |
| DPFlate | `COLLECT_INPUT → BACKTRACK → BUILD_TREE → EMIT_TOKENS` | = Inflate | 4 |
| Deflate | `FIND_MATCHES → BUILD_TREE → FLUSH_TOKENS` | `READ_TREES → READ_BLOCK_HEADER → DECODE_TOKENS → COPY_MATCH → FLUSH` | 3 / 5 |
| Delta | `COLLECT → ENCODE → FLUSH` | `COLLECT → DECODE → FLUSH` | 3 |
| Brotli | `FIND_MATCHES → BUILD_TREE → FLUSH_TOKENS` | `READ_TREES → DECODE_TOKENS → COPY_MATCH` | 3 / 4 |
| Zstd | `COLLECT → COMPRESS → FLUSH` | `READ_HEADER → DECOMPRESS → FLUSH` | 3 |

---

## 五、AlgorithmFactory 使用关系

| AlgorithmID | 当前创建方式 | 建议 |
|-------------|-------------|------|
| Deflate | `std::make_unique<Deflate>(...)` | ✅ 状态机直连 |
| Inflate | `std::make_unique<Inflate>()` | ✅ 状态机直连 |
| DPFlate | `std::make_unique<DPFlate>(...)` | ✅ 状态机直连 |
| LZDP | `std::make_unique<LZDP_Streaming>(...)` | ✅ 状态机直连 |
| LZDPDecompress | `std::make_unique<LZDPDecompress_Streaming>(...)` | ✅ 状态机直连 |
| DeltaEncode/Decode | `std::make_unique<DeltaEncode/Decode>()` | ✅ 状态机直连 |
| **LZSS** | `SCA(LZSS::compress, ...)` | ❌ 应改用 `LZSS_Streaming` |
| **LZSS_NoFlag** | `SCA(LZSS::compress, ...)` | ❌ 应改用 `LZSS_Streaming` |
| **LZSSDecompress** | `SDA(LZSS::decompress, ...)` | ❌ 应改用状态机 |
| **Brotli** | `SCA(BrotliCompress::process, ...)` | ❌ 应改用状态机直连 |
| **BrotliDecompress** | `SDA(BrotliDecompress::process, ...)` | ❌ 应改用状态机直连 |
| **Zstd** | `SCA(ZstdCompress::compress, ...)` | ❌ 应改用状态机直连 |
| **ZstdDecompress** | `SDA(ZstdDecompress::decompress, ...)` | ❌ 应改用状态机直连 |
