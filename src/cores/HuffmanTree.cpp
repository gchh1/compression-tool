#include "huffman_tree.hpp"

#include <queue>

namespace core {
namespace algorithm {

/**
 * @brief 从字节流构建哈夫曼树
 */
HuffmanTree::HuffmanTree(const std::vector<u8>& symbols) {
    if (symbols.empty()) {
        root_ = nullptr;
        return;
    }

    // 1. 统计频率
    std::vector<u32> freq_map(256, 0);
    for (u8 symbol : symbols) {
        freq_map[symbol]++;
    }

    // 2. 构建哈夫曼树
    buildTree(freq_map);
}

/**
 * @brief 从频率表构建哈夫曼树
 */
HuffmanTree::HuffmanTree(const std::vector<u32>& freq_map) {
    buildTree(freq_map);
}

/**
 * @brief 生成编码字典
 */
std::vector<std::string> HuffmanTree::encode() {
    tree_.clear();  // 清空之前的树
    std::vector<std::string> dictionary(256, "");
    std::string path = "";
    preorder(root_, path, dictionary);
    return dictionary;
}

/**
 * @brief 构建哈夫曼树
 */
void HuffmanTree::buildTree(const std::vector<u32>& freq_map) {
    // 使用优先队列（小顶堆）构建哈夫曼树
    std::priority_queue<HuffmanNode*, std::vector<HuffmanNode*>, HuffmanNodeCompare> pq;

    // 初始化：将所有非零频率的符号加入队列
    for (int i = 0; i < 256; ++i) {
        if (freq_map[i] > 0) {
            pq.push(new HuffmanNode(static_cast<u8>(i), freq_map[i]));
        }
    }

    // 特殊情况：只有一个符号
    if (pq.size() == 1) {
        HuffmanNode* only = pq.top();
        pq.pop();
        root_ = new HuffmanNode(only, new HuffmanNode(0, 1));  // 添加虚拟节点，频率设为 1
    }

    // 重复合并两个频率最小的节点
    while (pq.size() >= 2) {
        HuffmanNode* left = pq.top();
        pq.pop();
        HuffmanNode* right = pq.top();
        pq.pop();

        HuffmanNode* parent = new HuffmanNode(left, right);
        pq.push(parent);
    }

    // 根节点
    if (!pq.empty()) {
        root_ = pq.top();
    }
}

/**
 * @brief 前序遍历生成编码和序列化树
 */
void HuffmanTree::preorder(HuffmanNode* n, std::string& cur, std::vector<std::string>& res) {
    if (n == nullptr) {
        return;
    }

    // 到达叶子节点
    if (n->isLeaf()) {
        res[n->symbol] = cur;
        tree_.push_back('1');
        tree_.push_back(n->symbol);
        return;
    }

    // 内部节点
    tree_.push_back('0');

    // 遍历左子树
    cur.push_back('0');
    preorder(n->left, cur, res);
    cur.pop_back();

    // 遍历右子树
    cur.push_back('1');
    preorder(n->right, cur, res);
    cur.pop_back();
}

}  // namespace algorithm
}  // namespace core
