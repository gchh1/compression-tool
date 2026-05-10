# ADE Feature Extraction Framework v3.0 - Implementation Guide

## 📁 项目结构

```
src/ade/
├── include/                          # 头文件 (Header-only library)
│   ├── FeatureVectorV3.hpp          # 核心数据结构 (Base + Extension)
│   ├── MagicBytesDetector.hpp        # Magic Bytes 检测器 (30+格式)
│   ├── CountMinSketch.hpp            # Count-Min Sketch 算法 (64KB)
│   ├── BaseFeatureExtractor.hpp      # Base Segment 20维提取器
│   ├── ExtensionExtractors.hpp       # 6个 Extension 提取器
│   └── FeatureExtractorV3.hpp        # 主提取流程 + JSON序列化
├── tests/
│   └── test_feature_extractor.cpp    # 单元测试 (30+ 测试用例)
├── CMakeLists.txt                    # 构建配置
└── README.md                         # 本文件
```

## 🚀 快速开始

### 1. 编译和运行测试

```bash
# 进入项目根目录
cd d:\AAA_C\compression-tool

# 构建 ADE 模块和测试
mkdir build && cd build
cmake .. -DADE_BUILD_BENCHMARK=OFF
cmake --build . --config Release

# 运行单元测试
.\bin\test_ade_feature_extractor.exe
```

### 2. 基本用法示例

```cpp
#include "FeatureExtractorV3.hpp"
#include <iostream>
#include <vector>

int main() {
    // 1. 创建特征提取器实例
    compressor::ade::FeatureExtractorV3 extractor;

    // 2. 加载文件数据 (从内存或磁盘)
    std::vector<uint8_t> file_data = load_file("example.png");

    // 3. 提取特征 (单次调用，自动完成所有步骤)
    auto result = extractor.extract(file_data);

    // 4. 访问结果
    std::cout << "File Type: "
              << compressor::ade::file_type_to_string(result.detection.type) << "\n";
    std::cout << "Confidence: " << result.detection.confidence << "\n";
    std::cout << "Shannon Entropy: " << result.vector.base.shannon_entropy << " bits/byte\n";
    std::cout << "Skewness: " << result.vector.base.skewness << " (encryption detector)\n";
    std::cout << "Extension Type: " << static_cast<int>(result.vector.ext_type) << "\n";
    std::cout << "Total Vector Size: " << result.vector.total_bytes() << " bytes\n";

    // 5. 获取 ML 就绪的输入数组 (33维, 132字节)
    auto ml_input = result.to_ml_input();  // std::vector<float>(33)

    // 6. 序列化为 JSON 存储
    std::string json_str = result.to_json();
    save_to_database(json_str);

    return 0;
}
```

### 3. 批量处理多个文件

```cpp
#include <vector>
#include <string>

// 文件路径列表
std::vector<std::string> files = {
    "document.pdf",
    "image.png",
    "archive.zip",
    "source.cpp",
    "audio.mp3"
};

// 批量提取特征
auto results = extractor.batch_extract(files);

for (size_t i = 0; i < results.size(); ++i) {
    if (results[i].detection.type != compressor::ade::FileType::UNKNOWN) {
        std::cout << files[i] << ": "
                  << compressor::ade::file_type_to_string(results[i].detection.type)
                  << " (" << results[i].extraction_time_ms << " ms)\n";
    }
}
```

## 🎯 核心组件说明

### 1. FeatureVectorV3 (数据结构)

**分段式架构**:
- **BaseSegment**: 20维通用特征 (80字节固定)
- **ExtensionData**: 5-8维类型特定特征 (20-32字节可变)

**支持的文件类型**:
| 类别 | Extension | 维度 | 大小 |
|------|-----------|------|------|
| Text/Code | TextCodeExtension | 5 dims | 20 bytes |
| Image | ImageExtension | 8 dims | 32 bytes |
| Audio | AudioExtension | 6 dims | 24 bytes |
| Video | VideoExtension | 7 dims | 28 bytes |
| Archive | ArchiveExtension | 4 dims | 16 bytes |
| Binary/Exec | BinaryExtension | 5 dims | 20 bytes |

### 2. MagicBytesDetector (文件类型检测)

**支持 30+ 格式**:
- 📦 压缩包: ZIP, GZIP, BZIP2, 7Z, RAR, TAR, XZ
- 🖼️ 图片: PNG, JPEG, GIF, BMP, TIFF, WebP, AVIF
- 🎵 音频: WAV, MP3, FLAC, OGG, AAC
- 🎬 视频: MP4, MKV, AVI, FLV, WebM
- 🔧 可执行: PE (Windows), ELF (Linux), Mach-O (macOS)
- 📄 文档: PDF, OLE2 (DOC/XLS/PPT)
- 🗄️ 数据库: SQLite
- 📝 文本: UTF-8 BOM, XML, HTML, JSON

**置信度评分** [0.0, 1.0]:
- ≥ 0.95: 高置信度 (精确匹配签名)
- 0.80-0.94: 中等置信度 (容器格式需二次验证)
- < 0.80: 低置信度 (启发式检测)

### 3. CountMinSketch (N-gram统计)

**参数配置**:
```
Width: 4096 列
Depth: 4 行 (hash functions)
Memory: 64 KB 固定
Error bounds: ε=0.00049, δ=0.0183
```

**用途**:
- 特征 [16]: Bigram 唯一性比例
- 特征 [17]: Top-K 集中度 (K=10)
- 支持增量更新，适合流式处理

