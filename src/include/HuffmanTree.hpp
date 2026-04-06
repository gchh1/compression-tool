#ifndef HUFFMAN_TREE_HPP
#define HUFFMAN_TREE_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "core.hpp"

namespace core {
namespace algorithm {

/**
 * @brief 哈夫曼树节点
 */
struct HuffmanNode {
    u8 symbol;           // 符号
    u32 frequency;       // 频率
    HuffmanNode* left;   // 左子节点
    HuffmanNode* right;  // 右子节点

    // 叶子节点构造函数
    HuffmanNode(u8 d, u32 f) : symbol(d), frequency(f), left(nullptr), right(nullptr) {}

    // 内部节点构造函数
    HuffmanNode(HuffmanNode* l, HuffmanNode* r)
        : symbol(0), frequency(l->frequency + r->frequency), left(l), right(r) {}

    // 判断是否为叶子节点
    bool isLeaf() const { return left == nullptr && right == nullptr; }

    // RAII：析构函数自动清理
    ~HuffmanNode() {
        delete left;
        delete right;
    }
};

/**
 * @brief 节点比较器（用于优先队列）
 */
struct HuffmanNodeCompare {
    bool operator()(HuffmanNode* a, HuffmanNode* b) {
        return a->frequency > b->frequency;  // 小顶堆
    }
};

/**
 * @brief 哈夫曼树类
 * 
 * 接收字节流，构建编码字典和序列化树结构
 */
class HuffmanTree {
public:
    // 禁用默认构造、拷贝构造和拷贝赋值
    HuffmanTree() = delete;
    HuffmanTree(const HuffmanTree&) = delete;
    HuffmanTree& operator=(const HuffmanTree&) = delete;

    /**
     * @brief 从字节流构建哈夫曼树
     * @param symbols 输入字节流
     */
    explicit HuffmanTree(const std::vector<u8>& symbols);

    /**
     * @brief 从频率表构建哈夫曼树
     * @param freq_map 频率表（大小 256）
     */
    explicit HuffmanTree(const std::vector<u32>& freq_map);

    /**
     * @brief 从位流读取器重构哈夫曼树（用于解压）
     * @tparam BitReader 位读取函数对象
     * @param readBit 位读取函数
     */
    template <typename BitReader>
    explicit HuffmanTree(BitReader readBit);

    /**
     * @brief 析构函数（RAII）
     */
    ~HuffmanTree() {
        delete root_;
    }

    /**
     * @brief 生成编码字典
     * @return std::vector<std::string> 256 个符号的编码
     */
    std::vector<std::string> encode();

    /**
     * @brief 获取序列化的树结构（前序遍历）
     * @return std::vector<u8> 序列化的树
     */
    std::vector<u8> getTree() const { return tree_; }

    /**
     * @brief 获取根节点
     * @return HuffmanNode* 根节点指针
     */
    HuffmanNode* getRoot() const { return root_; }

private:
    /**
     * @brief 构建哈夫曼树
     * @param freqMap 频率表
     */
    void buildTree(const std::vector<u32>& freqMap);

    /**
     * @brief 前序遍历生成编码和序列化树
     * @param n 当前节点
     * @param cur 当前编码路径
     * @param res 编码字典
     */
    void preorder(HuffmanNode* n, std::string& cur, std::vector<std::string>& res);

    HuffmanNode* root_ = nullptr;  // 根节点
    std::vector<u8> tree_;         // 序列化的树（前序遍历）
};

// 模板实现必须放在头文件
template <typename BitReader>
HuffmanTree::HuffmanTree(BitReader readBit) {
    // 递归 lambda 构建哈夫曼树
    auto buildTree = [&readBit](auto& self) -> HuffmanNode* {
        int bit = readBit();

        if (bit == -1) {
            return nullptr;
        }

        // 0 = 内部节点，递归构建左右子树
        if (bit == 0) {
            HuffmanNode* left = self(self);
            HuffmanNode* right = self(self);
            return new HuffmanNode(left, right);
        }
        // 1 = 叶子节点，读取 8 位符号
        else {
            u8 temp = 0;
            for (int i = 0; i < 8; ++i) {
                int bit1 = readBit();
                if (bit1 == -1) break;
                temp = static_cast<u8>((temp << 1) | bit1);
            }
            return new HuffmanNode(temp, 0);
        }
    };

    root_ = buildTree(buildTree);
}

}  // namespace algorithm
}  // namespace core

#endif
