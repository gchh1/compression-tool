# Bug Report

## Bug 1: calcBitWidth 位宽计算错误导致 DPFlate CRC 不匹配

### 状态
已修复

### 严重程度
严重 - 导致所有 DPFlate 压缩数据解压后 CRC 不匹配

### 发现时间
2026-05-14

### 文件位置
[SpillBitStream.hpp](../src/algorithm/include/SpillBitStream.hpp#L27-L34)

### 问题描述
`PackedDpLinkSpec::calcBitWidth` 函数在计算所需位宽时错误地减去了 1，导致当最大值为 2 的幂时（如 128、2048），计算出的位宽比实际需要的少 1 位。

```cpp
static auto calcBitWidth(size_t v) -> uint8_t {
    int bits = 0;
    if (v == 0) return 1;
    --v;  // BUG: 这行导致位宽计算错误
    while (v > 0) {
        ++bits;
        v >>= 1;
    }
    return static_cast<uint8_t>(bits == 0 ? 1 : bits);
}
```

### 影响范围
- `calcBitWidth(128)` 返回 7，但需要 8 位才能表示 0-128
- `calcBitWidth(2048)` 返回 11，但需要 12 位才能表示 0-2048
- 当 match length = 128 时，`writeBits(128, 7)` 将 128 截断为 0
- 当 offset = 2048 时，`writeBits(2048, 11)` 将 2048 截断为 0
- 所有使用 DPFlate 压缩的数据都可能受影响，尤其是包含长重复模式的数据

### 复现步骤
1. 创建重复模式数据（如 "REGRESS_LZDP_DPFLATE_" 重复 32 次）
2. 使用 DPFlate 压缩（lookahead_size=128, search_size=2048）
3. 使用 Inflate 解压
4. 比较原始数据和解压数据

### 修复
移除 `--v;` 行，使函数正确计算表示 0 到 v（含）所需的位宽。

### 验证
- test_roundtrip: ALL TESTS PASSED
- test_lzdp_dpflate_regression: ALL PASSED
- debug_token_trace: Match: YES
- debug_dpflate FLATE 模式: Match: YES

---

## Bug 2: 3HfMT 模式解压数据不完整

### 状态
已修复

### 严重程度
严重 - 3HfMT 模式解压后数据大小和内容都不正确

### 发现时间
2026-05-14

### 文件位置
[Inflate3HM.cpp](../src/algorithm/Inflate3HM.cpp#L165-L181)

### 问题描述
使用 3HfMT 模式压缩的数据解压后只有 32 字节（原始 672 字节），且 CRC 不匹配。压缩数据为 38 字节，表明压缩过程正确，但解压过程未能正确还原所有数据。

### 根因分析
该问题由两个独立 bug 共同导致：

**Bug 2a: COPY_MATCH 源位置计算错误**
[Inflate3HM.cpp](../src/algorithm/Inflate3HM.cpp#L175)
```cpp
// BUG: out_abs_ 在循环中被 appendDecodedByte 递增，
// 导致 out_abs_ - offset + i 双重累加偏移量
size_t src_pos = static_cast<size_t>((out_abs_ - offset + i) % kWindowSize);
```
当 offset=21, length=128 时：
- i=0: src_pos = (21-21+0) = 0 ✓
- i=1: src_pos = (22-21+1) = 2 ✗（应为 1）
- i=2: src_pos = (23-21+2) = 4 ✗（应为 2）

修复：保存起始 out_abs_ 值，使用固定值计算源位置。

**Bug 2b: offset_bits_3hm/length_bits_3hm 位宽计算错误**
[DPFlate.hpp](../src/algorithm/include/DPFlate.hpp#L53-L70)
```cpp
size_t offset_bits_3hm() const {
    size_t v = SEARCH_SIZE;
    --v;  // BUG: 导致 SEARCH_SIZE=2048 时返回 11 而非 12
    ...
}
size_t length_bits_3hm() const {
    size_t v = LOOKAHEAD_SIZE;
    --v;  // BUG: 导致 LOOKAHEAD_SIZE=128 时返回 7 而非 8
    ...
}
```
当 LOOKAHEAD_SIZE=128 时，length_bits=7，导致 length=128 在 multi-level 编码中被截断。

### 修复
1. 移除 offset_bits_3hm() 和 length_bits_3hm() 中的 `--v;` 行
2. 在 COPY_MATCH 循环中使用固定的 start_abs 替代变化的 out_abs_

### 验证
- debug_dpflate 3HfMT 模式: Match: YES
- Direct Inflate3HM: Match: YES
- test_3hm_all 全语料测试 (2026-05-15): 4/4 通过
  - pattern (672 bytes): CRC=0x626487e3 ✓
  - random (384 bytes): CRC=0x3f5b556e ✓
  - text (22300 bytes): CRC=0x4ca507bd ✓
  - binary_64k (65536 bytes): CRC=0x3a5db188 ✓
- test_algorithm_comparison --memory-only: 全部算法 CRC_OK

---

## Bug 5: HuffmanTree3HM 零频率符号导致 Huffman 编码长度为 0

### 状态
已修复

### 严重程度
严重 - 导致 3HfMT 压缩/解压崩溃

### 发现时间
2026-05-15

### 文件位置
[HuffmanTree3HM.cpp](../src/algorithm/HuffmanTree3HM.cpp)

### 问题描述
在构建 Huffman 树时，如果某个符号的频率为 0（即该符号在数据中从未出现），Huffman 编码算法会为其分配长度为 0 的编码。当后续尝试使用该编码进行 encode/decode 时，会导致未定义行为或崩溃。

### 根因分析
`HuffmanTree3HM::buildTrees` 直接使用传入的频率表构建 Huffman 树。对于 offset 和 length 符号空间（通常为 256 或 4096 个符号），大部分符号的频率为 0。Huffman 树构造函数对频率为 0 的符号分配了 0 长度的编码。

### 修复
在 `buildTrees` 中添加 `ensure_all_symbols` lambda，确保所有符号的最小频率为 1：

```cpp
auto ensure_all_symbols = [](std::vector<uint32_t> freq, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (freq[i] == 0) freq[i] = 1;
    }
    return freq;
};
```

### 验证
- test_3hm_all: 4/4 通过
- test_algorithm_comparison: 3HfMT 全部 CRC_OK

---

## Bug 6: 3HfMT 解压输出缓冲区未刷新导致数据丢失

### 状态
已修复

### 严重程度
严重 - 导致解压后数据为空

### 发现时间
2026-05-15

### 文件位置
[Inflate3HM.cpp](../src/algorithm/Inflate3HM.cpp)

### 问题描述
在 DECODE_RUN_LEN、DECODE_LITERALS、DECODE_MATCH_LEN 状态中，当 `is_last_chunk` 为 true 且 `reader_.ensureBits(1)` 返回 false 时，直接设置 `status.done = true` 返回，但此时 `output_buf_` 中可能还有未刷新的解码数据，导致这些数据丢失。

### 修复
在每个状态的 `is_last_chunk` 分支中，先检查 `output_buf_` 是否为空。如果不为空，切换到 `FLUSH_TO_WRITER` 状态先刷新缓冲区：

```cpp
if (is_last_chunk) {
    if (!output_buf_.empty()) {
        output_flush_pos_ = 0;
        decode_state_ = DecodeState::FLUSH_TO_WRITER;
        continue;
    }
    status.done = true;
    return;
}
```

### 验证
- test_3hm_all: 4/4 通过，解压后数据大小与原始一致

---

## Bug 7: DeflateCompressor::compress 单次调用导致压缩数据截断

### 状态
已修复

### 严重程度
严重 - 可能导致 Deflate 压缩数据不完整

### 发现时间
2026-05-15

### 文件位置
[DeflateCompressor.cpp](../src/core/DeflateCompressor.cpp#L83-L101)

### 问题描述
`DeflateCompressor::compress` 只调用一次 `deflate.process()`，但 Deflate 算法内部状态机可能在输出缓冲区满时返回 `need_output=true`，需要多次调用才能完成压缩。单次调用导致压缩数据被截断。

### 修复
添加 `compress_to_end` 模板函数，循环调用 `process()` 直到 `status.done` 为 true，与 `inflate_to_end` 对称：

```cpp
template <typename Algo>
auto compress_to_end(Algo& algo, const std::vector<uint8_t>& input,
                     std::vector<uint8_t>& out) -> algorithm::AlgorithmStatus {
    algo.reset();
    size_t in_off = 0;
    size_t out_pos = 0;
    out.resize(std::max(input.size() * 2 + 65536, size_t{4096}));
    algorithm::AlgorithmStatus st{};
    for (;;) {
        if (out_pos >= out.size()) {
            out.resize(std::max(out.size() * 2, out_pos + input.size() + 65536));
        }
        const auto in_span = std::span<const uint8_t>(
            input.data() + in_off, input.size() - in_off);
        const auto out_span =
            std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);
        st = algo.process(in_span, out_span, in_off + in_span.size() >= input.size());
        in_off += st.bytes_consumed;
        out_pos += st.bytes_produced;
        if (st.done) break;
        if (st.need_output && st.bytes_produced == 0) {
            out.resize(std::max(out.size() * 2, out_pos + input.size() + 65536));
            continue;
        }
        if (st.need_input && in_off >= input.size()) break;
    }
    out.resize(out_pos);
    return st;
}
```

### 验证
- test_regression_baseline: 18/18 通过
- test_algorithm_comparison: pattern/random/text 全部 CRC_OK

---

## Bug 8: Inflate 解压 binary_64k 随机数据失败

### 状态
待修复

### 严重程度
中等 - 仅影响特定随机数据模式的 Deflate 解压

### 发现时间
2026-05-15

### 文件位置
[Inflate.cpp](../src/algorithm/Inflate.cpp) 或 [DeflateCompressor.cpp](../src/core/DeflateCompressor.cpp) 中的 `inflate_to_end`

### 问题描述
使用 Deflate 算法压缩 65536 字节随机数据（seed=42）后，解压只产出 17725 字节（原始 65536），且从第 16386 字节开始数据错误。`inflate_to_end` 返回 `status.done=false`，错误信息为 "Inflate decompression incomplete"。

压缩数据大小 66978 字节（> 原始 65536，随机数据正常），压缩过程 `status.done=true`，说明压缩完整但解压失败。

### 影响范围
- 仅影响 binary_64k 随机数据（seed=42 的 mt19937 生成）
- pattern、random（384字节）、text 语料均正常
- test_algorithm_comparison 中的 binary_64k（seed=67890）因 LZDP 超时从未实际测试到 Deflate

### 临时措施
- test_regression_baseline 跳过 Deflate binary_64k 测试
- 其他语料的回归基线正常工作

---

## Bug 3: 流式模式文件权限错误

### 状态
待修复

### 严重程度
中等 - 影响流式压缩/解压功能

### 发现时间
2026-05-14

### 问题描述
`compressFile` 和 `decompressFile` 流式接口返回 "Cannot commit output: Permission denied" 错误。可能是临时文件或输出文件路径权限问题。

### 复现步骤
1. 调用 DPFlateCompressor::compressFile
2. 返回 success=0
3. 错误信息: "Cannot commit output: Permission denied"

---

## Bug 4: 编译系统依赖跟踪不完整

### 状态
待修复

### 严重程度
低 - 影响开发效率

### 发现时间
2026-05-14

### 文件位置
CMakeLists.txt

### 问题描述
修改头文件（如 SpillBitStream.hpp）后，CMake 构建系统未能正确检测到依赖变化，导致需要手动删除 .obj 文件才能触发重新编译。这是因为 CMake 的依赖跟踪没有正确包含所有头文件依赖。

### 影响
- 修改头文件后，`mingw32-make` 报告 "Nothing to be done"
- 需要手动 `rm *.obj` 才能触发重新编译
- 可能导致开发者误以为代码已更新，实际使用的是旧版本

### 临时解决方案
```powershell
rm <path>\*.obj
mingw32-make -j4
```
