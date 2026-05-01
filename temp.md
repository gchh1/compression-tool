现在我对整个系统有了完整理解。下面以 **压缩 `resources/level2_medium/` 网页文件夹** 为例，从用户操作开始，追踪每一层的数据流和状态变化。

---

## 场景设定

```
resources/level2_medium/
├── index.html      (12 KB,  text/html)
├── style.css       (8 KB,   text/css)
├── script.js       (15 KB,  text/javascript)
└── images/
    └── logo.png    (45 KB,  image/png)
```

目标：将这 4 个文件打包压缩成一个 `.pack` 文件，其中 `.png` 图片先用 Delta 预处理再 Deflate 压缩，文本文件直接 Deflate。

---

## 第一层：Python GUI —— 文件扫描与策略决策

### 1.1 用户操作 → 文件入表

用户将 `level2_medium/` 文件夹拖拽到 `MainWindow`。Qt 触发 `dropEvent` → `_add_paths` → 遍历目录：

```
_add_paths(["resources/level2_medium/"])
  ├── os.listdir → 发现 4 个文件
  ├── _add_file_row("index.html") → 插入表行 0
  ├── _add_file_row("style.css")  → 插入表行 1
  ├── _add_file_row("script.js")  → 插入表行 2
  └── _add_file_row("logo.png")   → 插入表行 3
```

`QTableWidget` 现在有 4 行：

| row | 文件名 | 大小 | 类型 | 状态 |
|-----|--------|------|------|------|
| 0 | index.html | 12.0 KB | 网页 | 等待中 |
| 1 | style.css | 8.0 KB | 样式 | 等待中 |
| 2 | script.js | 15.0 KB | 脚本 | 等待中 |
| 3 | logo.png | 45.0 KB | 图片 | 等待中 |

### 1.2 用户点击「开始压缩」

`_on_compress()` → 创建 `CompressionWorker` 线程，传入 `[(0, "index.html"), (1, "style.css"), (2, "script.js"), (3, "logo.png")]`。

**目前 GUI 的实际行为**（因 pybind 未接入，走 zlib fallback）：

```python
# CompressionWorker.run()
for row_idx, file_path in self._file_list:
    data = open(file_path, 'rb').read()  # 整个文件读入内存
    if engine.available:                  # False → 走 else
        result = engine.compress(data, AlgorithmType.DEFLATE)
    else:
        compressed = zlib.compress(data)  # ← Python stdlib
```

这是**单文件非流式**的简易路径。下面重点描述**设计目标中的 C++ 完整管路**——即 PackWriter 打包压缩流程。

---

## 第二层：Archiver 层 —— PackWriter 启动打包

假设 Python 通过 pybind 调用 C++ 端：

```python
# 理想的 pybind 调用
writer = PackWriter()
for filepath in file_list:
    algo = DecisionEngine.decide(filepath)  # 策略引擎决定算法
    writer.beginFile(filepath, comp_algo=Deflate, preproc_algo=DeltaEncode or None)
    for chunk in read_file_streaming(filepath, 64KB):
        writer.pushFileData(chunk)
        if writer.hasOutput():
            send_to_disk(writer.pullOutput())
    writer.endFile()
writer.finish()
```

### 2.1 压缩 `index.html`（纯文本，无预处理）

**调用 `writer.beginFile("index.html", comp_algo=Deflate, preproc_algo=None)`**

此时 `PackWriter` 成员状态：

```
entry_header_:
  filepath         = "index.html"
  preproc_algo_id  = None
  comp_algo_id     = Deflate
  original_size    = 0
  compressed_size  = 0
  data_offset      = 0

output_buffer_ = [0x0E, 0x00, 'i','n','d','e','x','.','h','t','m','l', 0x00, 0x02, 0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
                  └── name_len=14 ──┘└────── "index.html" ─────────┘└─pre=0 ─┘└─comp=2 ─┘└── original_size=0 ───────────────┘└── compressed_size=0 ─────────────┘
                                                                      ↑ header_offset_ 指向此处 (compressed_size 预留位)

pipeline_:
  first_  → StreamProcessor(DeltaEncode or nullptr)  ← 因 preproc=None，first_ 持有的 algo 为 nullptr，但 StreamProcessor 构造时直接用了 std::move
```

等一下，这里有个实现细节需要澄清。看 `Pipeline` 构造函数：

```cpp
Pipeline::Pipeline(std::unique_ptr<IAlgorithm> first,
                   std::unique_ptr<IAlgorithm> second)
    : first_(std::make_unique<StreamProcessor>(std::move(first))) { ... }
```

当 `preproc=None` 时，`createAlgorithm(None)` 返回 `nullptr`。然后 `std::make_unique<StreamProcessor>(nullptr)` 会把 `nullptr` 传给 `StreamProcessor` 构造函数 —— 这是**未定义行为**，因为 `StreamProcessor` 内部直接使用 `algo_` 而不判空。

