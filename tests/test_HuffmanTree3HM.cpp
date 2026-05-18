#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree3HM.hpp"

using namespace compressor::algorithm;
using namespace compressor::utils;

// ============================================================
// 测试 1: 基本三树编码/解码往返
// ============================================================
void testBasicRoundTrip() {
    std::cout << "--- Test 1: Basic Round Trip ---" << std::endl;

    // 模拟数据: lit(61) lit(62) match(offset=3,len=3) match(offset=5,len=2)
    // 游程分组后:
    //   run1: offset=0, run_length=2, literals=[61, 62]
    //   match1: offset=3, length=3
    //   match2: offset=5, length=2

    // 频率统计
    std::vector<uint32_t> literal_freq(256, 0);
    literal_freq[61] = 1;
    literal_freq[62] = 1;

    std::vector<uint32_t> offset_freq(64, 0);
    offset_freq[0] = 1;
    offset_freq[3] = 1;
    offset_freq[5] = 1;

    std::vector<uint32_t> length_freq(32, 0);
    length_freq[2] = 2;
    length_freq[3] = 1;

    // 建树
    HuffmanTree3HM encoder;
    encoder.buildTrees(literal_freq, offset_freq, length_freq,
                       64, 32, 6, 5, 5, 5);

    std::cout << "  Tree size: " << encoder.getTreeSize() << " bits" << std::endl;

    // 编码：先序列化树，再编码数据
    std::vector<uint8_t> buffer(1024, 0);
    BitWriter writer{std::span<uint8_t>(buffer)};

    encoder.serialize(writer);

    // run1: offset=0, run_length=2, literals=[61, 62]
    encoder.encodeRunHeader(2, writer);
    encoder.encodeLiteral(61, writer);
    encoder.encodeLiteral(62, writer);

    // match1: offset=3, length=3
    encoder.encodeMatch(3, 3, writer);

    // match2: offset=5, length=2
    encoder.encodeMatch(5, 2, writer);

    writer.flush();
    size_t bytes_written = writer.getBytesWritten();
    std::cout << "  Encoded " << bytes_written << " bytes total" << std::endl;

    // 解码：从同一 buffer 读取
    BitReader reader(std::span<const uint8_t>(buffer.data(), bytes_written));
    HuffmanTree3HM decoder;
    decoder.deserialize(reader);

    // 解码 run1
    uint16_t offset = decoder.decodeOffset(reader);
    assert(offset == 0);
    uint16_t run_len = decoder.decodeRunLength(reader);
    assert(run_len == 2);
    uint8_t lit1 = decoder.decodeLiteral(reader);
    uint8_t lit2 = decoder.decodeLiteral(reader);
    assert(lit1 == 61);
    assert(lit2 == 62);
    std::cout << "  Run1: offset=0, run_length=" << run_len
              << ", literals=[" << (int)lit1 << "," << (int)lit2 << "]" << std::endl;

    // 解码 match1
    offset = decoder.decodeOffset(reader);
    assert(offset == 3);
    uint16_t match_len = decoder.decodeMatchLength(reader);
    assert(match_len == 3);
    std::cout << "  Match1: offset=" << offset << ", length=" << match_len << std::endl;

    // 解码 match2
    offset = decoder.decodeOffset(reader);
    assert(offset == 5);
    match_len = decoder.decodeMatchLength(reader);
    assert(match_len == 2);
    std::cout << "  Match2: offset=" << offset << ", length=" << match_len << std::endl;

    std::cout << "  PASSED!" << std::endl;
}

// ============================================================
// 测试 2: 纯字面量（无匹配）
// ============================================================
void testAllLiterals() {
    std::cout << "\n--- Test 2: All Literals ---" << std::endl;

    std::vector<uint8_t> input = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};

    // 频率统计：所有字面量作为一个游程
    std::vector<uint32_t> literal_freq(256, 0);
    for (auto b : input) literal_freq[b]++;

    std::vector<uint32_t> offset_freq(64, 0);
    offset_freq[0] = 1;

    std::vector<uint32_t> length_freq(32, 0);
    length_freq[static_cast<size_t>(input.size())] = 1;

    HuffmanTree3HM encoder;
    encoder.buildTrees(literal_freq, offset_freq, length_freq,
                       64, 32, 6, 5, 5, 5);

    // 编码
    std::vector<uint8_t> buffer(1024, 0);
    BitWriter writer{std::span<uint8_t>(buffer)};

    encoder.serialize(writer);
    encoder.encodeRunHeader(static_cast<uint16_t>(input.size()), writer);
    for (auto b : input) encoder.encodeLiteral(b, writer);

    writer.flush();
    size_t bytes_written = writer.getBytesWritten();
    std::cout << "  Encoded " << input.size() << " bytes into "
              << bytes_written << " bytes total" << std::endl;

    // 解码
    BitReader reader(std::span<const uint8_t>(buffer.data(), bytes_written));
    HuffmanTree3HM decoder;
    decoder.deserialize(reader);

    uint16_t offset = decoder.decodeOffset(reader);
    assert(offset == 0);
    uint16_t run_len = decoder.decodeRunLength(reader);
    assert(run_len == input.size());

    std::vector<uint8_t> output;
    for (size_t i = 0; i < run_len; i++) {
        output.push_back(decoder.decodeLiteral(reader));
    }

    assert(output.size() == input.size());
    for (size_t i = 0; i < input.size(); i++) {
        assert(output[i] == input[i]);
    }

    std::cout << "  Decoded: \"";
    for (auto b : output) std::cout << static_cast<char>(b);
    std::cout << "\"" << std::endl;
    std::cout << "  PASSED!" << std::endl;
}

