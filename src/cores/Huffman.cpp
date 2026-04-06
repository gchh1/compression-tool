#include "huffman.hpp"
#include "huffman_tree.hpp"

namespace core {
namespace algorithm {

/**
 * @brief 压缩数据
 */
std::vector<u8> Huffman::compress(const std::vector<u8>& input) {
    if (input.empty()) {
        return {};
    }

    std::vector<u8> result;
    result.reserve(input.size());  // 预分配空间

    // 1. 写入原始大小（4 字节）
    u32 original_size = static_cast<u32>(input.size());
    core::concat(result, core::to_u8(original_size));

    // 2. 构建哈夫曼树并序列化
    HuffmanTree huffman_tree(input);
    std::vector<std::string> dictionary = huffman_tree.encode();

    // 3. 序列化哈夫曼树（位打包）
    u8 bit_buffer = 0;
    u8 bit_count = 0;

    auto writeBit = [&](u8 bit) {
        bit_buffer = (bit_buffer << 1) | (bit & 1);
        bit_count++;
        if (bit_count == 8) {
            result.push_back(bit_buffer);
            bit_buffer = 0;
            bit_count = 0;
        }
    };

    // 写入序列化的树
    std::vector<u8> tree_data = huffman_tree.getTree();
    for (size_t i = 0; i < tree_data.size(); ++i) {
        if (tree_data[i] == '0') {
            writeBit(0);
        } else if (tree_data[i] == '1') {
            writeBit(1);
            // 写入叶子节点的 8 位符号
            u8 symbol = tree_data[++i];
            for (int j = 7; j >= 0; --j) {
                writeBit((symbol >> j) & 1);
            }
        }
    }

    // 4. 编码数据
    for (u8 byte : input) {
        const std::string& code = dictionary[byte];
        for (char bit : code) {
            writeBit(bit == '0' ? 0 : 1);
        }
    }

    // 处理剩余的位
    if (bit_count > 0) {
        bit_buffer <<= (8 - bit_count);
        result.push_back(bit_buffer);
    }

    return result;
}

/**
 * @brief 解压数据
 */
std::vector<u8> Huffman::decompress(const std::vector<u8>& input) {
    if (input.size() < 4) {
        return {};
    }

    std::vector<u8> result;

    // 1. 读取原始大小
    u32 original_size = core::from_u8<u32>(input.data(), 4);
    size_t byte_idx = 4;
    u8 bit_idx = 0;

    // 2. 位读取辅助函数
    auto readBit = [&]() -> int {
        if (byte_idx >= input.size()) {
            return -1;
        }

        int bit = (input[byte_idx] >> (7 - bit_idx)) & 1;
        bit_idx++;

        if (bit_idx == 8) {
            bit_idx = 0;
            byte_idx++;
        }

        return bit;
    };

    // 3. 重构哈夫曼树
    HuffmanTree huffman_tree(readBit);

    // 4. 解码数据
    HuffmanNode* root = huffman_tree.getRoot();
    HuffmanNode* cursor = root;

    while (result.size() < original_size) {
        int bit = readBit();
        if (bit == -1) break;

        // 移动到子节点
        cursor = (bit == 0) ? cursor->left : cursor->right;

        // 到达叶子节点
        if (cursor && cursor->isLeaf()) {
            result.push_back(cursor->symbol);
            cursor = root;  // 重置到根节点
        }
    }

    return result;
}

}  // namespace algorithm
}  // namespace core