**这是当前代码的一个 bug**。Pipeline 应该区分「没有 preprocessor」和「有 preprocessor」两种情况。正确的设计是 Pipeline 只持有一个 `StreamProcessor` 当没有第二段时。

但为了完整描述流程，我们假定这个 bug 被修复（Pipeline 在 second=nullptr 时不创建对应的 StreamProcessor，push 直接到 first_，pull 直接从 first_）。

继续。Pipeline 状态（修复后）：

```
pipeline_:
  first_  → StreamProcessor(Deflate)  ← comp 算法
  second_ → 不存在                     ← 无预处理，单阶段管道
  finished_ = false

first_ 成员:
  algo_     → Deflate 对象
  in_buffer_ → RingBuffer(capacity=65536, head=0, size=0)
  out_buffer_ → vector<uint8_t>(65536, 0)
  out_pos_ = 0
  finished_ = false
```

**Deflate 对象初始状态：**

```
window_           → vector<uint8_t>(65536, 0)    // 64KB 双倍窗口
head_             → vector<uint16_t>(32768, 0xFFFF) // 哈希表全部 NULL
prev_             → vector<uint16_t>(32768, 0xFFFF) // 链表全部 NULL
cursor_           = 0
lookahead_        = 0
token_buffer_     → empty
token_flush_idx_  = 0
huffman_tree_     → nullptr
dist_tree_        → nullptr
dictionary_       → empty
dist_dictionary_  → empty
deflate_state_    = FIND_MATCHES
```

BitReader/Writer（继承自 AlgorithmBase）：
```
reader_:
  read_       → span(nullptr, 0)  // 空，等待 changeSource 绑定
  byte_pos_   = 0
  buffer_     = 0
  buffer_idx_ = 0

writer_:
  write_      → span(nullptr, 0)  // 空，等待 changeSource 绑定
  byte_pos_   = 0
  buffer_     = 0
  buffer_idx_ = 0
```

### 2.2 喂第一块数据：`pushFileData(chunk_0)`

假设文件以 64KB 块读入。`index.html` 只有 12KB，所以一次 push 就喂完整个文件。

```cpp
PackWriter::pushFileData(chunk_0)  // chunk_0 = <index.html 全部 12KB>
```

**步骤 1**: `entry_header_.original_size += 12288` → 现在是 12288

**步骤 2**: `pipeline_->push(chunk_0, is_last=false)`

```
Pipeline::push(chunk_0, false)
  → first_->push(chunk_0, false)    // 只有单阶段，直接推给 StreamProcessor(Deflate)
```

**步骤 3**: `StreamProcessor::push(chunk_0, false)`

```cpp
void StreamProcessor::push(data, is_last) {
    in_buffer_.append(data);     // ← RingBuffer 尾部追加 12KB
    processChunks(is_last);      // ← 立即驱动算法消费
}
```

`RingBuffer` 状态变化：

```
append 前: ring_(cap=65536), head=0, size=0
append 后: ring_(cap=65536), head=0, size=12288
           ring_[0..12287] = <index.html 的字节>
```

`processChunks(false)` 被调用。这是 **算法驱动的核心循环**：

```cpp
void StreamProcessor::processChunks(bool is_last) {
    while (!in_buffer_.empty() || (is_last && !finished_)) {
        auto span_in  = in_buffer_.readSpan();         // 环形缓冲区 → 连续 span
        auto span_out = span<uint8_t>(out_buffer_.data() + out_pos_,
                                      out_buffer_.size() - out_pos_); // 可用输出空间

        auto status = algo_->process(span_in, span_out, final_flag);
        //                            ↑       ↑
        //                         读窗口   写窗口

        in_buffer_.consume(status.bytes_consumed);     // 消费已处理的输入
        out_pos_ += status.bytes_produced;             // 推进输出游标

        if (status.done)       { finished_ = true; break; }
        if (status.need_input)  { break; }  // 输入不够，等下一波 push
        if (status.need_output) { break; }  // 输出满了，等外部 pull+consume
    }
}
```

每次循环，`in_buffer_.readSpan()` 将环形缓冲区 compact 为 `{ring_.data() + 0, 12288}` 的连续 span。

`algo_->process(span_in, span_out, false)` 进入 `AlgorithmBase::process`：

```cpp
AlgorithmStatus AlgorithmBase::process(span<const uint8_t> read,
                                        span<uint8_t> write, bool is_last) {
    reader_.changeSource(read);   // ← BitReader 绑定到 span_in（in_buffer_ 的窗口）
    writer_.changeSource(write);  // ← BitWriter 绑定到 span_out（out_buffer_ 的空闲区域）

    AlgorithmStatus status;        // 初始全零
    handle(status, is_last);       // ← 进入 Deflate::handle

    status.bytes_consumed = reader_.getByteRead();    // BitReader 实际读了多少字节
    status.bytes_produced = writer_.getBytesWritten(); // BitWriter 实际写了多少字节
    return status;
}
```

