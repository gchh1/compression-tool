#include <iostream>
#include <vector>
#include <cassert>
#include <random>
#include <chrono>

#include "huffman.hpp"
#include "lz77.hpp"

using namespace core;
using namespace core::algorithm;

void test_huffman_basic() {
    std::cout << "=== 测试 Huffman 基本功能 ===" << std::endl;

    std::vector<u8> data = {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};

    auto compressed = Huffman::compress(data);
    auto decompressed = Huffman::decompress(compressed);

    std::cout << "原始大小：" << data.size() << " 字节" << std::endl;
    std::cout << "压缩大小：" << compressed.size() << " 字节" << std::endl;
    std::cout << "压缩率：" << (1.0 - (double)compressed.size() / data.size()) * 100 << "%" << std::endl;

    assert(data == decompressed);
    std::cout << "✓ 基本测试通过！" << std::endl << std::endl;
}

void test_huffman_empty() {
    std::cout << "=== 测试 Huffman 空数据 ===" << std::endl;

    std::vector<u8> empty_data;
    auto compressed = Huffman::compress(empty_data);
    auto decompressed = Huffman::decompress(compressed);

    assert(decompressed.empty());
    std::cout << "✓ 空数据测试通过！" << std::endl << std::endl;
}

void test_huffman_single_char() {
    std::cout << "=== 测试 Huffman 单一字符 ===" << std::endl;

    std::vector<u8> data(100, 'A');  // 100 个 'A'
    auto compressed = Huffman::compress(data);
    auto decompressed = Huffman::decompress(compressed);

    std::cout << "原始大小：" << data.size() << " 字节" << std::endl;
    std::cout << "压缩大小：" << compressed.size() << " 字节" << std::endl;

    assert(data == decompressed);
    std::cout << "✓ 单一字符测试通过！" << std::endl << std::endl;
}

void test_huffman_random() {
    std::cout << "=== 测试 Huffman 随机数据 ===" << std::endl;

    std::mt19937 gen(42);  // 固定种子
    std::uniform_int_distribution<u16> dist(0, 255);

    std::vector<u8> data(10000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = dist(gen);
    }

    auto compressed = Huffman::compress(data);
    auto decompressed = Huffman::decompress(compressed);

    std::cout << "原始大小：" << data.size() << " 字节" << std::endl;
    std::cout << "压缩大小：" << compressed.size() << " 字节" << std::endl;
    std::cout << "压缩率：" << (1.0 - (double)compressed.size() / data.size()) * 100 << "%" << std::endl;

    assert(data == decompressed);
    std::cout << "✓ 随机数据测试通过！" << std::endl << std::endl;
}

void test_huffman_large() {
    std::cout << "=== 测试 Huffman 大数据 ===" << std::endl;

    std::vector<u8> data;
    data.reserve(100000);

    // 生成有规律的数据（更好的压缩效果）
    for (int i = 0; i < 10000; ++i) {
        data.push_back('a');
        data.push_back('b');
        data.push_back('c');
        data.push_back(' ');
        data.push_back('a');
        data.push_back('b');
        data.push_back('c');
        data.push_back('\n');
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto compressed = Huffman::compress(data);
    auto end = std::chrono::high_resolution_clock::now();
    auto compress_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    start = std::chrono::high_resolution_clock::now();
    auto decompressed = Huffman::decompress(compressed);
    end = std::chrono::high_resolution_clock::now();
    auto decompress_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "原始大小：" << data.size() << " 字节" << std::endl;
    std::cout << "压缩大小：" << compressed.size() << " 字节" << std::endl;
    std::cout << "压缩率：" << (1.0 - (double)compressed.size() / data.size()) * 100 << "%" << std::endl;
    std::cout << "压缩时间：" << compress_time << " ms" << std::endl;
    std::cout << "解压时间：" << decompress_time << " ms" << std::endl;

    assert(data == decompressed);
    std::cout << "✓ 大数据测试通过！" << std::endl << std::endl;
}

void test_lz77_huffman_chain() {
    std::cout << "=== 测试 LZ77 + Huffman 级联 ===" << std::endl;

    std::vector<u8> data = {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd',
                            'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};

    // LZ77 压缩
    LZ77 lz77(2, 2);
    auto lz77_compressed = lz77.Compress(data, 255, 255, lz77MatchType::KMPNEXT);

    // Huffman 压缩 LZ77 的结果
    auto huffman_compressed = Huffman::compress(lz77_compressed);

    // Huffman 解压
    auto huffman_decompressed = Huffman::decompress(huffman_compressed);

    // LZ77 解压
    auto final_data = lz77.Decompress(huffman_decompressed);

    std::cout << "原始大小：" << data.size() << " 字节" << std::endl;
    std::cout << "LZ77 压缩后：" << lz77_compressed.size() << " 字节" << std::endl;
    std::cout << "Huffman 压缩后：" << huffman_compressed.size() << " 字节" << std::endl;
    std::cout << "总压缩率：" << (1.0 - (double)huffman_compressed.size() / data.size()) * 100 << "%" << std::endl;

    assert(data == final_data);
    std::cout << "✓ LZ77+Huffman 级联测试通过！" << std::endl << std::endl;
}

int main() {
    std::cout << "╔════════════════════════════════════╗" << std::endl;
    std::cout << "║     Huffman 压缩算法测试套件       ║" << std::endl;
    std::cout << "╚════════════════════════════════════╝" << std::endl << std::endl;

    test_huffman_basic();
    test_huffman_empty();
    test_huffman_single_char();
    test_huffman_random();
    test_huffman_large();
    test_lz77_huffman_chain();

    std::cout << "╔════════════════════════════════════╗" << std::endl;
    std::cout << "║        所有测试通过！✓             ║" << std::endl;
    std::cout << "╚════════════════════════════════════╝" << std::endl;

    return 0;
}
