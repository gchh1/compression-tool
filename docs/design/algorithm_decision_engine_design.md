# 智能算法决策器模块 - 设计讨论记录

> **项目**: WebCompress 压缩工具  
> **模块名称**: Algorithm Decision Engine (ADE)  
> **讨论日期**: 2026-01-15  
> **参与人员**: 用户 (产品/架构) + AI Assistant (技术顾问)  
> **当前阶段**: 需求分析与方案设计 (Phase 1-2)

> **仓库路径更新 (2026-05)**:
> - ADE 资产路径从 `src/ade/data/` 迁移到 `assets/ade/`
> - ADE C++ 测试路径从 `src/ade/tests/` 迁移到 `tests/cpp/ade/`
> - `src/ade/` 仅保留代码相关目录（见下方分层布局）、`tools/`、`bench/`
> - `src/ade/data/` 与 `src/ade/tests/` 已清理，不再作为有效路径

### 当前推荐目录布局（规范，2026-05）

ADE C++ 布局：**`src/ade/include/`** 与 **`bench/`、`features/`、`models/`、`runtime/`、`tools/` 并列**；**`include/` 内不再划分子文件夹**，全部公共 `.hpp` 扁平存放（命名避免冲突）。**实现 `.cpp`** 放在 `runtime/` 或日后的 `models/`、`features/` 根下；**禁止**在各域下再建 `src/` 子目录。拆 `.cpp` 时：声明留在 `include/*.hpp`，定义放到对应 `.cpp`。

```
assets/ade/                              # 模型与训练数据资产
tests/cpp/ade/                           # ADE C++ 单元测试

src/ade/include/*.hpp                    # 全部 ADE 公共头文件（单层，不按域分子目录）
src/ade/runtime/*.cpp                    # 运行时 .cpp（与 include/ 并列）

src/ade/tools/                           # 训练/数据生成工具源码
src/ade/bench/                           # 性能基准代码
```

**编码规范（摘要）**：

- **头文件**：类型与 API 声明；体量小的 `inline` 辅助函数（如 `algorithm_id_to_string`）可保留在头内；数据结构的轻量 `to_string`/`to_json` 可保留在头内或后续再下沉。
- **源文件**：重逻辑放在 `runtime/*.cpp` 或日后的 `features/*.cpp`、`models/*.cpp`；**禁止**在各域下建 `*/src/`。头文件只放在 **`ade/include/`** 单层目录。
- **依赖方向**：`runtime` → `features` + `models`；`features` 与 `models` 互不依赖（演化算法若需决策类型，仅通过 `DecisionEngine.hpp` 暴露的枚举/结构，避免循环 include）。
- **`include/`** 下不建子目录；以清晰文件名区分模块职责。

---

## 📋 目录