这里的关键设计：**AlgorithmBase 不接触外部内存，只看 BitReader/BitWriter**。BitReader/BitWriter 是零拷贝的 span 包装器 —— 只保存指针，不拷贝数据。

**BitReader 状态**（`changeSource` 后）：

```
read_       → span{in_buffer_ 中 ring_.data() + 0, 12288}
byte_pos_   = 0
buffer_     → fillBuffer() 预加载 64-bit
buffer_idx_ → 32~64 之间的值
```

`fillBuffer()` 的实现非常激进 —— 以 4 字节（32-bit）为单位批量吸入 64-bit 寄存器：
```cpp
// 能吞一个字（4字节）就吞
while (buffer_idx_ <= 32 && (read_.size() - byte_pos_) >= 4) {
    uint32_t next_word;
    std::memcpy(&next_word, read_.data() + byte_pos_, 4);
    buffer_ |= (static_cast<uint64_t>(next_word) << buffer_idx_);
    buffer_idx_ += 32;
    byte_pos_ += 4;
}
// 不够 4 字节时逐字节吞
while (buffer_idx_ <= 56 && byte_pos_ < read_.size()) {
    buffer_ |= (static_cast<uint64_t>(read_[byte_pos_++]) << buffer_idx_);
    buffer_idx_ += 8;
}
```

**BitWriter 状态**（`changeSource` 后）：

```
write_      → span{out_buffer_.data() + 0, 65536}  ← 整个输出缓冲区空闲
byte_pos_   = 0
buffer_     = 0
buffer_idx_ = 0
```

**Deflate::handle** 被调用，进入**状态机主循环**：

```cpp
void Deflate::handle(AlgorithmStatus& status, bool is_last_chunk) {
    while (true) {
        (this->*kStateHandlers[static_cast<size_t>(deflate_state_)])(
            status, is_last_chunk);
        // kStateHandlers = {&handleFindMatches, &handleBuildTree, &handleFlushTokens}
        if (status.need_input || status.need_output || status.done) return;
    }
}
```

---

## 第三层：Algorithm 层 —— Deflate 状态机详解

### 状态 1: FIND_MATCHES —— LZ77 滑动窗口匹配

`deflate_state_ = FIND_MATCHES`，所以跳入 `handleFindMatches(status, false)`。

**Step 1: fillWindow() —— 从 BitReader 读数据到滑动窗口**

```cpp
void Deflate::fillWindow() {
    while (cursor_ >= WINDOW_SIZE - MAX_MATCH) {  // cursor(0) >= 65536-258? → false，跳过
        ...
    }
    // 但 lookahead_ = 0 且 reader_.getRemainSize() > 0
    // 所以进入主逻辑：
    size_t space_left = WINDOW_SIZE - (cursor_ + lookahead_);  // 65536 - 0 = 65536
    size_t remain     = reader_.getRemainSize();               // 12288
    size_t to_copy    = min(65536, 12288) = 12288;
    reader_.readBytes(window_.data() + 0, 12288);              // 直接 memcpy 到 window_
    lookahead_ += 12288;  // = 12288
}
```

`BitReader::readBytes` 先把 buffer_ 中缓存的 bit 级数据吐出，然后直接从底层 span 做 memcpy（跳过寄存器，极速路径）。

**Deflate 状态变化**：

```
window_[0..12287] = <html 字节>
cursor_    = 0
lookahead_ = 12288
```

**Step 2: 哈希链匹配循环** —— 对当前位置 `cursor_=0` 的 3 字节做哈希：

```cpp
uint16_t hash_val = getHash(0);  // (w[0]<<10 ^ w[1]<<5 ^ w[2]) & 32767
uint16_t match_pos = head_[hash_val];  // = NULL_PTR（第一次见这个哈希）

prev_[0 & 32767] = NULL_PTR;   // 记录链
head_[hash_val]  = 0;          // 更新链表头
```

因为是第一个位置，没有匹配。`match_length=0 < MIN_MATCH(3)`，输出一个**字面量 token**：

```cpp
Token t;
t.is_literal = true;
t.code       = window_[0];  // '<' 的 ASCII 值 0x3C
token_buffer_.push_back(t);
cursor_++;
lookahead_--;  // = 12287
```

**此时 token_buffer_.size() = 1**，远小于 `MAX_BLOCK_TOKENS(16384)`，状态不变。

**循环继续**：`handle()` 的外层 while(true) 再次调用 `handleFindMatches`。