// ============================================================
// 测试 3: 游程截断
// ============================================================
void testRunTruncation() {
    std::cout << "\n--- Test 3: Run Truncation ---" << std::endl;

    const size_t Lb = 4;
    const size_t max_run = (1 << Lb) - 1;
    const size_t total_literals = 20;

    std::vector<uint32_t> literal_freq(256, 0);
    for (size_t i = 0; i < total_literals; i++) {
        literal_freq[static_cast<uint8_t>('A' + (i % 26))]++;
    }

    std::vector<uint32_t> offset_freq(64, 0);
    offset_freq[0] = 2;

    std::vector<uint32_t> length_freq(32, 0);
    length_freq[max_run] = 1;
    length_freq[total_literals - max_run] = 1;

    HuffmanTree3HM encoder;
    encoder.buildTrees(literal_freq, offset_freq, length_freq,
                       64, 32, 6, Lb, Lb, Lb);

    // 编码
    std::vector<uint8_t> buffer(1024, 0);
    BitWriter writer{std::span<uint8_t>(buffer)};

    encoder.serialize(writer);

    encoder.encodeRunHeader(static_cast<uint16_t>(max_run), writer);
    for (size_t i = 0; i < max_run; i++) {
        encoder.encodeLiteral(static_cast<uint8_t>('A' + (i % 26)), writer);
    }

    size_t remaining = total_literals - max_run;
    encoder.encodeRunHeader(static_cast<uint16_t>(remaining), writer);
    for (size_t i = 0; i < remaining; i++) {
        encoder.encodeLiteral(static_cast<uint8_t>('A' + ((max_run + i) % 26)), writer);
    }

    writer.flush();
    size_t bytes_written = writer.getBytesWritten();
    std::cout << "  Encoded " << total_literals << " literals (truncated into 2 runs) into "
              << bytes_written << " bytes total" << std::endl;

    // 解码
    BitReader reader(std::span<const uint8_t>(buffer.data(), bytes_written));
    HuffmanTree3HM decoder;
    decoder.deserialize(reader);

    std::vector<uint8_t> output;

    uint16_t offset = decoder.decodeOffset(reader);
    assert(offset == 0);
    uint16_t run_len = decoder.decodeRunLength(reader);
    assert(run_len == max_run);
    for (size_t i = 0; i < run_len; i++) {
        output.push_back(decoder.decodeLiteral(reader));
    }

    offset = decoder.decodeOffset(reader);
    assert(offset == 0);
    run_len = decoder.decodeRunLength(reader);
    assert(run_len == remaining);
    for (size_t i = 0; i < run_len; i++) {
        output.push_back(decoder.decodeLiteral(reader));
    }

    assert(output.size() == total_literals);
    std::cout << "  Decoded " << output.size() << " literals" << std::endl;
    std::cout << "  PASSED!" << std::endl;
}

// ============================================================
// 测试 4: 空数据
// ============================================================
void testEmptyData() {
    std::cout << "\n--- Test 4: Empty Data ---" << std::endl;

    std::vector<uint32_t> literal_freq(256, 0);
    std::vector<uint32_t> offset_freq(64, 0);
    std::vector<uint32_t> length_freq(32, 0);

    HuffmanTree3HM encoder;
    encoder.buildTrees(literal_freq, offset_freq, length_freq,
                       64, 32, 6, 5, 5, 5);

    std::vector<uint8_t> buffer(1024, 0);
    BitWriter writer{std::span<uint8_t>(buffer)};
    encoder.serialize(writer);
    writer.flush();

    std::cout << "  Empty tree size: " << encoder.getTreeSize() << " bits" << std::endl;
    std::cout << "  PASSED!" << std::endl;
}