### 4. BaseFeatureExtractor (Base 20维)

**提取流程 (O(n) 单次遍历)**:

```
Phase 1: 元数据统计 (file_size, printable_ratio, etc.)
Phase 2: 字节直方图累积 (histogram[256])
Phase 3: N-gram feeding → Count-Min Sketch
Phase 4: 局部熵窗口化 (1024-byte blocks)
Phase 5: 最终统计计算 (entropy, skewness, kurtosis, etc.)
```

**关键特征**:
| ID | 名称 | 范围 | 用途 |
|----|------|------|------|
| [3] | shannon_entropy | [0, 8] | 可压缩性指标 |
| [14] | **skewness** | [-1, 1] | 🔑 加密检测器 |
| [15] | kurtosis | ~[-1, 2] | 分布形态 |
| [18] | rle_potential | [0, 1] | RLE压缩潜力 |
| [19] | dict_potential | [0, 1] | LZ77字典潜力 |

### 5. ExtensionExtractors (6个专用提取器)

每个提取器解析文件头部的格式特定元数据:

- **ImageExtractor**: 解析 PNG IHDR/JPEG SOF/BMP DIB header
  - 关键字段: `is_lossless_original` (决定是否可以无损转换)
  - JPEG质量估计: 从量化表反推质量因子

- **AudioExtractor**: 解析 WAV fmt / MP3 frame / FLAC STREAMINFO
  - 支持采样率、位深度、声道数、时长估算

- **ArchiveExtractor**: 解析 ZIP local header / GZIP trailer
  - 内部格式识别、再压缩潜力评估

- **BinaryExtractor**: 解析 PE COFF / ELF header
  - 结构密度、对齐粒度、端序、可执行性评分

## 📊 性能特性

| 指标 | 数值 |
|------|------|
| 时间复杂度 | **O(n)** 单次遍历 |
| 工作内存 | **~130 KB** (64KB CMS + 统计累加器) |
| 输出大小 | **100-112 字节** (未填充) |
| ML输入大小 | **132 字节** (33维填充) |
| 支持的最大文件大小 | 仅受系统内存限制 |
| 提取速度目标 | **>100 MB/s** (待实测验证) |

## 🧪 测试覆盖

单元测试包含 **30+ 测试用例**, 分为8大类:

1. ✅ 数据结构验证 (FeatureVectorV3)
2. ✅ Magic Bytes 检测 (PNG, JPEG, ZIP, PDF, ELF, 文本启发式)
3. ✅ Count-Min Sketch 准确性 (基本操作、内存大小、唯一性估计)
4. ✅ Base Segment 提取 (空文件、文本、高熵随机、重复数据、混合内容)
5. ✅ Extension 提取 (Text, PNG, BMP, WAV, ZIP, PE)
6. ✅ 端到端集成 (完整流程、JSON序列化、ML数组生成)
7. ✅ 边界情况 (空指针、零长度、单字节、大文件模拟)
8. ✅ 错误处理 (异常安全、断言检查)

运行测试:
```bash
cd build
ctest --output-on-failure
# 或直接执行
./bin/test_ade_feature_extractor.exe
```

## 🔧 高级配置

### 自定义 Count-Min Sketch 参数

```cpp
// 使用更大的 sketch 以获得更高精度 (256KB)
using BigCMS = compressor::ade::CountMinSketch<8192, 8>;
static_assert(BigCMS::memory_size() == 262144);  // 256 KB
```

### 扩展新的文件类型

1. 在 `FileType` 枚举中添加新类型
2. 在 `MagicBytesDatabase` 中添加签名模式
3. 在 `map_file_type_to_extension()` 中添加路由规则
4. (可选) 创建新的 Extension 结构体和 Extractor

### 与 ADE ML Pipeline 集成

```cpp
// 1. 提取特征
auto result = extractor.extract(file_data);

// 2. 获取 ML 输入向量
std::vector<float> features = result.to_ml_input(33);

// 3. 送入 Random Forest 分类器
auto algorithm = rf_classifier.predict(features);

// 4. 使用 EA 优化参数 (如果需要)
auto params = ea_optimizer.optimize(algorithm, features);
```

## ⚠️ 已知限制

1. **MP4/MKV 视频元数据**: 当前为简化实现，需要完整的 box/EBML 解析器才能准确提取分辨率等信息
2. **Skewness/Kurtosis 计算**: 当前为近似算法，精确计算需要跟踪三阶/四阶矩
3. **局部熵方差**: 需要维护滑动窗口缓冲区，对极大文件 (>1GB) 可能占用较多内存
4. **JPEG 质量估计**: 基于量化表的启发式方法，误差约 ±10%

## 📈 下一步工作

1. **性能基准测试**: 实测不同文件大小的提取速度和内存占用
2. **测试集构建**: 收集 500+ 真实文件样本进行验证
3. **ML 模型训练**: 基于 Random Forest 的算法选择分类器
4. **流式处理优化**: 支持超大文件的分块处理 (无需全部加载到内存)
5. **多线程并行**: 对批量处理场景启用并行提取

## 📚 相关文档

- 规格文档: `docs/feature_vector_specification_v3_final.md`
- ADE 设计文档: `docs/algorithm_decision_engine_design.md`
- 文献分析: `docs/FileFeatures/literature_analysis_report.md`

---

**版本**: v3.0 Final
**最后更新**: 2026-05-07
**作者**: WebCompress ADE Team