`cursor_=1`, `lookahead_=12287`。不需要 fillWindow（空间充足）。计算 `hash_val = getHash(1)`，查看 `head_[hash_val]`…… 再次命中 NULL_PTR，再输出一个字面量 token。

如此反复。`handle()` 的 while(true) 循环会不断调用 `handleFindMatches`，每轮处理一个字符（产生一个 token），直到某一条件触发退出。

**什么时候退出？** 当 `token_buffer_.size() >= MAX_BLOCK_TOKENS(16384)` 时，转换到 `BUILD_TREE` 状态。对于 12KB 的文件，大约产生 ~12K 个 token，不到阈值。

但当输入数据消费完后：

```cpp
if (lookahead_ == 0) {
    if (!is_last_chunk) {
        status.need_input = true;  // ← 退出，等待下一波 push
        return;
    }
}
```

`is_last_chunk=false`（我们还不知道这是最后一块），所以设置 `status.need_input = true`，退出 `handle()`。

**回到 `StreamProcessor::processChunks`**：

```cpp
status.bytes_consumed = reader_.getByteRead();  // = 12288（全部消费）
status.bytes_produced = 0;                       // = 0（还没 flush token）
// status.need_input = true → 退出 while 循环
```

> 注意：FIND_MATCHES 阶段只消费输入、不产生输出。输出在 FLUSH_TOKENS 阶段才产生。

此时 `in_buffer_.consume(12288)` 清空了 RingBuffer。

`processChunks` 因 `need_input` 退出，但 `out_pos_=0`，`Pipeline::pull()` 返回空。所以 `PackWriter::drainOutput()` 也是空操作。

**这就是流式设计的精髓**：12KB 的 HTML 输入已被全部消费并转换为 12K 个 LZ77 token 存于 `token_buffer_`，但暂未输出任何压缩字节。内存占用：`token_buffer_` ≈ 12K × sizeof(Token) = 12K × ~16 bytes ≈ 192KB。

### 但是 —— 当前代码有个设计问题

正常情况下，`pushFileData` 调用后外部应该接着调用 `endFile()`。但让我们先看 `endFile` 的流程。

### 2.3 `writer.endFile()` —— 触发 BUILD_TREE 和 FLUSH_TOKENS

`endFile()` 调用 `pipeline_->finish()`：

```cpp
void Pipeline::finish() {
    first_->finish();  // → StreamProcessor::finish()
    drainInternal();   // 无 second_，空操作
    finished_ = true;
}
```

`StreamProcessor::finish()` 调用 `processChunks(true)`（is_last=true）：

现在 `in_buffer_` 是空的，但 `is_last=true` 并且 `finished_=false`，所以进入循环。

`algo_->process(span_in={}, span_out=out_buffer 全空闲, final_flag=true)`。

`AlgorithmBase::process` 将空输入绑定到 reader_，全空闲输出绑定到 writer_。

回到 `Deflate::handle`，再次进入 `handleFindMatches`：

```cpp
// lookahead_ = 0, reader_.getRemainSize() = 0
// fillWindow: to_copy = 0, break
// lookahead_ == 0 && is_last_chunk == true
→ deflate_state_ = BUILD_TREE;  // 状态转换
→ return;
```

handle() 再次循环，这次走 `handleBuildTree`。

### 状态 2: BUILD_TREE —— 构建 Huffman 树

```cpp
void Deflate::handleBuildTree(AlgorithmStatus& status, bool is_last) {
    // 1. 统计数据
    vector<uint32_t> freq_map(288, 0);   // Deflate 字母表
    vector<uint32_t> dist_freq(30, 0);   // 距离字母表
    for (const auto& token : token_buffer_) {
        freq_map[token.code]++;           // 字面量或长度码
        if (!token.is_literal)
            dist_freq[token.dist_code]++; // 仅匹配 token 有距离码
    }
    freq_map[256] = 1;                    // EOF 符号

    // 2. 构建 Huffman 树
    huffman_tree_ = make_unique<HuffmanTree>(freq_map, 288, 15);
    dist_tree_    = make_unique<HuffmanTree>(dist_freq, 30, 5);

    // 3. 生成规范编码字典
    dictionary_     = huffman_tree_->buildDictionary();
    dist_dictionary_ = dist_tree_->buildDictionary();

    // 4. 序列化树头到 BitWriter
    if (writer_.ensureSpace(huffman_tree_->getTreeSize() + dist_tree_->getTreeSize())) {
        huffman_tree_->serializeTree(writer_);   // 写入树结构（长度码 + 编码）
        dist_tree_->serializeTree(writer_);       // 写入距离树结构
        token_flush_idx_ = 0;
        deflate_state_   = FLUSH_TOKENS;          // 状态转换
    }
}
```

