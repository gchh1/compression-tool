#ifndef HUFFMAN_HPP
#define HUFFMAN_HPP

#include <vector>

#include "core.hpp"

namespace core {
namespace algorithm {

/**
 * @brief 哈夫曼压缩算法
 * 
 * 压缩格式：
 * [4 字节原始大小][序列化的哈夫曼树][编码数据]
 */
class Huffman {
public:
    /**
     * @brief 压缩数据
     * @param input 输入字节流
     * @return std::vector<u8> 压缩后的字节流
     */
    static std::vector<u8> compress(const std::vector<u8>& input);

    /**
     * @brief 解压数据
     * @param input 压缩后的字节流
     * @return std::vector<u8> 解压后的字节流
     */
    static std::vector<u8> decompress(const std::vector<u8>& input);
};

}  // namespace algorithm
}  // namespace core

#endif