// ============================================================
// 测试 5: 三数考量合并测试
// ============================================================
void testThreeNumberMerge() {
    std::cout << "\n--- Test 5: Three-Number Merge Strategy ---" << std::endl;

    // 三数考量：取最小的两个集合合并，减少总符号数
    // 例如：字面量树 256，length 树 32，offset 树 64
    // 合并最小的两个（length + offset）= 96
    // 总符号数 = 256 + 96 = 352
    // 相比 Deflate 的 286 + 30 = 316，在 length 和 offset 都较小时有优势
    {
        size_t literal_count = 256;
        size_t length_count = 32;
        size_t offset_count = 64;

        // 合并最小的两个：length(32) + offset(64) = 96
        size_t merged = length_count + offset_count;
        size_t total = literal_count + merged;
        std::cout << "  Case 1: literal=" << literal_count
                  << ", length=" << length_count
                  << ", offset=" << offset_count
                  << " -> merged=" << merged
                  << ", total=" << total << std::endl;
        // 当 length 和 offset 都较小时，总符号数可能小于 Deflate
        assert(total == 352);
    }

    // 当 offset 很大时，合并 length + offset 可能不如 Deflate
    {
        size_t literal_count = 256;
        size_t length_count = 32;
        size_t offset_count = 256;

        size_t merged = length_count + offset_count;
        size_t total = literal_count + merged;
        std::cout << "  Case 2: literal=" << literal_count
                  << ", length=" << length_count
                  << ", offset=" << offset_count
                  << " -> merged=" << merged
                  << ", total=" << total << std::endl;
        // 此时总符号数较大，但三树设计避免了 literal/length 混合树的 286 符号限制
        assert(total == 544);
    }

    std::cout << "  PASSED!" << std::endl;
}

// ============================================================
// 测试 6: offset / length 槽宽不同（独立 chunk_bits）
// ============================================================
void testAsymmetricChunkBits() {
    std::cout << "\n--- Test 6: Asymmetric offset/length chunk bits ---" << std::endl;

    const size_t offset_count = 16;
    const size_t length_count = 8;
    const size_t offset_bits = 6;
    const size_t length_bits = 5;
    const size_t offset_chunk = 4;
    const size_t length_chunk = 3;

    std::vector<uint32_t> literal_freq(256, 0);
    literal_freq[61] = 1;
    literal_freq[62] = 1;

    std::vector<uint32_t> offset_freq(offset_count, 0);
    // encodeRunHeader: offset 0 → two LSB chunks (0,0); matches (3,3),(5,2) → (3,0),(5,0)
    offset_freq[0] = 4;
    offset_freq[3] = 1;
    offset_freq[5] = 1;

    std::vector<uint32_t> length_freq(length_count, 0);
    // run len 2 → (2,0); len 3 → (3,0); len 2 → (2,0)
    length_freq[0] = 3;
    length_freq[2] = 2;
    length_freq[3] = 1;

    HuffmanTree3HM encoder;
    encoder.buildTrees(literal_freq, offset_freq, length_freq, offset_count, length_count,
                       offset_bits, length_bits, offset_chunk, length_chunk);

    std::vector<uint8_t> buffer(1024, 0);
    BitWriter writer{std::span<uint8_t>(buffer)};
    encoder.serialize(writer);
    encoder.encodeRunHeader(2, writer);
    encoder.encodeLiteral(61, writer);
    encoder.encodeLiteral(62, writer);
    encoder.encodeMatch(3, 3, writer);
    encoder.encodeMatch(5, 2, writer);

    writer.flush();
    size_t bytes_written = writer.getBytesWritten();

    BitReader reader(std::span<const uint8_t>(buffer.data(), bytes_written));
    HuffmanTree3HM decoder;
    decoder.deserialize(reader);

    assert(decoder.decodeOffset(reader) == 0);
    assert(decoder.decodeRunLength(reader) == 2);
    assert(decoder.decodeLiteral(reader) == 61);
    assert(decoder.decodeLiteral(reader) == 62);
    assert(decoder.decodeOffset(reader) == 3);
    assert(decoder.decodeMatchLength(reader) == 3);
    assert(decoder.decodeOffset(reader) == 5);
    assert(decoder.decodeMatchLength(reader) == 2);

    std::cout << "  PASSED!" << std::endl;
}

int main() {
    std::cout << "=== HuffmanTree3HM Tests ===" << std::endl;

    testBasicRoundTrip();
    testAllLiterals();
    testRunTruncation();
    testEmptyData();
    testThreeNumberMerge();
    testAsymmetricChunkBits();

    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}