**BitWriter 变化**：writer_ 的 `byte_pos_` 向前推进（写入了 Huffman 树序列化数据），`out_pos_` 还没变（等回到 StreamProcessor 才更新）。

### 状态 3: FLUSH_TOKENS —— 输出压缩比特流

handle() 再次循环，进入 `handleFlushTokens`：

```cpp
void Deflate::handleFlushTokens(AlgorithmStatus& status, bool) {
    while (token_flush_idx_ < token_buffer_.size()) {
        // 确保输出缓冲区有空间（至少 6 bit）
        if (!writer_.ensureSpace(6)) {
            status.need_output = true;  // ← 输出缓冲区满了！
            return;
        }

        const auto& token = token_buffer_[token_flush_idx_];

        // 写入主编码（Huffman 可变长前缀码）
        const auto& main_code = dictionary_[token.code];
        writer_.writeBits(main_code.code, main_code.length);

        if (!token.is_literal) {
            // 匹配 token: 写入额外长度位 + 距离码 + 额外距离位
            if (token.length_extra_bits > 0)
                writer_.writeBits(token.length_extra_val, token.length_extra_bits);
            const auto& dist_code = dist_dictionary_[token.dist_code];
            writer_.writeBits(dist_code.code, dist_code.length);
            if (token.dist_extra_bits > 0)
                writer_.writeBits(token.dist_extra_val, token.dist_extra_bits);
        }
        token_flush_idx_++;
    }

    // 全部 token 刷新完毕
    token_buffer_.clear();

    if (is_last_chunk && lookahead_ == 0) {
        // 写入 EOF 符号 (256)
        const auto& eof_code = dictionary_[256];
        writer_.writeBits(eof_code.code, eof_code.length);
        writer_.flush();     // 冲刷寄存器中剩余的不足 8-bit 的数据
        status.done = true;  // ← 算法完成
        return;
    }

    deflate_state_ = FIND_MATCHES;  // 继续下一块
}
```

**BitWriter::writeBits 内部**：

```cpp
void BitWriter::writeBits(uint64_t value, uint8_t count) {
    value &= (1ULL << count) - 1;
    buffer_ |= (value << buffer_idx_);  // LSB-first：新位拼在 buffer 左侧
    buffer_idx_ += count;

    // 当 buffer 攒够 32 位，批量写入 4 字节
    while (buffer_idx_ >= 32) {
        uint32_t out_word = static_cast<uint32_t>(buffer_);
        std::memcpy(write_.data() + byte_pos_, &out_word, 4);
        byte_pos_ += 4;
        buffer_ >>= 32;
        buffer_idx_ -= 32;
    }
}
```

这是**批量写入**设计：单个 Huffman 码可能只有 3-4 bit，但攒到 32 bit 才一次性 memcpy 到输出 span。避免逐字节写入的开销。

**写到 output 满时**：`ensureSpace(6) == false` → `status.need_output = true` → 退出 `handleFlushTokens` → 退出 `handle()`。

然后 `AlgorithmBase::process` 收集统计：
```cpp
status.bytes_consumed = reader_.getByteRead();   // 0（is_last 时无新输入）
status.bytes_produced = writer_.getBytesWritten(); // 例如 4500
status.need_output = true;                        // 传递给上层
```

回到 `StreamProcessor::processChunks`：
```cpp
out_pos_ += status.bytes_produced;  // out_pos_ = 4500
// status.need_output = true → break
```

回到 `StreamProcessor::finish()` → 返回 `{out_buffer_.data(), 4500}`。

但 `Pipeline::finish()` 没有接收这个返回值！然后 `PackWriter::endFile()` 调用 `drainOutput()`：

```cpp
void PackWriter::drainOutput() {
    while (true) {
        auto out = pipeline_->pull();   // → StreamProcessor::pull() → {out_buf, 4500}
        if (out.empty()) break;         // 不空！
        output_buffer_.insert(output_buffer_.end(), out.begin(), out.end());
        // output_buffer_ 现在包含了 [Header | 压缩数据 4500 字节]
        current_compressed_size_ += out.size();  // = 4500
        pipeline_->consume(out.size());           // → StreamProcessor::consume(4500)
    }
}
```

`StreamProcessor::consume(4500)`:
```cpp
void StreamProcessor::consume(size_t n) {
    memmove(out_buffer_.data(), out_buffer_.data() + n, out_pos_ - n);
    out_pos_ -= n;  // out_pos_ → 0
}
```

但 finish() 后 `processChunks` 不会再次被调用（`finished_=true`），consume 的 memmove 只是为了保持缓冲区一致性，实际不重要。

然后 `processChunks(true)` 再次被调用（finish 的循环），因为：
- `in_buffer_` 空了
- `is_last=true`
- `finished_=false`（还没 done）

再次进入算法。`handleFlushTokens` 继续从上次的 `token_flush_idx_` 刷新剩余 token，直到全部刷完并写 EOF，`status.done=true`。