1. [背景与目标](#1-背景与目标)
2. [需求收集过程](#2-需求收集过程)
3. [已确认的决策清单](#3-已确认的决策清单)
4. [技术方案对比](#4-技术方案对比)
5. [详细设计规格](#5-详细设计规格)
6. [待定事项与后续计划](#6-待定事项与后续计划)
7. [附录：关键问题与回答](#7-附录关键问题与回答)

---

## 1. 背景与目标

### 1.1 模块定位
在压缩工具中实现**智能算法自动选择**功能，对应现有的 `AlgorithmType.AUTO` 模式。

### 1.2 核心目标
1. **自动化决策**: 根据文件特征自动选择最优压缩算法及参数配置
2. **持续学习**: 在后台静默收集压缩数据，不断优化决策模型
3. **可扩展性**: 方便未来接入新的压缩算法（包括多媒体专项算法）
4. **透明可控**: 决策过程可解释，用户可查看详情或覆盖选择

### 1.3 技术愿景
```
用户选择 "AUTO" → 系统流式扫描文件 → 提取30+维特征 
→ ML模型预测最优(算法+参数) → 执行压缩 → 记录结果到训练集 
→ 后台定期重训练模型 → 决策准确率持续提升
```

---

## 2. 需求收集过程

### 2.1 讨论规则（已建立）
1. ✅ 探索模式：不急于写代码或给出完整方案
2. ✅ 先提问厘清边界，再给方案
3. ✅ 要求用具体例子说明模糊点
4. ✅ 提供2-3个可行方案 + 推荐理由
5. ✅ 模拟核心场景和异常场景检验方案
6. ✅ 最终输出到 `docs/decisions.md` 并生成开发规格

### 2.2 问题组总览

#### 🔬 Q1: 决策器智能程度与范围
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q1.1 智能层级 | **B: ML级** | 随机森林分类选算法 + 神经网络回归调参数 |
| Q1.2 自动训练触发 | **A: 完全自动** | 静默收集数据，后台原子性写入 |
| Q1.3 特征粒度 | **C: 细粒度(30+维)** | 含N-gram、局部熵、特殊结构检测 |

**补充说明 (Q1.3)**:
- 不仅需要基础统计（大小、类型、熵值）
- 还需要深度分析策略（待调研论文）:
  - 文件指纹识别 (File Fingerprinting)
  - 压缩算法自适应选择 (Adaptive Compression)
  - 超参数优化 (Hyperparameter Optimization)
  - 多媒体专项压缩预处理 (Domain-Specific Compression)
- **未来扩展方向**: 图片/视频/音频无损压缩算法、字典增强压缩、各种预处理辅助选项

#### ⚡ Q2: 性能与用户体验
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q2.1 决策延迟容忍度 | **逐文件扫描+允许暂停** | 状态提示"正在扫描..." |
| Q2.2 决策可解释性 | **A极简+B底栏+C右键详情** | 状态栏显示算法，右键看详细分析 |

**关于大文件扫描的关键讨论**:
- ❌ 不采用全量预扫描（太慢）
- ✅ 采用 **C: 流式渐进式扫描**
  - 边读取边更新特征（滑动窗口统计）
  - 可在前10%数据时就做出初步决策
  - 后续数据用于微调或验证
  - **优势**: 无感知延迟，特征逐步精确

**关于速度权衡的结论**:
- ✅ 必须考虑算法速度！这是多目标优化问题
- 建议: 将"速度权重"作为可选配置项
- 示例场景:
  - 小文件(<1MB): 追求压缩率优先
  - 大文件(>100MB): 追求速度优先

#### 🏗️ Q3: 架构与扩展性
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q3.1 新算法接入成本 | **标准成本** | 完整实现ICompressor + 特征适配器 |
| Q3.2 训练数据存储 | **本地存储，不考虑隐私** | 直接存SQLite，无需匿名化 |

#### 🧠 Q4: 深度学习角色定位
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q4.1 DL具体作用 | **B: 双阶段模型** | Stage1分类选算法 + Stage2回归调参数 |
| Q4.2 论文调研范围 | **全部方向，BFS后DFS** | 先广度了解，再深入阅读 |

**Q4.1 双阶段模型详解**:
```
Stage 1 - 分类器 (随机森林/MLP):
  输入: 文件特征向量
  输出: 算法概率分布 [LZMine=0.7, Deflate=0.2, Brotli=0.1]
  
Stage 2 - 回归器 (神经网络):
  输入: [文件特征, 选定算法ID]
  输出: 最优参数值 {window_size: 8192, chain_length: 256, use_flag_encoding: 1,
                    use_3hfmtree: 0, huffman_chunk_bits: 8,
                    huffman_offset_chunk_bits: 0, huffman_length_chunk_bits: 0}
```

后两项为 `0` 时表示未单独指定 offset/length 槽宽、沿用 `huffman_chunk_bits`（语义见 `flate-encoding-design.md`）。

**优势**: 模块化，Stage1可解释性强

**与 Flate 后端、3HfMTree 的衔接（ADE 只约定「学什麽、存什麽」）**

实现侧在 **LZSS / LZDP / DPFlate**（以及可选走 3HfM 内核的 **Deflate** 路径）上采用与 `docs/design/flate-encoding-design.md` 一致的 **编码方案 × 树策略** 组合矩阵。对 ADE 而言，除已有的 **`use_flag_encoding`**（flag / non-flag）外，**树策略与多级槽**进入同一套 **Stage2 可预测、可探索、`params_used` 可落盘** 的子空间，而不在本篇重复比特级规格：

| 键名（示例，与 GUI / 管线快照对齐） | 含义（摘要） |
| --- | --- |
| `use_3hfmtree` | `0` = FLATE 风格两树（与标准 Inflate 兼容的解压路径）；`1` = **3HfMTree**（字面量 + offset + length 三树，**一树多查**多级槽，见 flate 文档 §3.2） |
| `huffman_chunk_bits` | offset/length **共用**默认槽宽 `w`（实现里可对应统一 `huffman_chunk_bits`） |
| `huffman_offset_chunk_bits` | 非 `0` 时覆盖 offset 侧槽宽 `w_ob`；`0` 表示不覆盖、沿用统一槽宽 |
| `huffman_length_chunk_bits` | 非 `0` 时覆盖 length 侧槽宽 `w_lb`；`0` 同上 |

**K／w 对偶、末段掩码、组合矩阵** 等细节一律以 **`flate-encoding-design.md`** 为准；ADE 设计仅要求：选中算法后，Stage2/EA/静默探索在**预算内**可扰动上述离散/小整数维度，且训练 JSON 中 **`algorithm_used` + `params_used` 能无损回放** 本次压缩配置。

**Q4.2 论文调研四大方向**:
1. **文件特征提取** (File Fingerprinting)
   - 关键词: byte frequency analysis, n-gram classification, content-based identification
   - 目标: 用最少数据判断文件类型和可压缩性
   
2. **压缩算法选择** (Algorithm Selection)
   - 关键词: adaptive compression, machine learning for compression, auto-tuning
   - 目标: 业界最佳实践 (7-Zip, Zstandard策略)
   
3. **超参数优化** (HPO)
   - 关键词: Bayesian optimization LZ77, neural architecture search compression
   - 目标: 高效搜索参数空间
   
4. **多媒体专项压缩** (Domain-Specific)
   - 关键词: pre-processing text compression, dictionary HTML, lossless image/audio codec
   - 目标: 针对特定类型的专用算法和预处理

#### 💾 Q5: 实时性与崩溃恢复
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q5.1 数据安全级别 | **原子性写入+丢弃不完整** | SQLite WAL模式，单条不完整则丢弃 |
| Q5.2 暂停功能完善 | **双模式中断** | 第1次软中断 → 第2次硬中断 |

**Q5.2 暂停功能详细设计**:
```
┌─────────────────────────────────────────────┐
│ 初始状态: ▶ 开始压缩                        │
│                                             │
│ 点击第1次: ⏸️ 暂停 (软中断)                │
│   → 完成当前文件后停止                      │
│   → 显示:"正在完成当前文件 (2/10)..."       │
│                                             │
│ 软中断过程中点击第2次: ⏹️ 停止 (硬中断)    │
│   → 立即终止当前文件的压缩                  │
│   → 已完成文件保留，当前文件标记为未完成    │
│                                             │
│ 图标状态转换:                               │
│   ▶ → ⏸️ → ⏹️                             │
└─────────────────────────────────────────────┘
```

#### 🎨 Q6: UI/UX交互细节
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q6.1 决策可视化程度 | **A极简+底栏新列+右键详情** | 新列显示实际使用的算法 |
| Q6.2 训练反馈循环 | **C静默进化+高级报告页** | 设置页可查看统计信息 |

**Q6.1 UI设计确认**:
- **状态栏**: 显示正在分析的文件名和进度
- **表格新列**: "算法"列（居中，自适应宽度）
  - 压缩完成后填充实际使用的算法名
  - AUTO模式下显示推荐算法或实际算法
- **右键菜单**: 新增"查看决策详情"
- **详情面板**: 显示文件特征、算法评分排名、使用参数、预期vs实际对比

**Q6.2 数据记录范围**:
- ✅ **记录所有压缩行为**
- 包括用户手动选择的算法/参数
- 用于训练时作为真实反馈样本

---

### 2.3 第三轮问题（技术实现细节）

### 2.5 第四轮问题（核心架构决策）

#### ⚙️ Q7: 技术架构与依赖 (修订版)
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q7.1 ML/DL库选择 | **C++原生实现** | RF/EA/简单NN用C++，仅复杂矩阵运算考虑GPU |
| Q7.2 流式提取策略 | **单次遍历同时提取** | 计算熵时同步提取所有全局+局部特征 |
| Q7.3 参数空间搜索 | **C: 学习排序(Learning to Rank)** | **+ EA进化算法手段** |

**Q7.1 C++原生实现详解**:

用户提出核心问题: **"对于随机森林和进化算法。还有简单的神经网络层。可以直接用c加加实现吧。除了神经网络用到矩阵运算。还有可能用到一点gpu加速"**

**答案: 完全正确！** 

```
C++实现的优势分析:
┌─────────────────────┬──────────────────────────────────────────┐
│ 算法组件            │ 实现复杂度 & 性能考量                    │
├─────────────────────┼──────────────────────────────────────────┤
│ 随机森林 (RF)       │ ★☆☆ 纯逻辑运算，C++极易实现             │
│   - 决策树构建      │   递归分裂，无需特殊库                   │
│   - 集成投票        │   简单统计，性能极佳                     │
│                     │                                          │
│ 进化算法 (EA)       │ ★★☆ 种群管理+遗传操作，C++天然适合      │
│   - GA/PSO/CMA-ES   │   向量运算可用std::vector或Eigen         │
│   - 适应度评估      │   可并行化，多线程友好                   │
│                     │                                          │
│ 简单神经网络        │ ★★☆ 前向传播，基础矩阵运算               │
│   - 全连接层(FC)    │   可用Eigen库或手写矩阵乘法              │
│   - 激活函数        │   简单数学运算                           │
│   - 反向传播(BP)    │   链式求导，教学级实现                   │
│                     │                                          │
│ 复杂深度学习        │ ★★★ CNN/RNN/Transformer                  │
│   - 卷积/注意力机制 │   建议用PyTorch/TensorFlow + GPU加速     │
│   - 大规模训练      │   或ONNX Runtime推理引擎                 │
└─────────────────────┴──────────────────────────────────────────┘

推荐技术栈:
✅ 核心ML: C++17标准库 + Eigen3(轻量级线性代数)
✅ 推理加速: 可选CUDA/cuDNN(如需GPU)
✅ 模型训练: 可选Python PyTorch(离线训练) → 导出ONNX → C++加载
✅ 数据存储: 自定义二进制格式(见Q8.3)
```

**架构优势**:
- 🚀 **性能**: C++原生运行，无Python GIL限制
- 🔧 **可控**: 完全透明的算法实现，便于调试和优化
- 📦 **部署**: 单一可执行文件，无Python依赖
- 🎯 **演示**: 原生数据结构可直接可视化，适合学术汇报

**Q7.2 单次遍历特征提取 (重大修正)**:

用户洞察: **"对于文件熵这种特征，不每个字节看一遍怎么提取特征啊我问你,既然都全看过，所谓的局部特征不也可以在这个过程提取吗"**

**这个观点完全正确！之前的设计过于复杂化了。**

```cpp
// 修正后的特征提取策略: 单次遍历 = O(n) 时间复杂度
class SinglePassFeatureExtractor {
public:
    void processChunk(const uint8_t* data, size_t size, size_t globalOffset) {
        for (size_t i = 0; i < size; i++) {
            byte = data[i];
            pos = globalOffset + i;
            
            // ===== 1. 全局统计特征 (增量更新) =====
            updateGlobalStatistics(byte);  // 熵值、直方图、均值等
            
            // ===== 2. 局部特征 (滑动窗口) =====
            updateLocalFeatures(byte, pos);  // 局部熵、N-gram、重复模式
            
            // ===== 3. 结构特征 (位置敏感) =====
            updateStructuralFeatures(byte, pos);  // 文件头、周期性、块边界
        }
    }
    
private:
    // 全局统计状态
    GlobalStats global_stats_;      // 1024 bytes左右
    
    // 局部特征状态 (滑动窗口)
    SlidingWindow local_window_;    // 几KB
    
    // 结构检测状态
    StructuralDetector struct_det_; // 几百bytes
};

// 内存占用估算:
// 总计 < 10KB (即使处理TB级文件!)
// vs 旧方案: 需要多次扫描或大量采样缓冲区
```

**三类特征的统一提取逻辑**:

```cpp
void SinglePassFeatureExtractor::updateGlobalStatistics(uint8_t byte) {
    // 1. 字节频率表 (用于计算熵值)
    byte_histogram_[byte]++;
    total_bytes_++;
    
    // 2. 在线更新熵值 (Welford's algorithm)
    // H(X) = -Σ p(x) * log2(p(x))
    // 可以增量维护，不需要存储所有字节!
    
    // 3. 游程编码统计 (重复性检测)
    if (byte == last_byte_) {
        current_run_length_++;
    } else {
        updateRunLengthDistribution(current_run_length_);
        current_run_length_ = 1;
        last_byte_ = byte;
    }
}

void SinglePassFeatureExtractor::updateLocalFeatures(uint8_t byte, size_t pos) {
    // 1. N-gram更新 (bigram/trigram)
    bigram_[last_byte_][byte]++;          // 256x256 = 64KB (可压缩)
    trigram_[prev2_][last_byte_][byte]++; // 可选，内存较大
    
    // 2. 滑动窗口局部熵 (检测局部变化)
    window_.push_back(byte);
    if (window_.size() > WINDOW_SIZE) {
        window_.pop_front();
    }
    local_entropy_ = calculateEntropy(window_);  // 小窗口，快速计算
    
    // 3. 周期性检测 (自相关)
    if (pos % PERIOD_CHECK_INTERVAL == 0) {
        checkPeriodicity(pos);
    }
}

void SinglePassFeatureExtractor::updateStructuralFeatures(uint8_t byte, size_t pos) {
    // 1. 文件头签名匹配 (前4KB)
    if (pos < HEADER_SIZE) {
        header_buffer_[pos] = byte;
        if (pos == HEADER_SIZE - 1) {
            detectFileType(header_buffer_);
        }
    }
    
    // 2. 块边界检测 (每64KB)
    if (pos % BLOCK_SIZE == 0) {
        recordBlockFingerprint(pos);
    }
    
    // 3. 高频序列检测 (如0x0000, 0xFFFF)
    detectSpecialSequences(byte, pos);
}
```

**性能对比**:
```
旧方案 (多次扫描/采样):
  时间: O(n) * 扫描次数 (通常2-3次)
  内存: 采样缓冲区 (数MB)
  
新方案 (单次遍历):
  时间: O(n) (仅需1次遍历!)
  内存: < 10KB (固定大小的统计结构)

速度提升: 2-3倍 ✅
内存降低: 100倍以上 ✅
```

**Q7.3 种群与样本统一管理**:

用户提出创新思路: **"我在想每个的样本可不可以都作为一个个体加入到该算法的种群，然后实际训练可以采样也可以全部用于更新"**

**这是一个非常优雅的设计！将进化算法的训练数据管理统一起来。**

```cpp
// 统一种群/样本架构设计
template<typename Individual>
class EvolutionaryPopulation {
public:
    // ===== 核心接口 =====
    
    void addIndividual(const Individual& ind) {
        // 添加新个体到种群 (每次压缩完成后调用)
        all_individuals_.push_back(ind);
        
        // 持久化到硬盘 (异步)
        persistToStorage(ind);
        
        // 如果内存种群未满，直接加入活跃种群
        if (active_population_.size() < max_active_size_) {
            active_population_.push_back(ind);
        } else {
            // 否则触发自然选择 (淘汰最差个体)
            performSelection();
        }
    }
    
    // ===== 训练时使用 =====
    
    std::vector<Individual> getTrainingBatch(size_t batch_size) {
        // 方式A: 随机采样 (SGD风格)
        return randomSample(batch_size);
        
        // 方式B: 使用全部活跃种群 (批量梯度下降)
        // return active_population_;
        
        // 方式C: 加权采样 (适应度高的个体更可能被选中)
        // return weightedSample(batch_size);
    }
    
    std::vector<Individual> getFullPopulation() const {
        return active_population_;
    }
    
    // ===== 智能初始化 (启动时) =====
    
    void initializeFromStorage(InitializationStrategy strategy) {
        // 从硬盘加载历史个体
        auto all_history = loadAllFromStorage();
        
        switch (strategy) {
            case RANDOM_SAMPLE:
                // 随机选择N个多样化个体
                active_population_ = selectDiverseSample(
                    all_history, max_active_size_
                );
                break;
                
            case RECENT_PRIORITY:
                // 优先选择最近的个体 (反映当前用户习惯)
                active_population_ = selectRecentSamples(
                    all_history, max_active_size_
                );
                break;
                
            case FITNESS_BASED:
                // 选择适应度最高的个体 (精英保留)
                active_population_ = selectEliteIndividuals(
                    all_history, max_active_size_
                );
                break;
                
            case DIVERSITY_BALANCED:
                // 平衡多样性 + 质量 (推荐!)
                active_population_ = balancedSelection(
                    all_history, max_active_size_
                );
                break;
        }
    }
    
private:
    // ===== 存储分层 =====
    std::vector<Individual> active_population_;   // 内存中的活跃种群 (有限大小)
    std::vector<Individual> all_individuals_;      // 完整历史记录 (可选，或仅存索引)
    
    PersistentStorage storage_;                    // 硬盘持久化层
    
    size_t max_active_size_ = 10000;               // 内存上限 (可配置)
};
```

**用户关于数据保留的最终决定**: **"肯定要原封不动保留，选A,这是我们种群的一员吧"**

完整保留策略:
```
✅ 每个压缩记录都是"种群的一员"
✅ 内存中种群大小有限制 (如10,000个活跃个体)
✅ 硬盘存储几乎无限制 (单个记录 ~500bytes, 100万条 ≈ 500MB)
✅ 启动程序时智能初始化:
   - 从硬盘中选择性加载活跃种群
   - 选择标准: 多样性 + 时效性 + 质量平衡
   - 初始化时间: < 1秒 (索引查询)
```

### 2.6 第五轮问题 (务实的架构修正)

> **⚠️ 重要原则**: 本项目**严禁吹牛逼、脱离实际和欺骗行为**。所有技术方案必须基于实际可行性，避免过度承诺。

#### ⚙️ Q7: 技术架构 (最终务实版)
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q7.1 ML/DL实现 | **C++原生 + 务实评估** | RF/EA/简单NN用C++，GPU仅作为高级可配置选项 |
| Q7.2 特征提取 | **单次遍历统一提取** | O(n)时间，实际内存占用需测试验证 |
| Q7.3 参数搜索 | **多算法可选 + 可配置** | EA算法(GA/PSO/CMA-ES)全部实现，用户在高级设置中选择 |

**Q7.1 C++实现的务实评估**:

```
实际可实现性分析 (基于现有代码库):

✅ 完全可行 (已有基础):
   - 随机森林: 纯逻辑运算，递归分裂+投票统计
     * 复杂度: O(n_features × n_samples × log(n_samples))
     * 内存: 树结构 + 特征缓存
     * 参考: 已有Huffman树实现经验
   
   - 进化算法(GA/PSO): 种群管理+遗传操作
     * 复杂度: O(population_size × generations × evaluation_cost)
     * 内存: 种群数组 + 适应度数组
     * 并行化: 适应度评估可多线程

⚠️ 需要轻量级依赖:
   - 简单神经网络(全连接层):
     * 矩阵运算: Eigen3库 (~1MB头文件 only)
     * 或手写矩阵乘法 (对于小网络完全可行)
     * 反向传播: 链式求导，教学级实现

❌ 不建议C++原生实现:
   - CNN/RNN/Transformer等复杂架构
   - 建议方案: Python PyTorch训练 → ONNX导出 → C++ ONNX Runtime推理
   - 或: 直接使用Python端训练，C++只做推理
```

**GPU/CUDA加速的实际定位**:

```cpp
// GPU加速应该作为"高级选项"，而非默认功能
struct AdvancedAlgorithmConfig {
    // ===== 基础配置 (默认可见) =====
    bool use_auto_mode = true;
    
    // ===== 高级配置 (高级面板中) =====
    
    // GPU加速选项
    struct GpuConfig {
        bool enable_gpu = false;           // 默认关闭
        std::string device_type = "cpu";   // "cpu" / "cuda" / "opencl"
        int gpu_device_id = 0;             // 多GPU时选择哪个
        size_t batch_size = 32;            // GPU批处理大小
        
        // 自动检测GPU可用性
        bool auto_detect = true;           // 启动时自动检测
    } gpu_config;
    
    // EA算法选择
    enum class EAAlgorithm {
        GENETIC_ALGORITHM,      // GA - 遗传算法
        PARTICLE_SWARM,         // PSO - 粒子群优化
        CMA_ES,                 // CMA-ES - 协方差矩阵自适应
        NONE                    // 不使用EA (纯NN预测)
    };
    EAAlgorithm ea_algorithm = EAAlgorithm::GENETIC_ALGORITHM;
    
    // 种群管理参数
    struct PopulationConfig {
        size_t max_active_population = 10000;   // 内存中最大种群数
        double elite_ratio = 0.1;               // 精英保留比例 (10%)
        double exploration_ratio = 0.3;         // 探索样本比例 (30%)
        bool consider_recency = false;          // 是否考虑时效性 (默认关闭)
        
        // 初始化策略
        enum class InitStrategy {
            DIVERSITY_BALANCED,    // 平衡多样性+质量 (推荐)
            ELITE_ONLY,            // 仅精英
            RECENT_PRIORITY,       // 最近优先
            RANDOM_SAMPLE          // 随机采样
        };
        InitStrategy init_strategy = InitStrategy::DIVERSITY_BALANCED;
    } population_config;
};
```

**用户明确要求**: "cuda和gpu的可选加速要应用到高级选项的算法配置里面"

这意味着:
- ❌ 不是简单的"可选加速"标签
- ✅ 要整合到完整的**高级算法配置系统**
- ✅ 用户可以在UI中选择是否启用、选择设备、调整参数
- ✅ 默认情况下使用CPU（保证兼容性）

#### 💾 Q8: 数据持久化 (务实修正)

**用户关键决策**: 
- ❌ **不再使用自定义二进制格式** (不利于格式变更)
- ✅ **改用JSON格式** (对硬盘存储宽容)
- ✅ **整合到现有数据模型** (FileRecord或C++ API封装)

**Q8.3 JSON格式 + 数据模型整合方案**:

```python
# 方案A: 扩展现有 FileRecord (Python端)
class FileRecord(Record):
    # ... 现有字段 ...
    
    # ===== 新增: ML训练相关字段 =====
    
    # 特征向量 (30维)
    feature_vector: list[float] = []  # 或使用FeatureVector dataclass
    
    # 压缩决策记录 (用于训练)
    ml_record: MLTrainingRecord | None = None


@dataclass
class MLTrainingRecord:
    """单条ML训练记录 (JSON序列化)"""
    record_id: str                    # UUID
    
    # 文件特征 (输入)
    features: dict                    # 30维特征字典 {"entropy": 7.2, ...}
    
    # 算法与参数 (决策)
    algorithm_used: str               # "LZMINE", "DEFLATE", etc.
    params_used: dict                 # {"window_size": 4096, "use_flag_encoding": 1,
                                      #  "use_3hfmtree": 0, "huffman_chunk_bits": 8, ...}
    
    # 结果 (输出)
    compression_ratio: float
    compression_time_ms: float
    output_size_bytes: int
    success: bool
    
    # 训练元数据
    sample_weight: float              # 1.0 (成功) / 0.5 (有错误)
    app_version: str                  # 用于兼容性检查
    
    def to_json(self) -> str:
        """序列化为JSON"""
        return json.dumps(asdict(self), indent=2)
    
    @classmethod
    def from_json(cls, json_str: str) -> 'MLTrainingRecord':
        """从JSON反序列化"""
        data = json.loads(json_str)
        return cls(**data)


# 方案B: C++封装API + Python包装 (推荐用于高性能场景)
"""
C++核心层 (ml_core.dll):
  - BinaryRecordManager: 二进制高效读写 (内部实现)
  - EvolutionaryPopulation<T>: 种群管理
  - FeatureExtractor: 单次遍历特征提取
  
Python包装层 (ml_api.py):
  - PyMLRecordManager: 调用C++ API
  - 提供Pythonic接口
  - 自动处理JSON转换 (如果需要调试/查看)

优势:
  - C++保证性能 (特征提取、EA计算)
  - Python保证易用性 (UI集成、调试)
  - 存储格式灵活 (C++内部可用二进制，对外暴露JSON)
"""

# 实际推荐的混合方案:
class HybridStorageManager:
    """
    混合存储策略:
    - 运行时: C++管理内存中的活跃种群 (高性能)
    - 持久化: JSON文件存储 (易于调试和版本控制)
    - API: C++导出函数，Python调用
    """
    
    def __init__(self):
        self.cpp_core = CPPMLCore()           # C++核心实例
        self.json_storage = JSONStorage()      # JSON持久化层
    
    def save_record(self, record: MLTrainingRecord):
        # 1. 传递给C++加入活跃种群
        self.cpp_core.add_individual(record.to_binary())
        
        # 2. 异步保存到JSON文件 (后台线程)
        self.json_storage.append_record(record.to_json())
    
    def load_population(self, config: PopulationConfig):
        # 1. 从JSON加载历史记录
        all_records = self.json_storage.load_all()
        
        # 2. 调用C++智能初始化
        binary_data = [r.to_binary() for r in all_records]
        active_pop = self.cpp_core.initialize_population(
            binary_data, 
            config.to_cpp_struct()
        )
        
        return active_pop
```

**JSON格式的实际存储示例**:

```json
{
  "record_id": "uuid-v4-string",
  "features": {
    "size_mb": 2.15,
    "extension_category": 1,
    "entropy_bits": 7.23,
    "mean_byte_value": 127.5,
    "std_byte_value": 64.2,
    "unique_byte_ratio": 0.85,
    "repetitiveness_score": 0.12,
    "compressibility_estimate": 0.65,
    "long_run_ratio": 0.08,
    "periodicity_score": 0.03,
    "detected_type": 2,
    "header_signature_match": 0.95,
    "local_entropy_variance": 0.42,
    "ascii_ratio": 0.78,
    "printable_ratio": 0.82,
    "high_bit_usage": 0.15,
    "bigram_entropy": 7.18,
    "trigram_entropy": 6.95,
    "dominant_ngram_concentration": 0.25,
    "dictionary_word_ratio": 0.45,
    "code_keyword_density": 0.02,
    "markup_tag_density": 0.01,
    "numeric_pattern_ratio": 0.08,
    "whitespace_distribution": 0.62,
    "autocorrelation_peak": 0.15,
    "block_similarity": 0.22,
    "entropy_curve_slope": -0.05,
    "special_sequence_freq": 0.03,
    "byte_run_length_stats": [2.5, 1.8]
  },
  "algorithm_used": "LZMINE",
  "params_used": {
    "search_size": 4096,
    "lookahead_size": 256,
    "min_match": 3,
    "dp_depth": 3,
    "use_flag_encoding": 1,
    "use_3hfmtree": 0,
    "huffman_chunk_bits": 8,
    "huffman_offset_chunk_bits": 0,
    "huffman_length_chunk_bits": 0
  },
  "compression_ratio": 0.65,
  "compression_time_ms": 120.5,
  "output_size_bytes": 1392640,
  "success": true,
  "sample_weight": 1.0,
  "app_version": "1.0.0"
}
```

**存储空间估算 (JSON vs 二进制)**:

```
单条记录:
  - JSON格式: ~800 bytes - 1.2 KB (人类可读，便于调试)
  - 二进制格式: ~200 bytes (紧凑但难以调试)

100万条记录:
  - JSON: ~800 MB - 1.2 GB (现代硬盘完全可以接受!)
  - 二进制: ~190 MB

用户决策: "对硬盘存储大小宽容一点"
→ 选择JSON ✅ (灵活性 > 存储空间)
```

#### 🔧 Q9: 高级配置选项详细设计

**用户要求**: 所有高级参数都应该可在UI中配置

```python
# 高级算法配置的数据模型
@dataclass  
class AutoModeAdvancedConfig:
    """
    AUTO模式的高级配置
    用户可在"设置 → 高级 → 算法配置 → AUTO" 中调整这些参数
    """
    
    # ===== Stage 1: 算法分类器配置 =====
    
    classifier_config: ClassifierConfig
    
    # ===== Stage 2: 参数优化器配置 =====
    
    optimizer_config: OptimizerConfig
    
    # ===== 种群管理与训练配置 =====
    
    population_config: PopulationConfig
    
    # ===== 特征提取配置 =====
    
    feature_config: FeatureConfig


@dataclass
class ClassifierConfig:
    """随机森林分类器配置"""
    n_estimators: int = 100                # 树的数量 (10-1000)
    max_depth: int = 20                    # 最大深度 (3-50)
    min_samples_split: int = 2             # 分裂最小样本数
    min_samples_leaf: int = 1              # 叶节点最小样本数
    feature_sampling_ratio: float = 0.7    # 特征采样比例 (sqrt/log2/all)


@dataclass
class OptimizerConfig:
    """Stage 2 参数优化器配置"""
    
    # 神经网络快速筛选
    nn_enabled: bool = True
    nn_hidden_layers: list[int] = field(default_factory=lambda: [64, 32])  # 隐藏层大小
    nn_learning_rate: float = 0.001
    nn_top_k_predictions: int = 10          # 返回Top-K候选参数
    
    # 进化算法精细搜索
    ea_algorithm: str = "GA"                # "GA" / "PSO" / "CMA-ES" / "NONE"
    ea_generations: int = 30                # 进化代数 (10-100)
    ea_population_size: int = 20            # 种群大小 (10-100)
    ea_mutation_rate: float = 0.1           # 变异概率 (0.01-0.5)
    ea_crossover_rate: float = 0.8          # 交叉概率 (0.5-0.95)
    
    # 位流后端探索：flag 编码 + 树策略 (FLATE vs 3HfMTree) + 槽宽族
    # （与 flate-encoding-design 中组合矩阵一致；实现可分期打开各子开关）
    encoding_scheme_exploration: bool = True   # 是否探索 use_flag_encoding 等切换
    encoding_scheme_strategy: str = "adaptive" # "fixed" / "adaptive" / "random"
    tree_strategy_exploration: bool = False      # 是否在适用算法上探索 use_3hfmtree（成本高，默认关）
    huffman_slot_exploration: bool = False       # 是否在 3HfM 路径上探索 huffman_*_chunk_bits（默认关）
    
    # 时间限制
    max_optimization_time_ms: int = 500     # 最大优化时间 (100-2000ms)


@dataclass
class PopulationConfig:
    """种群管理配置 (来自用户要求)"""
    max_active_population: int = 10000      # 内存中最大活跃个体数
    elite_ratio: float = 0.1                # 精英保留比例 (0.05-0.3)
    exploration_ratio: float = 0.3          # 探索样本比例 (0.1-0.5)
    consider_recency: bool = False          # 是否考虑时效性 (默认关闭)
    
    init_strategy: str = "DIVERSITY_BALANCED"  # 初始化策略
    
    # 训练触发条件
    min_new_samples_for_retrain: int = 100  # 至少多少新样本才重训练
    retrain_interval_minutes: int = 60      # 重训练间隔 (分钟)


@dataclass
class FeatureConfig:
    """特征提取配置"""
    extract_during_compression: bool = True  # 是否在压缩时同步提取 (单次遍历)
    
    # 特征维度选择 (可选禁用某些高成本特征)
    enable_ngram_features: bool = True       # N-gram特征 (bigram/trigram)
    enable_structural_features: bool = True  # 结构特征 (文件头、周期性等)
    enable_advanced_features: bool = False   # 高级特征 (自相关、块相似度等)
    
    # 采样配置 (如果不用单次遍历)
    sampling_rate: float = 0.1               # 采样率 (0.01-1.0)
    window_size: int = 4096                  # 滑动窗口大小
```

**UI布局设计 (高级设置页面)**:

```
┌─────────────────────────────────────────────────────────────┐
│ 设置 → 高级 → 算法配置 → AUTO模式                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│ 📊 Stage 1: 算法分类器 (随机森林)                           │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 树的数量:     [100]   (10-1000)                         │ │
│ │ 最大深度:     [20]    (3-50)                             │ │
│ │ 特征采样:     [70%]   (sqrt / log2 / all)               │ │
│ └─────────────────────────────────────────────────────────┘ │
│                                                             │
│ 🧠 Stage 2: 参数优化器                                      │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ ☑ 启用神经网络快速筛选                                  │ │
│ │   隐藏层: [64, 32]   Top-K: [10]                        │ │
│ │                                                         │ │
│ │ 进化算法: [GA ▼]  (GA / PSO / CMA-ES / 关闭)           │ │
│ │   代数: [30]   种群大小: [20]                            │ │
│ │   变异率: [0.1]   交叉率: [0.8]                          │ │
│ │                                                         │ │
│ │ 位流探索: [☑] flag 编码等 (LZSS/LZDP/DPFlate…)        │ │
│ │   [☐] 树策略 3HfMTree↔FLATE（可选，算力敏感）          │ │
│ │   [☐] 3HfM 槽宽 huffman_*_chunk_bits（可选）           │ │
│ │   策略: [自适应 ▼] (固定/自适应/随机扰动)               │ │
│ │                                                         │ │
│ │ ⏱️ 最大优化时间: [500] ms                                │ │
│ └─────────────────────────────────────────────────────────┘ │
│                                                             │
│ 🎯 GPU加速 (可选)                                          │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ ☐ 启用GPU加速                                           │ │
│ │ 设备类型: [自动检测 ▼]  (CPU / CUDA / OpenCL)            │ │
│ │ GPU设备ID: [0]   批处理大小: [32]                        │ │
│ └─────────────────────────────────────────────────────────┘ │
│                                                             │
│ 🧬 种群管理                                                 │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 最大活跃种群: [10,000]  (1000-100000)                   │ │
│ │ 精英比例: [10%]  (5%-30%)                                │ │
│ │ 探索样本比例: [30%]  (10%-50%)                            │ │
│ │                                                         │ │
│ │ 初始化策略: [平衡多样性+质量 ▼]                          │ │
│ │   (平衡多样性 / 仅精英 / 最近优先 / 随机采样)            │ │
│ │                                                         │ │
│ │ ☐ 考虑样本时效性 (默认关闭)                              │ │
│ │                                                         │ │
│ │ 重训练触发:                                             │ │
│ │   最少新样本: [100] 条                                   │ │
│ │   重训练间隔: [60] 分钟                                  │ │
│ └─────────────────────────────────────────────────────────┘ │
│                                                             │
│ 🔍 特征提取                                                │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ ☑ 压缩时同步提取 (单次遍历，推荐)                       │ │
│ │ ☑ 启用N-gram特征                                       │ │
│ │ ☑ 启用结构特征                                         │ │
│ │ ☐ 启用高级特征 (自相关等，较慢)                         │ │
│ └─────────────────────────────────────────────────────────┘ │
│                                                             │
│                    [恢复默认]  [应用]  [取消]                 │
└─────────────────────────────────────────────────────────────┘
```

#### 🔄 Q8: 数据收集完整性
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q8.1 数据Schema | **简化版** | 去掉path/timestamp等，聚焦本质 |
| Q8.2 不完整数据处理 | **混合模式** | 成功权重1.0, 错误权重0.5, 中断丢弃 |

**Q8.1 简化后的Schema（最终版）**:
```python
class CompressionRecord:
    """单条压缩训练记录"""
    
    # ===== 核心标识 =====
    record_id: str                    # UUID
    
    # ===== 文件特征（输入侧 - 已解耦）=====
    features: FeatureVector           # 30+维特征向量（自定义数据结构）
    
    # 算法与参数（决策侧）
    algorithm_used: AlgorithmType     # 实际使用的算法
    params_used: dict                 # 实际参数字典；含 flate 后端时见 use_flag_encoding /
                                      # use_3hfmtree / huffman_* （与 flate-encoding-design 对齐）
    
    # ===== 结果（输出侧）=====
    compression_ratio: float          # 实际压缩率
    compression_time_ms: float        # 实际耗时(ms)
    output_size_bytes: int            # 输出大小
    success: bool                     # 是否成功完成
    
    # ===== 训练元数据 =====
    sample_weight: float              # 样本权重 (成功=1.0, 有错误=0.5)
    app_version: str                  # 程序版本号（用于兼容性检查）
```

**用户明确删除的字段**:
- ❌ file_path (用特征向量解耦，保护隐私且通用化)
- ❌ timestamp (对训练无意义)
- ❌ selection_mode (不区分手动/AUTO)
- ❌ auto_recommended_algorithm (不关心推荐过程)
- ❌ user_overridden (同上)
- ❌ error_message (用success布尔值替代)
- ❌ model_version (不影响压缩质量本质)

**保留的核心哲学**:
> "本质压缩质量的是算法及其参数，其他都是噪音"

**Q8.2 混合模式详细规则**:
```
✅ 完全成功 → weight=1.0 (正面样本)
⚠️ 有错误但产生部分结果 → weight=0.5 (负面样本，有价值!)
❌ 用户中途取消(暂停) → 丢弃 (可作为负面样本研究用户耐心)
❌ 程序崩溃残留 → 丢弃 (启动时清理未完成事务)
```

**用户洞察**: "之前好像没有考虑过负面样本" → 这是个重要发现！
- 负面样本可以帮助模型避免推荐会导致超时/失败的参数组合

#### 🎨 Q9: UI最后确认
| 问题 | 用户选择 | 核心要点 |
|------|---------|---------|
| Q9.1 表格新列设计 | **A:压缩完后显示 + 自适应居中** | 新增"算法"列 |
| Q9.2 右键详情面板 | **符合预期，暂无对比实验** | 按照之前的设计图 |

**Q9.1 表格列设计最终确认**:
```
| ☐ | 文件名 | 大小 | 类型 | 状态 | 算法 | 压缩率 | 耗时 |
|---|--------|------|------|------|------|--------|------|
| ☐ | wow.html | 60KB | HTML | ✓ Done | LZMine | 65% | 120ms |
| ☐ | data.bin | 2.1MB | Binary | ⏳ Pending | - | - | - |
| ☐ | log.txt | 15MB | Text | ✓ Done | Deflate | 72% | 450ms |

- 列宽: 自适应内容
- 对齐: 居中
- 填充时机: 压缩完成后立即显示
- 待定时: 显示 "-" 或留空
```

### 2.4 额外需求与洞察

#### 💡 关键技术决策

**1. 原生数据结构实现**
用户要求: **"随机森林等决策器的保存需要用到一些数据结构吧，这个数据结构我原生实现，方便汇报演示成果"**

含义:
- ❌ 不直接使用 scikit-learn 的 pickle 序列化
- ✅ 自己实现树结构、节点、序列化格式
- ✅ 优点:
  - 完全可控，便于理解和展示
  - 可以导出为可视化的树形图
  - 便于论文/报告中的算法描述
  - 避免依赖黑盒库的内部实现变化

**需要原生实现的数据结构**:
```python
# 决策树节点
class TreeNode:
    feature_index: int      # 分裂特征索引
    threshold: float        # 分裂阈值
    left: TreeNode          # 左子树
    right: TreeNode         # 右子树
    value: float | None     # 叶子节点的预测值（分类时为类别概率）

# 随机森林
class RandomForest:
    trees: list[TreeNode]   # 多棵决策树
    max_depth: int          # 最大深度
    n_estimators: int       # 树的数量
    feature_importances: dict  # 特征重要性

# Huffman树（已有）
# 神经网络层（可选原生实现或用PyTorch）
```

**2. 未来扩展规划**
用户提到的长期愿景:
- 🖼️ 图片无损压缩算法 (PNG/WebP/FLIF)
- 🎵 音频无损压缩 (FLAC/APE)
- 🎬 视频无损压缩 ( HuffYUV/FFV1)
- 📄 字典增强压缩 (针对HTML/JSON/XML的预训练字典)
- 🔧 各种可逆预处理辅助选项（如去重、规范化；具体算法与管线不在本文约定）

**这些都需要在架构设计中预留接口！**

---

## 3. 已确认的决策清单

### 3.1 架构层面 (最终务实版)

| 决策项 | 选择 | 理由 |
|--------|------|------|
| 整体架构 | **双阶段ML管道** | Stage1(RF分类) + Stage2(NN回归+EA优化) |
| **技术栈 (务实评估)** | **C++原生实现** | RF/EA/简单NN用C++17+Eigen3，复杂DL用PyTorch+ONNX |
| **GPU加速定位** | **高级可配置选项** | 默认CPU，用户在高级设置中启用CUDA/OpenCL |
| **特征提取策略** | **单次遍历统一提取** | O(n)时间，实际内存占用需测试验证 |
| 特征维度 | **30+细粒度特征** | 支持复杂文件类型区分，可配置启用/禁用 |
| **数据持久化格式 (修正)** | **JSON格式** | 整合到FileRecord或C++ API封装，灵活性优先 |
| 数据存储 | **本地JSON文件** | 人类可读，易于调试和版本控制 |
| 模型持久化 | **原生序列化格式** | 便于展示和理解，避免库依赖 |
| **种群管理策略** | **统一种群/样本架构** | 每个压缩记录即种群个体，完整保留 |
| **高级配置系统** | **全面可配置化** | EA算法/种群参数/初始化策略/GPU等全部UI可调 |

### 3.2 功能层面 (含高级配置)

| 决策项 | 选择 | 行为描述 |
|--------|------|----------|
| 自动训练 | **完全静默** | 后台收集，自动重训练，用户无感知 |
| 训练数据范围 | **所有压缩行为** | 包括手动选择的情况 |
| 负面样本处理 | **混合模式** | 成功(1.0)/错误(0.5)/中断(丢弃) |
| 速度权衡 | **多目标优化** | 压缩率 vs 速度 的帕累托最优 |
| 暂停功能 | **双模式中断** | 第1次软中断 → 第2次硬中断 |
| 大文件支持 | **流式扫描** | 单次遍历，增量更新特征 |
| 样本保留策略 | **原封不动完整保留** | 内存有限制(可配置)，硬盘几乎无限，智能加载 |
| **EA算法选择** | **多算法实现 + 用户选择** | GA/PSO/CMA-ES全部实现，高级设置中切换 |
| **种群参数配置** | **UI可调整** | 大小/精英比例/探索比例/初始化策略全部可配 |
| **训练触发条件** | **可配置阈值** | 最少新样本数 + 时间间隔双重条件 |

### 3.3 UX/UI层面

| 决策项 | 选择 | 实现方式 |
|--------|------|----------|
| 决策透明度 | **三级展示** | 状态栏(简) → 表格列(中) → 右键面板(详) |
| 算法列显示 | **压缩完成后** | 居中，自适应宽度 |
| 详情面板 | **标准化模板** | 特征/评分/参数/预期vs实际 |
| 训练反馈 | **静默+可选报告** | 设置页高级选项查看统计 |
| 扫描状态提示 | **实时进度** | "正在分析: filename.ext (45%)" |

### 3.4 扩展性层面

| 决策项 | 选择 | 设计原则 |
|--------|------|----------|
| 新算法接入 | **标准成本** | ICompressor + FeatureAdapter + 配置注册 |
| 新特征添加 | **插件式** | FeatureExtractor基类，可继承扩展 |
| 多媒体支持 | **预留接口** | DomainSpecificPreprocessor抽象类 |
| 字典增强 | **可配置** | 外部字典文件 + 内置常用字典 |

---

## 4. 技术方案对比

> **待第二阶段输出** (当前讨论尚未完全结束，暂略)

---

## 5. 详细设计规格

> **待第三阶段输出** (需先确定最终方案)

---

## 6. 待定事项与后续计划

### 6.1 待确认的技术细节
- [ ] 流式特征提取的具体"早停准则"设计
- [ ] EA进化算法的具体变体选择 (CMA-ES/GA/PSO?)
- [ ] 原生数据结构的序列化格式 (JSON/二进制/自定义?)
- [ ] 特征向量的具体30+维度定义 (需结合论文调研)
- [ ] 神经网络回归器的架构设计 (层数/激活函数/损失函数)

### 6.2 论文调研计划 (BFS → DFS)

**Phase 1: 广度调研 (本周)**
- [ ] 搜索 "machine learning for compression algorithm selection" 近5年综述
- [ ] 收集 10-20 篇高引用论文标题和摘要
- [ ] 归类到4个研究方向

**Phase 2: 深度精读 (下周)**
- [ ] 每个方向挑选2-3篇最相关论文
- [ ] 提炼可复用的特征提取方法
- [ ] 记录实验设置和性能基准

**Phase 3: 方法融合 (后续)**
- [ ] 结合本项目特点，设计定制化的特征集
- [ ] 确定基线模型和评估指标

### 6.3 开发路线图预览

```
MVP (v0.1) - 2周:
  ├── 特征提取器 (基础15个特征)
  ├── 规则引擎 (if-else baseline)
  ├── 数据收集框架 (SQLite)
  └── UI: 算法列 + 基础详情面板

v0.5 - 4周:
  ├── 随机森林分类器 (原生实现)
  ├── 特征扩展至30维
  ├── 流式扫描实现
  └── 双模式暂停功能

v1.0 - 8周:
  ├── 神经网络回归器 (PyTorch)
  ├── EA参数优化集成
  ├── 自动训练管线
  └── 完整UI + 报告系统

v1.5+ (未来):
  ├── 多媒体专项算法接入
  ├── 字典增强压缩
  ├── 在线学习 (持续适应)
  └── 分布式训练 (可选)
```

---

## 7. 附录：关键问题与回答实录

### 7.1 关于"流式扫描是否需要每个字节" (已修正)

**用户疑问**: "特征提取不是整个文件每一个字节都要吗?"

**原始回答**: 
- 不需要逐字节！分三类特征:
  1. **全局统计特征**: 可增量计算（熵值、直方图等），边读边更新
  2. **局部结构特征**: 只需采样（文件类型、头部签名等）
  3. **元数据特征**: 无需读取内容（扩展名、大小等）
- 结论: **分块读取(64KB chunks)** + **智能采样(前/中/尾)** + **增量统计更新**

**✅ 用户纠正 (Q7.2 修订版)**:
**"对于文件熵这种特征，不每个字节看一遍怎么提取特征啊我问你,既然都全看过，所谓的局部特征不也可以在这个过程提取吗"**

**修正后的正确答案**:
- ✅ **用户的观点完全正确！**
- ✅ 既然计算熵值确实需要遍历每个字节，那在这个过程中**完全可以同时提取所有局部特征**
- ✅ **单次遍历策略**: O(n) 时间复杂度，<10KB 固定内存
- ✅ 三类特征（全局统计 + 局部N-gram + 结构检测）在同一个循环中统一提取
- ❌ 旧方案的"多次扫描"或"大量采样缓冲区"是过度设计

**性能提升**: 速度提升2-3倍，内存降低100倍以上

### 7.2 关于"C++原生实现可行性"

**用户提问**: "对于随机森林和进化算法。还有简单的神经网络层。可以直接用c加加实现吧。除了神经网络用到矩阵运算。还有可能用到一点gpu加速"

**详细分析**:

| 算法组件 | C++实现难度 | 推荐方案 |
|----------|-------------|----------|
| 随机森林 (RF) | ★☆☆ 极易 | 纯逻辑运算，递归分裂+投票统计 |
| 进化算法 (EA) | ★★☆ 容易 | 种群管理+遗传操作，C++天然适合 |
| 简单NN (全连接) | ★★☆ 容易 | 前向传播+BP，可用Eigen3库 |
| 复杂DL (CNN/RNN) | ★★★ 较难 | 建议用PyTorch训练→ONNX导出→C++推理 |

**推荐技术栈**:
- 核心ML: **C++17标准库 + Eigen3** (轻量级线性代数)
- 可选GPU加速: **CUDA/cuDNN** (如需)
- 离线训练: **Python PyTorch** → 导出ONNX → C++加载
- 数据存储: **自定义二进制格式** (自主管理)

**架构优势**:
- 🚀 性能: C++原生运行，无Python GIL限制
- 🔧 可控: 完全透明的算法实现
- 📦 部署: 单一可执行文件，无外部依赖
- 🎯 演示: 原生数据结构可直接可视化

### 7.3 关于"种群与样本统一管理"

**用户创新思路**: "我在想每个的样本可不可以都作为一个个体加入到该算法的种群，然后实际训练可以采样也可以全部用于更新"

**设计理念**:
- ✅ 每个压缩记录都是"种群的一员"
- ✅ 统一的 `EvolutionaryPopulation<T>` 模板类管理所有个体
- ✅ 内存中维护活跃种群（有限大小，如1万个体）
- ✅ 硬盘上完整保留所有历史记录（几乎无限制）

**智能初始化策略 (启动时)**:

```cpp
enum InitializationStrategy {
    RANDOM_SAMPLE,        // 随机选择多样化个体
    RECENT_PRIORITY,      // 优先选择最近样本 (反映当前习惯)
    FITNESS_BASED,        // 选择适应度最高的 (精英保留)
    DIVERSITY_BALANCED    // 平衡多样性 + 质量 (推荐!)
};
```

**用户最终决定**: "肯定要原封不动保留，选A,这是我们种群的一员吧"

完整保留策略:
```
✅ 内存中种群大小有限制 (如10,000个活跃个体)
✅ 硬盘存储几乎无限制 (单个记录 ~200bytes, 100万条 ≈ 190MB)
✅ 启动程序时智能初始化 (< 1秒索引查询)
✅ 选择标准: 多样性 + 时效性 + 质量平衡
```

### 7.4 关于"数据持久化格式" (最终修正)

**用户最终决策**: "二进制看来不利于格式改变，用json吧，我们对硬盘存储大小宽容一点"

**修订后的方案**:

| 维度 | JSON格式 (选择) | 二进制格式 (弃用) |
|------|----------------|------------------|
| **灵活性** | ✅ 易于添加/删除字段 | ❌ 需要复杂的版本兼容机制 |
| **可调试性** | ✅ 人类可读，文本编辑器即可查看 | ❌ 需要专门的hex查看器 |
| **版本兼容** | ✅ 自动处理缺失字段 (使用默认值) | ❌ 需要手动处理结构变更 |
| **存储空间** | ⚠️ ~800 bytes-1.2 KB/条 | ✅ ~200 bytes/条 |
| **解析速度** | ⚠️ ~1ms/条 | ✅ < 0.01ms/条 |
| **跨语言支持** | ✅ 天然支持 | ❌ 需定义schema |

**实际存储成本**:
```
单条记录: ~800 bytes - 1.2 KB
100万条记录: ~800 MB - 1.2 GB

用户评估: "对硬盘存储大小宽容一点"
→ 现代硬盘 (1TB+) 完全可以接受! ✅
```

**整合到现有数据模型的三种方案**:

```python
# 方案A: 直接扩展 FileRecord (最简单)
class FileRecord(Record):
    # ... 现有字段 ...
    ml_record: MLTrainingRecord | None = None  # 新增字段

# 方案B: C++核心 + Python API封装 (推荐，高性能)
# C++层负责: 特征提取、EA计算、内存种群管理
# Python层负责: UI集成、JSON序列化、调试接口

# 方案C: 混合模式 (灵活)
# 运行时: C++管理活跃种群 (高性能)
# 持久化: JSON文件存储 (易调试)
# 通过API桥接两层
```

### 7.5 关于"高级配置系统"

**用户要求**: 所有算法参数都应该是可配置的高级选项

**已确定的可配置项清单**:

```
📊 Stage 1 分类器 (随机森林):
   ✓ 树的数量 (10-1000)
   ✓ 最大深度 (3-50)
   ✓ 特征采样策略 (sqrt/log2/all)

🧠 Stage 2 参数优化器:
   ✓ 神经网络开关 + 架构参数
   ✓ EA算法选择 (GA/PSO/CMA-ES/NONE)
   ✓ EA参数 (代数、种群大小、变异率、交叉率)
   ✓ 编码方案探索开关 + 策略 (fixed/adaptive/random) — `use_flag_encoding` 等
   ✓ （可选）树策略探索 — `use_3hfmtree`（FLATE vs 3HfMTree，见 `flate-encoding-design.md`）
   ✓ （可选）3HfM 槽宽探索 — `huffman_chunk_bits` / `huffman_offset_chunk_bits` / `huffman_length_chunk_bits`
   ✓ 最大优化时间限制

🎯 GPU加速:
   ✓ 启用/禁用开关
   ✓ 设备类型选择 (CPU/CUDA/OpenCL)
   ✓ GPU设备ID、批处理大小

🧬 种群管理:
   ✓ 最大活跃种群大小 (1000-100000)
   ✓ 精英保留比例 (5%-30%)
   ✓ 探索样本比例 (10%-50%)
   ✓ 初始化策略 (4种可选)
   ✓ 时效性开关 (默认关闭)

⚙️ 训练触发:
   ✓ 最少新样本数阈值
   ✓ 重训练时间间隔

🔍 特征提取:
   ✓ 单次遍历开关
   ✓ N-gram特征开关
   ✓ 结构特征开关
   ✓ 高级特征开关 (默认关闭)
```

**UI位置**: 设置 → 高级 → 算法配置 → AUTO模式

### 7.5 关于"负面样本"

**用户洞察**: "之前我好像没有考虑过负面样本"

**重要性**: 
- 负面样本 = 用户取消/失败/超时的案例
- 价值: 帮助模型学会"什么不该推荐"
- 处理: 权重0.5，参与训练但影响减半

---

## 📝 文档维护日志

| 日期 | 版本 | 修改内容 | 作者 |
|------|------|----------|------|
| 2026-01-15 | v0.1 | 初始创建，记录Phase 1-2讨论内容 | AI Assistant |
| 2026-01-15 | v0.2 | 补充Q7-Q9问答，完善决策清单 | AI Assistant |
| 2026-01-15 | v0.3 | 添加额外需求和洞察，整理schema | AI Assistant |
| 2026-01-15 | v1.0 | 重大修订: C++原生实现、单次遍历特征提取、种群统一管理、二进制格式 | AI Assistant |
| **2026-01-15** | **v2.0** | **务实修正: JSON格式、高级配置系统、GPU整合、移除不切实际承诺** | **AI Assistant** |
| **2026-05-12** | **v2.1** | **编码方案参数补充: 新增 use_flag_encoding 到参数空间、优化器配置、L2探索、高级UI** | **AI Assistant** |
| **2026-05-13** | **v2.2** | **3HfMTree：Stage2/`params_used`/高级配置与 L2 探索与 `flate-encoding-design.md` 对齐说明** | **AI Assistant** |

---

**v2.0 务实修正说明**:

基于用户第五轮反馈，进行重要的**去泡沫化和落地修正**：

### ⚠️ 核心原则
> **严禁吹牛逼、脱离实际和欺骗行为。所有技术方案必须基于实际可行性。**

### 🔄 主要修正

1. **数据格式: 二进制 → JSON**
   - 原因: 二进制格式不利于版本变更和调试
   - 用户决策: "对硬盘存储大小宽容一点"
   - 结果: 选择JSON格式 (~800 bytes/条 vs 二进制200 bytes)
   - 优势: 灵活性、可读性、易调试 > 存储空间节省

2. **GPU加速: 可选功能 → 高级配置选项**
   - 原因: 不是简单开关，要整合到完整配置系统
   - 实现: "设置 → 高级 → 算法配置 → AUTO" 中配置
   - 默认: CPU (保证兼容性)
   - 可选: CUDA / OpenCL + 设备选择 + 批处理参数

3. **EA算法: 单一选择 → 多算法实现+用户切换**
   - 实现: GA / PSO / CMA-ES 全部实现
   - 用户可在高级设置中选择使用哪个
   - 或选择"NONE"关闭EA (仅用NN预测)

4. **种群管理参数: 硬编码 → 全面可配置**
   - 精英比例 (5%-30%)
   - 探索样本比例 (10%-50%)
   - 最大活跃种群大小 (1000-100000)
   - 初始化策略 (4种可选)
   - 时效性开关 (默认关闭)

5. **特征提取性能承诺: 乐观估计 → 需测试验证**
   - 修正: "O(n)时间，<10KB内存" → "O(n)时间，实际内存占用需测试验证"
   - 原因: 避免过度承诺，实际实现后需profiling

6. **数据持久化方案: 独立系统 → 整合到现有模型**
   - 方案A: 直接扩展 FileRecord (最简单)
   - 方案B: C++核心 + Python API封装 (推荐高性能场景)
   - 方案C: 混合模式 (运行时C++ + 持久化JSON)

### ✅ 已最终确认的决策

- [x] 技术栈: C++17原生实现核心算法 (RF/EA/简单NN)
- [x] 特征提取: 单次遍历统一提取 (需实际测试性能)
- [x] 数据格式: JSON (灵活性优先)
- [x] GPU加速: 高级可配置选项 (默认CPU)
- [x] EA算法: 多实现 + 用户选择 (GA/PSO/CMA-ES)
- [x] 种群管理: 参数全部UI可配置
- [x] 样本保留: 完整保存到硬盘 (JSON文件)
- [x] 高级配置系统: 完整的UI配置面板设计

### 📋 待后续工作 (按优先级)

#### 🔴 必须调研
1. **30维特征向量的具体定义** - 需结合论文调研确定每个特征的计算方法
2. **EA三种算法的实际效果对比** - 在真实压缩数据上测试GA vs PSO vs CMA-ES
3. **神经网络架构的浅调研+实践** - 首先需要收集初始训练样本

#### 🟡 需要实践验证
4. **单次遍历特征提取的实际内存占用** - 实现原型并profiling
5. **智能初始化策略的具体效果** - 测试不同策略的收敛速度
6. **C++与Python的性能差异基准测试** - 确定哪些模块确实需要C++

#### 🟢 可后续优化
7. **JSON文件的加载性能优化** - 如果100万条记录加载慢，考虑索引或分片
8. **高级配置UI的实现细节** - 具体框架和交互逻辑
9. **ONNX Runtime集成** (如果决定用PyTorch训练模型)

---

## 8. 静默采样策略 (Silent Exploration Policy)

> **设计日期**: 2026-05-09  
> **理论基础**: 多摇臂赌博机 (Multi-Armed Bandit) + 上下文赌博机 (Contextual Bandit)  
> **核心原则**: 兼顾 Explore（探索未知）与 Exploit（利用已知），静默执行，用户无感知

### 8.1 问题定义

当前 ADE 管线存在根本缺陷：

```
现状: 纯 Exploit 策略
┌──────────┐     ┌─────────────┐     ┌──────────────┐
│ 文件特征  │ ──→ │ DecisionEngine│ ──→ │ 唯一"最优"算法 │
│ (20-dim)  │     │ (RF预测)     │     │ 压缩 + 记录   │
└──────────┘     └─────────────┘     └──────────────┘
                                              ↓
                                    ❌ 永远不知道其他算法表现如何
                                    ❌ 模型陷入局部最优无法自我修正
                                    ❌ 新算法/新参数永远没有数据支撑
```

**形式化建模为上下文多摇臂赌博机问题：**

| 要素 | 对应含义 |
|------|---------|
| **Context (上下文)** | 文件的 20 维特征向量 `x ∈ R^20` |
| **Arms (摇臂/动作)** | 每个算法+参数组合 `a ∈ A`，其中 `A = {LZSS, LZMine, Deflate, DPFlate, Gzip, Brotli, Zstd} × P` |
| **Reward (奖励)** | 压缩率 `r = compressed_size / original_size`，越低越好 |
| **目标** | 最小化长期累积压缩比 `min Σ r_t` |

### 8.2 采样策略总览

```
┌──────────────────────────────────────────────────────────────────┐
│                    Exploration Pipeline                          │
│                                                                  │
│  用户点击 "AUTO 压缩"                                            │
│       │                                                          │
│       ▼                                                          │
│  ┌─────────────┐    ┌──────────────┐    ┌──────────────────┐    │
│  │ 特征提取     │ ─→ │ EXPLOIT 主路径 │ → │ 展示给用户的     │    │
│  │ (20-dim)    │    │ DecisionEngine │   │ 压缩结果         │    │
│  └─────────────┘    └──────────────┘    └──────────────────┘    │
│       │                     │                    ▲              │
│       │                     ▼                    │              │
│       │            ┌────────────────┐           │              │
│       │            │ 探索策略引擎    │ ── explore? ──┘          │
│       │            │ (UCB-Clustered)│                           │
│       │            └────────────────┘                           │
│       │                     │                                   │
│       │               Yes  │  No                                │
│       │                ▼   └──→ 跳过                            │
│       │            ┌──────────────┐                             │
│       └───────────→│ 后台线程      │                             │
│                   │ 静默探索压缩   │                             │
│                   │ (用户无感知)   │                             │
│                   └──────┬───────┘                             │
│                          │                                      │
│                          ▼                                      │
│                   ┌──────────────┐                              │
│                   │ TrainingStore │ ← 与主路径结果同等对待        │
│                   │ 写入探索样本   │   is_exploration=True       │
│                   └──────────────┘                              │
│                                                                  │
│  关键约束:                                                        │
│  • 探索不阻塞主流程 (异步后台线程)                                 │
│  • 探索失败不影响用户体验 (静默丢弃)                               │
│  • 探索样本与正常样本格式完全一致                                  │
│  • 总计算预算可控 (不超过主流程的 30% 时间开销)                    │
└──────────────────────────────────────────────────────────────────┘
```

### 8.3 核心算法: Adaptive Clustered UCB (AC-UCB)

#### 8.3.1 为什么选择 UCB 而非 ε-greedy

| 策略 | 探索方式 | 问题 | 适用场景 |
|------|---------|------|---------|
| ε-greedy | 固定概率随机选 | 不区分"值得探索的未知"和"不值得的未知"，浪费算力 | 简单场景 |
| Thompson Sampling | 从后验分布采样 | 需要维护完整后验，实现复杂，对压缩场景过度工程化 | 在线广告 |
| **UCB (Upper Confidence Bound)** | 选置信上界最高的 | 自适应：不确定时自动增加探索，确定时自动减少利用 | **本场景 ✅** |

**UCB 的直觉**: 对于每个 arm，我们不仅看它的平均收益，还加上一个"不确定性 bonus"。不确定性越大（尝试次数少），bonus 越大。这样系统会自然地：
- 少试过的 arm → 高 bonus → 更可能被选中（explore）
- 多试过的 arm → 低 bonus → 只在真正好时才被选中（exploit）

#### 8.3.2 Context Clustering: 将连续特征空间离散化为簇

20 维特征空间是连续的，直接按每个精确特征值做 UCB 会导致：
- 每个文件都是独一无二的 context → 永远没有足够的数据做决策
- 无法泛化到未见过的文件

**解决方案: K-Means / K-Medoids 聚类**

```python
# 伪代码: 特征空间聚类
class FeatureClusterSpace:
    """
    将 20 维特征向量映射到有限个簇 (cluster)
    同一簇内的文件被认为具有相似的"最优算法"
    
    初始化:
      - 使用历史数据的 K-Means 聚类 (K=16~64)
      - 或使用预定义的规则簇 (基于熵值区间、文件类型等)
    
    运行时:
      - 计算新文件特征到各簇中心的距离
      - 分配到最近的簇
      - 如果距离 > threshold → 创建新簇 (动态扩展)
    """
    
    MAX_CLUSTERS = 64
    NEW_CLUSTER_DISTANCE_THRESHOLD = 0.15  # 归一化距离
    
    def assign_cluster(self, features: BaseFeatures) -> int:
        vec = np.array(features.vector)
        
        if len(self.centers) == 0:
            return self._create_new_cluster(vec)
        
        distances = [np.linalg.norm(vec - c) for c in self.centers]
        min_dist = min(distances)
        best_cluster = int(np.argmin(distances))
        
        if min_dist > self.NEW_CLUSTER_DISTANCE_THRESHOLD and len(self.centers) < self.MAX_CLUSTERS:
            return self._create_new_cluster(vec)
        
        return best_cluster
```

#### 8.3.3 AC-UCB 完整公式

对于簇 `c` 中的算法 arm `a`：

$$
\text{UCB}(c, a) = \underbrace{\bar{r}_{c,a}}_{\text{平均压缩率}} - \underbrace{\alpha \cdot \sqrt{\frac{\ln(N_c)}{n_{c,a}}}}_{\text{探索 bonus}}
$$

其中：
- $\bar{r}_{c,a}$: 簇 `c` 中算法 `a` 的平均压缩率（越低越好）
- $N_c$: 簇 `c` 中所有 arm 的总采样次数
- $n_{c,a}$: 簇 `c` 中算法 `a` 的采样次数
- $\alpha$: 探索系数（默认 1.41 ≈ √2）

**注意符号**: 因为压缩率是"越低越好"，所以 UCB = mean − bonus（与标准 UCB 反向）。

**探索触发条件**:

```python
def should_explore(self, cluster_id: int, greedy_algo: AlgorithmType) -> tuple[bool, AlgorithmType | None]:
    """
    返回: (是否需要探索, 探索目标算法)
    """
    stats = self.cluster_stats.get(cluster_id, {})
    N_total = sum(s.count for s in stats.values())
    
    if N_total < MIN_SAMPLES_FOR_DECISION:
        # 数据太少，强制探索
        return True, self._pick_least_sampled(cluster_id)
    
    # 计算 UCB 分数
    ucb_scores = {}
    for algo in ALL_ALGORITHMS:
        s = stats.get(algo, ArmStats(mean=1.0, count=0))
        if s.count == 0:
            ucb_scores[algo] = float('inf')  # 未尝试过 → 最高优先级
        else:
            bonus = self.alpha * math.sqrt(math.log(N_total + 1) / s.count)
            ucb_scores[algo] = s.mean - bonus
    
    best_ucb_algo = min(ucb_scores, key=ucb_scores.get)
    gap = abs(ucb_scores[best_ucb_algo] - ucb_scores[greedy_algo])
    
    if best_ucb_algo != greedy_algo and gap > EXPLORATION_GAP_THRESHOLD:
        return True, best_ucb_algo
    
    return False, None
```

### 8.4 两层探索策略

探索分为两个层级，对应不同的信息增益：

#### L1: 算法级探索 (Algorithm-Level)

**目的**: 发现某个簇中哪个算法整体最优

| 场景 | Greedy 选择 | L1 探索目标 | 信息价值 |
|------|------------|-------------|---------|
| 高熵随机数据 | LZMine | Brotli / Zstd | 验证专用算法是否真的无效 |
| 低重复文本 | Deflate | LZMine | 验证 LZ77 变体是否更优 |
| 已压缩文件 | SKIP | LZSS (轻量) | 验证是否还能再压一点 |

**触发条件**: 
- 该簇内某算法采样次数 < 3 次（严重欠采样）
- 或 UCB gap > 0.05（5% 以上的潜在改进空间）

#### L2: 参数级探索 (Parameter-Level)

**目的**: 在已选定算法附近搜索更好的参数组合

```
参数扰动策略:
┌──────────────────────────────────────────────────────┐
│ 当前参数: window_size=32768, chain_length=128        │
│                                                      │
│ L2 扰动选项 (每次选 1 个维度):                       │
│                                                      │
│  • window_size: ±50% 随机 → [16384, 49152]          │
│  • chain_length: ±50% 随机 → [64, 192]              │
│  • hash_bits: ±1 步长 → [14, 16, 18]               │
│  • lazy_match: 开关翻转                              │
│  • use_flag_encoding: 开关翻转 (0↔1)                │
│  • use_3hfmtree: 开关翻转 (0↔1)，仅适用算法且预算充足时  │
│  • huffman_chunk_bits: ±1～2 档（在允许范围内），同上   │
│                                                      │
│ 优先级: 先扰动能影响最大的参数 (window_size > chain)  │
└──────────────────────────────────────────────────────┘
```

**触发条件**:
- 该簇中该算法已有 ≥ 5 个样本（有统计意义的基础）
- 但参数方差大（说明最优参数尚未收敛）

#### 探索分配比例

```
总探索预算 (默认): 每次压缩有 P_explore = 0.25 的概率触发探索

P_explore 内部分配:
┌──────────────────────────────────────────┐
│ L1 算法探索: 60%                         │
│   → 当该簇存在未充分采样的算法时          │
│                                          │
│ L2 参数探索: 30%                         │
│   → 当当前算法的参数置信度不足时          │
│                                          │
│ L0 全量扫描: 10%                         │
│   → 用所有算法都压一遍 (仅小文件 <100KB)  │
│   → 用于快速建立初始基准                  │
└──────────────────────────────────────────┘
```

### 8.5 自适应衰减机制

探索强度不应恒定——随着数据积累应逐渐减少盲目探索：

```
自适应 ε (exploration rate):

ε_effective = ε_base × f_confidence × f_novelty × f_budget

其中:

f_confidence = max(0.1, 1.0 - sqrt(n_total / N_warmup))
  ↑ n_total 越多 → 越自信 → 探索越少
  ↑ 但不低于 0.1 (永不停止探索)

f_novelty = 1.0 + novelty_bonus
  ↑ 如果文件属于新创建的簇 → novelty_bonus = 0.5
  ↑ 新模式需要更多探索来建立基线

f_budget = min(1.0, remaining_compute_budget / total_budget)
  ↑ 本轮剩余计算预算比例
  ↑ 预防探索消耗过多 CPU 时间
```

**关键阈值表:**

| 阶段 | 累计样本数 | ε_base | 行为描述 |
|------|-----------|--------|---------|
| 冷启动 | 0 ~ 50 | 0.40 | 大量探索，快速建表 |
| 学习期 | 51 ~ 500 | 0.25 | 平衡探索与利用 |
| 成熟期 | 501 ~ 2000 | 0.15 | 主要利用，偶尔验证 |
| 稳定期 | 2000+ | 0.08 | 几乎纯利用，极少量防退化检查 |

### 8.6 静默执行机制

```
┌──────────────────────────────────────────────────────────┐
│                 Silent Explorer Thread                    │
│                                                           │
│  输入: file_data, explore_algorithm, explore_params       │
│                                                           │
│  执行流程:                                                 │
│  1. 复制原始数据 (避免竞争)                                 │
│  2. 创建独立 CompressionEngine 实例                        │
│  3. 使用 explore_algorithm + explore_params 执行压缩        │
│  4. 构造 TrainingSampleV3:                                 │
│     ├── features_vector: 相同 (来自同一文件的提取结果)      │
│     ├── algorithm_used: explore_algorithm (不是主路径的!)  │
│     ├── compression_ratio: 探索结果的比率                   │
│     ├── is_exploration: True                              │
│     └── sample_weight: 1.0 (与正常样本同权)               │
│  5. store.add_sample(sample)                               │
│                                                           │
│  异常处理:                                                 │
│  • 压缩超时 (> 30s) → 丢弃样本, 标记 timeout               │
│  • 内存不足 → 跳过本次探索                                 │
│  • 用户取消全部任务 → 立即终止探索线程                      │
│                                                           │
│  性能约束:                                                 │
│  • 最大并发探索数: 2 (防止占用过多资源)                     │
│  • 单次探索超时: 30s                                       │
│  • 总探索时间占比上限: 30% of total compress time          │
└──────────────────────────────────────────────────────────┘
```

### 8.7 训练数据中的探索标记

```python
@dataclass
class TrainingSampleV3:
    # ... 现有字段 ...
    
    # ===== 新增: 探索标记 =====
    is_exploration: bool = False          # 是否为探索样本
    exploration_type: str = ""           # "L1_algo" | "L2_param" | "L0_full"
    exploration_target: str = ""         # 探索目标的算法名
    parent_decision: str = ""            # 主路径 DecisionEngine 的推荐算法
    ucb_gap_at_time: float = 0.0         # 触发探索时的 UCB gap 值
    cluster_id: int = -1                 # 所属特征簇 ID
```

**训练时的处理**:

| 样本类型 | 训练权重 | 用途 |
|---------|---------|------|
| 正常样本 (is_exploration=False) | w=1.0 | 主训练信号 |
| 探索样本 - L1 (is_exploration=True) | w=1.0 | 补充稀疏区域的监督信号 |
| 探索样本 - L2 (is_exploration=True) | w=0.8 | 参数回归的辅助数据 |
| 探索样本 - L0 (is_exploration=True) | w=0.6 | 冷启动基线数据 |

### 8.8 与现有架构的集成点

```
修改范围 (最小侵入):

1. gui/ui/main_window.py
   └── single_compress() 方法末尾
       └── 压缩完成后调用 explorer.maybe_explore(record)

2. gui/ade/explorer.py
   └── class SilentExplorer
       ├── should_explore() → AC-UCB 决策
       ├── pick_target() → 选择探索目标
       ├── execute_async() → 后台线程执行
       └── record_result() → 写入 TrainingDataStore

3. gui/ade/training.py
   └── TrainingSampleV3 新增探索字段 (向后兼容, 默认值)

4. gui/engine/compressor.py（及 bridge）
   └── 无需修改 (CompressionEngine 本身不变)

5. config/webcompress_settings.json
   └── 新增段 "exploration":
       {
         "enabled": true,
         "epsilon_base": 0.25,
         "alpha_ucb": 1.41,
         "max_concurrent": 2,
         "timeout_seconds": 30,
         "budget_ratio": 0.30
       }
```

### 8.9 预期效果与验证指标

**定性预期:**
- 冷启动阶段 (前 50 个文件): 快速建立各算法在各簇上的性能基线
- 学习阶段 (50-500 个文件): 开始出现"意外发现"——某些簇的最优算法与直觉不同
- 成熟阶段 (500+ 个文件): 探索频率降低，但偶尔发现参数优化的边际收益

**定量验证指标:**

| 指标 | 定义 | 目标值 |
|------|------|--------|
| **覆盖率** | (至少被探索过的簇数) / 总簇数 | > 90% after 200 samples |
| **后悔界 (Regret)** | Σ(实际压缩比 - 最优压缩比) / n | 随 O(√T) 衰减 |
| **发现率** | 探索中发现优于贪婪选择的次数 / 总探索次数 | > 15% |
| **开销比** | 探索总耗时 / 主流程总耗时 | < 30% |
| **收敛速度** | 达到稳定 top-2 算法排序所需的样本数 | < 300 per cluster |

### 8.10 设计约束与边界条件

| 约束 | 处理方式 |
|------|---------|
| **小文件 (< 1KB)** | 跳过探索（压缩本身太快，探索无意义） |
| **超大文件 (> 100MB)** | 仅 L1 探索（L2 参数探索太慢） |
| **用户手动选算法** | 不触发探索（尊重用户意图，但仍记录结果） |
| **SKIP 决策** | 不触发探索（不可压缩的文件不需要比较） |
| **离线/断网环境** | 正常工作（探索是本地操作，无需外部依赖） |
| **首次运行 (空训练集)** | ε_base 自动提升至 0.5（冷启动高探索） |

---

## 9. 国际化 (i18n) 计划

> **状态**: 规划中 (未来版本)  
> **优先级**: P2 (功能完善)  
> **目标**: 支持中英文界面切换

### 9.1 现状

当前所有 UI 文本硬编码为中文（2026-05-09 统一修改），包括：

| 模块 | 硬编码位置 |
|------|-----------|
| 主窗口菜单/工具栏 | `gui/ui/main_window.py` |
| 文件表格 | `gui/ui/main_window.py` FileTableWidget |
| 决策详情对话框 | `gui/ui/main_window.py` DecisionDetailDialog |
| 状态栏 | `gui/ui/main_window.py` StatusBarWidget |
| 算法选择器 | `gui/ui/main_window.py` AlgorithmSelector |
| 算法配置对话框 | `gui/ui/main_window.py` AlgorithmConfigDialog |
| 对比视图 | `gui/ui/views/comparison_view.py`；算法对比弹窗 `gui/ui/dialogs/comparison_dialog.py` |

### 9.2 设计方案

```
┌─────────────────────────────────────────────┐
│              i18n 架构                       │
│                                             │
│  ┌──────────────┐    ┌──────────────────┐  │
│  │ locales/      │    │ I18nManager       │  │
│  │ ├── zh_CN.json │ ←→│ .get(key) → str  │  │
│  │ └── en_US.json │    │ .set_locale()    │  │
│  └──────────────┘    └──────────────────┘  │
│           ↑                   ↓             │
│  ┌──────────────────────────────────────┐  │
│  │ UI 层: tr("文件") / tr("File")        │  │
│  │         ↓ 自动解析                     │  │
│  │ I18nManager.get("menu.file") → "文件" │  │
│  └──────────────────────────────────────┘  │
│                                             │
│  配置持久化: webcompress_settings.json      │
│  └── "ui_locale": "zh_CN"                  │
└─────────────────────────────────────────────┘
```

### 9.3 实施步骤

| 阶段 | 任务 | 工作量估计 |
|------|------|-----------|
| Phase 1 | 创建 `I18nManager` 单例 + JSON 语言包 | 小 |
| Phase 2 | 提取主窗口 (~50 条文本) 到语言包 | 中 |
| Phase 3 | 提取对话框组件 (~80 条文本) | 中 |
| Phase 4 | 设置页添加"语言切换"下拉框 | 小 |
| Phase 5 | 热切换支持 (无需重启) | 中 |

### 9.4 语言包结构示例

```json
{
  "zh_CN": {
    "app.title": "WebCompress",
    "menu.file": "文件 (&F)",
    "menu.file.add": "添加文件 (&F)",
    "menu.help": "帮助 (&H)",
    "dialog.decision_title": "决策详情 - {name}",
    "group.file_info": "文件信息",
    "label.file_name": "文件名:",
    "label.compression_ratio": "压缩率:",
    ...
  },
  "en_US": {
    "app.title": "WebCompress",
    "menu.file": "&File",
    "menu.file.add": "Add &File",
    "menu.help": "&Help",
    "dialog.decision_title": "Decision Details - {name}",
    "group.file_info": "File Information",
    "label.file_name": "File Name:",
    "label.compression_ratio": "Compression Ratio:",
    ...
  }
}
```

### 9.5 技术约束

| 约束 | 处理方式 |
|------|---------|
| 不引入 gettext/Qt Linguist | 自研轻量 JSON 方案，避免额外依赖 |
| 动态参数 (如 `{name}`) | 使用 `.format()` 或 f-string 预处理 |
| Qt 内置字符串不翻译 | 仅翻译自定义 UI 文本，Qt 标准对话框保持系统语言 |
| 日志消息保持英文 | 日志面向开发者，不参与 i18n |

---

*本文档将持续更新，保持务实态度，直到模块设计冻结并进入开发阶段。*
