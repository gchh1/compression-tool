#include "DeflateCompressor.hpp"

namespace compressor {
namespace core {

auto DeflateCompressor::compress(const std::vector<uint8_t>& original_data)
    -> CompressResult {
    CompressResult result;
    result.original_size = original_data.size();

    // 开始计时
    auto start_time = std::chrono::high_resolution_clock::now();

    /* ==========================================
     * 工业级流水线：LZ77Fast -> Serialize -> Huffman
     * ========================================== */
    // 1. 极速哈希匹配，生成 Token 流
    std::vector<algorithm::Token> tokens =
        algorithm::Deflate::compress(original_data);

    // 2. 将 Token 序列化为中间字节流
    std::vector<uint8_t> intermediate_bytes = serializeTokens(tokens);

    // 3. 将中间字节流送入 Huffman 引擎进行极致压缩
    result.data = algorithm::Huffman::compress(intermediate_bytes);

    // 停止计时
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    // 填充结果数据
    result.compressed_size = result.data.size();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;
    result.time_ms = elapsed.count();

    return result;
}

auto DeflateCompressor::decompress(const std::vector<uint8_t>& compressed_data)
    -> CompressResult {
    CompressResult result;
    result.compressed_size = compressed_data.size();

    auto start_time = std::chrono::high_resolution_clock::now();

    /* ==========================================
     * 逆向流水线：Huffman -> Deserialize -> LZ77Fast
     * ========================================== */
    // 1. 哈夫曼解压，还原出中间字节流
    std::vector<uint8_t> intermediate_bytes =
        algorithm::Huffman::decompress(compressed_data);

    // 2. 将字节流反序列化为 Token 数组
    std::vector<algorithm::Token> tokens =
        deserializeTokens(intermediate_bytes);

    // 3. 根据 Token 游走还原原始文件
    result.data = algorithm::Deflate::decompress(tokens);

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    result.original_size = result.data.size();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;
    result.time_ms = elapsed.count();

    return result;
}

// ---------------------------------------------------------
// 私有序列化桥接工具实现
// ---------------------------------------------------------
std::vector<uint8_t> DeflateCompressor::serializeTokens(
    const std::vector<algorithm::Token>& tokens) {
    std::vector<uint8_t> res;
    res.reserve(tokens.size() * 3);  // 预估容量

    for (const auto& t : tokens) {
        if (t.is_literal) {
            res.push_back(1);  // 标记位 1：代表原字符
            res.push_back(static_cast<uint8_t>(t.value));
        } else {
            res.push_back(0);  // 标记位 0：代表匹配块
            // 拆解 16 位整数为两个 8 位字节 (大端序/Big Endian)
            res.push_back(static_cast<uint8_t>(t.value >> 8));
            res.push_back(static_cast<uint8_t>(t.value & 0xFF));
            res.push_back(static_cast<uint8_t>(t.position >> 8));
            res.push_back(static_cast<uint8_t>(t.position & 0xFF));
        }
    }
    return res;
}

std::vector<algorithm::Token> DeflateCompressor::deserializeTokens(
    const std::vector<uint8_t>& data) {
    std::vector<algorithm::Token> tokens;
    size_t i = 0;
    while (i < data.size()) {
        uint8_t flag = data[i++];
        if (flag == 1) {
            tokens.push_back({true, data[i++], 0});
        } else if (flag == 0) {
            uint16_t length =
                (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
            i += 2;
            uint16_t distance =
                (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
            i += 2;
            tokens.push_back({false, length, distance});
        }
    }
    return tokens;
}

}  // namespace core
}  // namespace compressor