然后 `finished_=true`，`processChunks` 退出。`finish()` 返回最终数据。

### 2.4 `writer.endFile()` —— 回填 compressed_size

```cpp
auto* dest = output_buffer_.data() + header_offset_;
// header_offset_ 指向 EntryHeader 中 compressed_size 字段的偏移位置
std::memcpy(dest, &current_compressed_size_, sizeof(uint64_t));
// 将实际压缩大小（如 4500）写回 header
```

此时 `output_buffer_` 的最终布局：

```
┌──────────────────┬──────────────────────────────────┐
│  EntryHeader     │  压缩后的 Deflate 比特流          │
│  (28 bytes)      │  (4500 bytes)                    │
│                  │                                  │
│  包含:            │  [HuffmanTree 序列化]             │
│  - 文件名        │  [LZ77 tokens 编码]               │
│  - 算法 ID       │  [EOF 符号]                      │
│  - 原始大小 12KB │                                   │
│  - 压缩大小 4.5KB│                                   │
└──────────────────┴──────────────────────────────────┘
```

`file_open_ = false`，`pipeline_` 被 reset（Deflate 对象析构，释放 64KB 窗口 + 哈希表内存）。

---

## 压缩 `logo.png`（图片 + Delta 预处理）

再次调用 `beginFile("logo.png", comp_algo=Deflate, preproc_algo=DeltaEncode)`。

这次 `preproc_algo=DeltaEncode`，所以 Pipeline 有两阶段：

```
pipeline_:
  first_  → StreamProcessor(DeltaEncode)  ← preprocessor
  second_ → StreamProcessor(Deflate)       ← compressor
  finished_ = false
```

### pushFileData 的链路

```cpp
PackWriter::pushFileData(chunk)
  → pipeline_->push(chunk)
    → first_->push(chunk)        // → StreamProcessor(DeltaEncode)
    → drainInternal()            // → drain(*first_, *second_)
      → 循环: first_->pull() → second_->push() → first_->consume()
    // 此时 second_(Deflate) 的 StreamProcessor 已经收到 DeltaEncode 的输出
    // Deflate 的 FIND_MATCHES 可能已经产生 token 或 need_input
  → drainOutput()
    → pipeline_->pull()          // → second_->pull()（Deflate 的输出）
    → output_buffer_ 追加数据
```

**数据流向**：

```
原始 PNG 字节 chunk (64KB)
  → Pipeline::push
    → StreamProcessor(DeltaEncode)::push
      → RingBuffer(in)::append
      → DeltaEncode::process → 逐像素差分 → BitWriter
      → StreamProcessor::pull() → Delta 编码后的数据
    → drain(DeltaEncode → Deflate)
      → StreamProcessor(Deflate)::push
        → RingBuffer(in)::append
        → Deflate::handle → FIND_MATCHES → 哈希链匹配 → token_buffer_
        → (可能 BUILD_TREE → FLUSH_TOKENS → BitWriter 输出)
    → Pipeline::pull() → Deflate 的 StreamProcessor::pull()
  → PackWriter::drainOutput
    → output_buffer_ 追加压缩数据
```

**DeltaEncode 内部**（简化）：对 4096 字节的 chunk 做逐像素差分，`pixel[i] = pixel[i] - pixel[i-1]`（带质量参数 shift），通过 BitWriter 写入。输出是差分后的字节流，比原始像素熵更低，让后面的 Deflate 更容易找到匹配。

### endFile 的链路

```cpp
PackWriter::endFile()
  → pipeline_->finish()
    → first_->finish()     // DeltaEncode 收尾
    → drainInternal()      // 把 Delta 最后的数据排给 Deflate
    → second_->finish()    // Deflate 收尾（BUILD_TREE → FLUSH_TOKENS → EOF）
  → drainOutput()          // 拉取最终压缩数据
  → 回填 compressed_size
```

---

## 收尾：`writer.finish()`

所有文件处理完后：

```cpp
void PackWriter::finish() {
    if (finished_) return;
    if (file_open_) closeCurrentFile();  // 保险：关掉未关闭的文件
    finished_ = true;
}
```

最终的 `output_buffer_` 布局（4 个文件打包）：

```
┌─────────────┬────────────────┬──────────────┬─────────────────┬──────────────┬────────────────┬────────────┬───────────────┐
│ EntryHeader │ compressed     │ EntryHeader  │ compressed      │ EntryHeader  │ compressed     │EntryHeader │ compressed    │
│ index.html  │ data (4.5KB)   │ style.css    │ data (3.2KB)    │ script.js    │ data (6.1KB)   │ logo.png   │ data (18KB)   │
│ orig=12KB   │                │ orig=8KB     │                 │ orig=15KB    │                │ orig=45KB  │               │
│ comp=Deflate│                │ comp=Deflate │                 │ comp=Deflate │                │comp=Deflate│               │
│ pre=None    │                │ pre=None     │                 │ pre=None     │                │pre=DeltaEnc│               │
└─────────────┴────────────────┴──────────────┴─────────────────┴──────────────┴────────────────┴────────────┴───────────────┘
```

