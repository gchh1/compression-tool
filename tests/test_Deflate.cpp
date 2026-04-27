
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "Deflate.hpp"

using namespace compressor::algorithm;

// 辅助函数：将二进制数据保存到文件
void saveToFile(const std::vector<uint8_t>& data, const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
    if (out) {
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        out.close();
        std::cout << "  [Disk] Saved compressed stream to: " << filename
                  << std::endl;
    } else {
        std::cerr << "  [Error] Failed to open file: " << filename << std::endl;
    }
}

// 核心流式测试框架
// 核心流式测试框架 (修复扩容机制版)
void runStreamingTest(const std::vector<uint8_t>& original_data,
                      const std::string& test_name) {
    std::cout << "\n=============================================" << std::endl;
    std::cout << "--- " << test_name << " ---" << std::endl;
    std::cout << "Original Size: " << original_data.size() << " bytes"
              << std::endl;

    // 初始依然给一点点预留空间
    std::vector<uint8_t> compressed_data(original_data.size() + 1024);
    Deflate deflater;

    size_t chunk_size = 1373;
    size_t read_pos = 0;
    size_t write_pos = 0;
    int chunk_count = 0;

    bool is_done = false;

    // 【关键修改】：循环条件改为依靠引擎的 is_done 标志
    while (!is_done) {
        size_t remain = original_data.size() - read_pos;
        size_t current_chunk_size = std::min(chunk_size, remain);

        // 当我们把所有输入数据都喂完时，is_last 才为 true
        bool is_last = (remain == current_chunk_size);

        std::span<const uint8_t> in_chunk(original_data.data() + read_pos,
                                          current_chunk_size);
        std::span<uint8_t> out_chunk(compressed_data.data() + write_pos,
                                     compressed_data.size() - write_pos);

        AlgorithmStatus s = deflater.process(in_chunk, out_chunk, is_last);

        read_pos += s.bytes_consumed;
        write_pos += s.bytes_produced;
        chunk_count++;

        // 【关键防御机制】：如果算法喊着要空间，我们就给它扩容！
        if (s.need_output) {
            // 每次扩容增加 4KB
            compressed_data.resize(compressed_data.size() + 4096);
            // 扩容后，下次循环会自动计算出更大的 out_chunk，引擎就能继续写了！
        }

        if (s.done) {
            is_done = true;
        }
    }

    compressed_data.resize(write_pos);  // 截断到实际真实写出的物理大小

    std::cout << "Compressed Size: " << compressed_data.size() << " bytes"
              << std::endl;
    std::cout << "Compression Ratio: "
              << (float)compressed_data.size() / original_data.size() * 100
              << "%" << std::endl;
    std::cout << "Total Scheduling Cycles: " << chunk_count << std::endl;

    saveToFile(compressed_data, test_name + ".deflate");
}

int main() {
    // ========================================================
    // 测试用例 1：高重复度文本 (验证 LZ77 滑动窗口能否跨块工作)
    // ========================================================
    std::string pattern = "DEFLATE_STREAMING_COMPRESSION_TEST_RFC1951_";
    std::vector<uint8_t> repetitive_data;
    repetitive_data.reserve(50000);
    for (int i = 0; i < 1500; ++i) {  // 约 64KB 数据，刚好能填满一整个 Window
        repetitive_data.insert(repetitive_data.end(), pattern.begin(),
                               pattern.end());
    }
    runStreamingTest(repetitive_data, "repetitive_test");

    // ========================================================
    // 测试用例 2：完全随机数据 (验证哈夫曼树在极端情况下的稳定性)
    // ========================================================
    std::vector<uint8_t> random_data(50000);
    std::mt19937 rng(1337);  // 固定随机种子以便复现
    std::uniform_int_distribution<int> dist(0, 255);
    for (int i = 0; i < 50000; ++i) {
        random_data[i] = dist(rng);
    }
    runStreamingTest(random_data, "random_test");

    std::cout << "\n[PASS] All C++ Streaming Tests Finished." << std::endl;
    std::cout << "Next Step: Use the Python script to verify the generated "
                 ".deflate files!"
              << std::endl;

    return 0;
}