#include <cassert>
#include <iostream>
#include <span>
#include <vector>

#include "Delta.hpp"

// 辅助打印函数
void printData(const std::vector<uint8_t>& data, const std::string& name) {
    std::cout << name << " (first 15 bytes): ";
    for (int i = 0; i < std::min((int)data.size(), 15); ++i) {
        std::cout << (int)data[i] << " ";
    }
    std::cout << "..." << std::endl;
}

int main() {
    using namespace compressor::algorithm;

    // 1. 准备测试数据 (构造一个 10000 字节的递增序列 0, 1, 2... 255, 0, 1...)
    std::vector<uint8_t> original_data;
    for (int i = 0; i < 10000; ++i) {
        original_data.push_back(i % 256);
    }

    // 2. 准备输出容器 (Delta 算法是 1:1 映射，所以大小相同)
    std::vector<uint8_t> encoded_data(original_data.size());
    std::vector<uint8_t> decoded_data(original_data.size());

    // 3. 实例化算法
    DeltaEncode encoder(100);  // 质量 100，shift = 0
    DeltaDecode decoder;

    // ==========================================
    // 测试场景 1：一次性全量处理 (Full Chunk)
    // ==========================================
    std::cout << "--- [Test 1] Full Chunk Processing ---" << std::endl;

    // 调用我们在 AlgorithmBase 中定义好的 process 接口
    AlgorithmStatus enc_status =
        encoder.process(original_data, encoded_data, true);
    assert(enc_status.done == true);

    AlgorithmStatus dec_status =
        decoder.process(encoded_data, decoded_data, true);
    assert(dec_status.done == true);

    assert(original_data == decoded_data);  // 验证解压数据是否和原数据完全一致
    std::cout << "[PASS] Full Chunk Test Successful!\n" << std::endl;

    // ==========================================
    // 测试场景 2：模拟极其极端的流式分块 (Streaming Chunks)
    // ==========================================
    std::cout << "--- [Test 2] Streaming Chunk Processing ---" << std::endl;
    encoder.reset();  // 重置内部的 prev_ 状态
    decoder.reset();

    std::vector<uint8_t> stream_encoded(original_data.size());
    std::vector<uint8_t> stream_decoded(original_data.size());

    // 故意设置一个不规则的 Chunk 大小 (1333 字节)，这会导致跨越边界！
    size_t chunk_size = 1333;
    size_t enc_read_pos = 0, enc_write_pos = 0;

    // 2.1 模拟外层 Chunk Pool 疯狂切块喂给 Encoder
    while (enc_read_pos < original_data.size()) {
        size_t remain = original_data.size() - enc_read_pos;
        size_t current_chunk_size = std::min(chunk_size, remain);
        bool is_last = (remain == current_chunk_size);  // 最后一个小块

        // 切割出当前的 Input Chunk 和 Output Chunk
        std::span<const uint8_t> in_chunk(original_data.data() + enc_read_pos,
                                          current_chunk_size);
        std::span<uint8_t> out_chunk(stream_encoded.data() + enc_write_pos,
                                     stream_encoded.size() - enc_write_pos);

        // 调用算法进行流式处理
        AlgorithmStatus s = encoder.process(in_chunk, out_chunk, is_last);

        // 更新调度器的游标
        enc_read_pos += s.bytes_consumed;
        enc_write_pos += s.bytes_produced;

        if (is_last) {
            assert(s.done == true);  // 验证收到 is_last 后算法是否正确结束
        }
    }
    printData(stream_encoded, "Encoded Data");

    // 2.2 模拟 Decoder 接收网络流传来的 Chunk
    size_t dec_read_pos = 0, dec_write_pos = 0;
    while (dec_read_pos < stream_encoded.size()) {
        size_t remain = stream_encoded.size() - dec_read_pos;
        size_t current_chunk_size = std::min(chunk_size, remain);
        bool is_last = (remain == current_chunk_size);

        std::span<const uint8_t> in_chunk(stream_encoded.data() + dec_read_pos,
                                          current_chunk_size);
        std::span<uint8_t> out_chunk(stream_decoded.data() + dec_write_pos,
                                     stream_decoded.size() - dec_write_pos);

        AlgorithmStatus s = decoder.process(in_chunk, out_chunk, is_last);

        dec_read_pos += s.bytes_consumed;
        dec_write_pos += s.bytes_produced;

        if (is_last) {
            assert(s.done == true);
        }
    }

    // 终极断言：流式切块解压后的数据，必须和原始数据完美一致！
    assert(original_data == stream_decoded);
    std::cout << "[PASS] Streaming Chunk Test Successful!\n" << std::endl;

    return 0;
}