---

## 解压：PackReader 的逆过程

```cpp
auto reader = make_unique<FileDataReader>(packed_file_path);
PackReader pack_reader(std::move(reader));
// 构造时自动调用 buildIndex() —— 遍历所有 EntryHeader

// 获取文件列表
auto& entries = pack_reader.getEntries();
// entries[0] = {filepath="index.html", data_offset=28, compressed_size=4500, ...}
// entries[1] = {filepath="style.css", data_offset=4528, compressed_size=3200, ...}

// 解压 index.html
auto pipeline = pack_reader.extractStream(0);
// 内部: Pipeline(Inflate, nullptr) → push 压缩数据 → finish → 返回

// 拉取解压结果
while (true) {
    auto out = pipeline->pull();
    if (out.empty()) break;
    write_to_disk(out);
    pipeline->consume(out.size());
}
```

`extractStream(0)` 对 `index.html`：
- `preproc_algo_id = None` → `getPostpressorID(None) = None` → `postproc_algo = nullptr`
- `comp_algo_id = Deflate` → `getDecompressorID(Deflate) = Inflate` → `decomp_algo = Inflate`
- `Pipeline(Inflate, nullptr)` → 单阶段管道

`extractStream(3)` 对 `logo.png`：
- `preproc_algo_id = DeltaEncode` → `getPostpressorID(DeltaEncode) = DeltaDecode` → `postproc_algo = DeltaDecode`
- `comp_algo_id = Deflate` → `getDecompressorID(Deflate) = Inflate`
- `Pipeline(Inflate, DeltaDecode)` → **两阶段管道：先解压再逆差分**

数据流：

```
packed_file (offset=meta.data_offset)
  → IDataReader::read
  → Pipeline::push
    → StreamProcessor(Inflate)::push
      → Inflate::handle → 解码 Huffman 树 → 解码 token → LZ77 窗口重建
    → drain(Inflate → DeltaDecode)
      → StreamProcessor(DeltaDecode)::push
        → DeltaDecode::handle → pixel[i] = pixel[i] + pixel[i-1]
  → Pipeline::pull → 解压后的原始 PNG 像素 → ImageParser::restore → PNG 文件
```

---

## 内存布局总结

整个压缩流程中，最大的内存消耗在 **Deflate 的 64KB 滑动窗口 + 192KB token buffer** 和 **output_buffer_ 的累积**：

| 组件 | 内存占用 | 生命周期 | 存储位置 |
|------|----------|----------|----------|
| `output_buffer_` (PackWriter) | 动态增长，最终 ≈ 所有压缩数据之和 | 整个打包过程 | **内存**（最终应写入磁盘） |
| `window_` (Deflate) | 64KB 固定 | 单个文件压缩期间 | 内存 |
| `head_[]` (Deflate 哈希表) | 64KB (32768 × uint16_t) | 单个文件压缩期间 | 内存 |
| `prev_[]` (Deflate 链) | 64KB | 单个文件压缩期间 | 内存 |
| `token_buffer_` (Deflate) | ~192KB (16K tokens) | 单个 block 期间 | 内存 |
| `in_buffer_` (StreamProcessor) | 动态，初始 64KB，可扩容 | 持续 | 内存 |
| `out_buffer_` (StreamProcessor) | 固定 64KB | 持续 | 内存 |
| `ring_` (RingBuffer) | 初始 64KB，按需翻倍 | 持续 | 内存 |
| `buffer_` (BitWriter/BitReader) | 8 字节（64-bit 寄存器） | 持续 | CPU 寄存器/栈 |

**关键设计决策**：

1. **Deflate 的 64KB 窗口是双倍缓冲**（`WINDOW_SIZE = 2 × SLIDE_SIZE`），避免每次滑动时 memmove 整个窗口 —— 只有当前游标越过 32KB 中线时才 slide 32KB。
2. **BitWriter 的批量写入**（攒够 32 bit 才 memcpy）减少了字节级写入的开销，利用现代 CPU 的 64-bit 寄存器做位操作。
3. **RingBuffer 的 compact 策略**：只在 `readSpan()` 时如果数据跨环末尾才 compact，append 时用两段 memcpy 处理跨环写入，避免频繁 memmove。
4. **当前 output_buffer_ 全在内存**，对于大文件夹（几百 MB）需要改造为**流式写出**——`pullOutput/consumeOutput` 接口已经为此预留：外部调用 pullOutput 获取数据写磁盘，consumeOutput 释放已写出的缓冲区前端。

