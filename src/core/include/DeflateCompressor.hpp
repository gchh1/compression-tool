#pragma once

#include <chrono>  // 用于统计 time_ms

#include "Deflate.hpp"
#include "Huffman.hpp"
#include "compressor.hpp"

namespace compressor {
namespace core {

class DeflateCompressor : public ICompressor {
   public:
    auto compress(const std::vector<uint8_t>& original_data)
        -> CompressResult override;
    auto decompress(const std::vector<uint8_t>& compressed_data)
        -> CompressResult override;

    auto get_algorithm_name(void) -> std::string override {
        return "Deflate (LZ77Fast + Huffman)";
    }

   private:
    /** @brief 桥接工具：将 Token 数组序列化为纯字节流，喂给 Huffman */
    std::vector<uint8_t> serializeTokens(
        const std::vector<algorithm::Token>& tokens);

    /** @brief 桥接工具：将解压后的纯字节流还原为 Token 数组，喂给 Deflate */
    std::vector<algorithm::Token> deserializeTokens(
        const std::vector<uint8_t>& data);
};

}  // namespace core
}  // namespace compressor