---
对，output 也从同一个池里拿 chunk。至于读写不对称，当前实现**没处理好**——RingBuffer 和 `output_buffer_` 都会无限扩容。用共享内存池恰好能天然解决。

## 一池共享：读、压缩中间态、写都用同一个池

```
              ┌─────────────── MemoryPool (固定 N 个 chunk) ───────────────┐
              │                                                             │
  硬盘 ──read──→ [A] ──推入──→ StreamProcessor ──产出──→ [B] ──write──→ 硬盘
              │  ↑                            ↑              ↑              │
              │  └── B 写完释放，A 处理完释放 ──┘              │              │
              │                                                  │              │
              │  所有 chunk 都在池中循环，不 new/delete             │
              └─────────────────────────────────────────────────────────────┘
```

Pool 只提供固定个 chunk。谁需要 buffer 就从池拿；用完了引用计数归零，deleter 自动归还。池空了就阻塞，这就是背压。

## 读写不对称怎么处理

你说的情况：read 还没消费完，write 已经写满了——这恰好就是共享池自动处理的场景：

```
假设池 = 6 个 chunk

时刻 1: Reader 拿了 3 个 chunk 填满数据，池剩 3 个
         [A:已读] [B:已读] [C:已读] [空闲] [空闲] [空闲]

时刻 2: 压缩拿 A 处理，产出放到从池里新拿的 D、E
         [A:压缩中] [B:等待] [C:等待] [D:压缩完] [E:压缩完] [空闲]

时刻 3: Writer 正在慢慢写 D，压缩产出又拿了最后一个 F
         此时池空了！
         [A:压缩中] [B:等待] [C:等待] [D:写入中] [E:待写入] [F:压缩完]

时刻 4: Reader 想读下一块 → pool.acquire() → 阻塞!
         压缩线程想产出 → pool.acquire() → 也阻塞!
         Writer 写完 D → D 归还池 → Writer 拿 E 继续写
         Reader 被唤醒，拿到刚释放的 chunk → 继续读盘
```

整个过程不需要任何显式的"读快慢"判断——**池空就是信号，谁拿不到谁等**。这就是优雅之处。

## 当前实现的问题

回看当前代码，读写不对称时有两个会无限膨胀的点：

```cpp
// RingBuffer::ensureCapacity —— 下游堵了就无脑扩容，没有上限
auto ensureCapacity(size_t total_size) -> void {
    if (total_size <= capacity_) return;
    size_t new_cap = capacity_ * 2;
    while (new_cap < total_size) new_cap *= 2;
    // ... 重新分配，内存持续增长
}

// PackWriter::drainOutput —— 输出积压在 output_buffer_，也只增不减
output_buffer_.insert(output_buffer_.end(), out.begin(), out.end());
```

压缩慢于读盘时，这两个 buffer 会悄悄吃光内存。

## 输入端的取舍

输入端稍微特殊：算法需要**连续**的 span（`process(std::span<const uint8_t>, ...)`），而池 chunk 天然是分块的。两种处理方式：

**方案 A：输入端保留 RingBuffer（接受拷贝 #1）**

读盘拿到的 pool chunk → 拷进 RingBuffer → 立即归还 chunk。RingBuffer 给算法提供连续视图。这保留了当前算法的接口不变，拷贝 #1 的代价换来简单。RingBuffer 只需设个上限，满了就停止从上游拉。

**方案 B：RingBuffer 存 DataChunk 引用（零拷贝但算法接口得改）**

RingBuffer 不存字节，存一个 `deque<DataChunk>`。`readSpan()` 时只能返回当前最前面 chunk 的 span（可能不是全部剩余数据）。算法一次只能看到一块，跨 chunk 边界时必须等下轮。改动较大，但零拷贝。

建议先走 **方案 A**——输入端保留 RingBuffer + 容量上限，拷贝 #1/#2 保留。输出端改池化方案，消除拷贝 #3/#4，收益最大而改动集中在输出路径。

## 池 + 流水线的最终形态

```
固定内存预算 = N × CHUNK_SIZE

   Reader 线程                Worker 线程              Writer 线程
   ──────────                ──────────               ──────────
   pool.acquire() → chunk    pipeline.push(chunk)     pipeline.pull() → DataChunk
   read(chunk)               compress()               write(chunk.view())
   DataChunk::adopt(chunk)   pool.acquire() → out     chunk 引用归零
        ↓                         ↓                        ↓
   shared_ptr 传入              产出 DataChunk        pool.release(out)
   流水线                        共享所有权             唤醒 Reader
```

内存用量被严格限制在池大小，快慢不对称由池的满/空自动调节。要不要我把整个 MemoryPool + DataChunk 的代码写出来？