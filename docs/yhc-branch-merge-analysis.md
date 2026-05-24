# yhc 分支合并分析报告

> 分析日期：2026-05-24
> 目标分支：`local-backup`（即 yhc 分支）
> 当前分支：`algorithm`
> 差异规模：300 文件，+2,869 / -56,813 行

---

## 一、分支总览

```
algorithm       ← 当前主分支（300+ 文件，模块化架构）
local-backup    ← yhc 分支（扁平结构，专注图像/归档功能）
main            ← 原始分支
```

**architecture 差异对比：**

| 维度 | algorithm（当前） | local-backup（yhc） |
|------|------------------|---------------------|
| 算法层 | `algorithm_new/` (LZDP/LZSS/Deflate/DPFlate/Brotli/Zstd/stb_image) | `algorithm/` (Deflate/LZSS/LZMine/LZ77/Delta/Huffman3HM) |
| 压缩器层 | `core_new/` (AlgorithmFactory + ExternalCodecs + GuiCompressors) | `core/` (AlgorithmFactory + LZSS/Deflate/LZMine) |
| API 层 | `api_new/` (WCXProtocol + pipeline + file compressFile) | 无独立 API 模块 |
| 预处理 | 无 | `preprocessor/` (ImagePreprocessor + ImageParser + Archiver) |
| 流水线 | `processor/` (Pipeline + StreamProcessor) | `processor/` (简化版) |
| 决策引擎 | `ade/` (DecisionEngine + Explorer + features + training) | 无（仅有 strategy.py 桩代码） |
| GUI | `gui/ui/` (MainWindow 3700+ 行 + worker + dialogs + config) | `gui/widgets/main_window.py` (简化版) + `gui/core/` |
| WebAssembly | 无 | `bindings/wasm/` |
| Python 绑定 | `bindings/pybind_new/` (完整模块) | `bindings/pybind/` (简化版，含 LZMine) |

---

## 二、yhc 分支独有的内容（可合并候选）

### 2.1 `src/preprocessor/` — 图片预处理模块

**文件清单：** 5 个文件（约 350 行）

| 文件 | 说明 |
|------|------|
| `include/IPreprocessor.hpp` | 抽象接口：`encode(raw_data, quality)` → `decode(preprocessed_data)` |
| `include/ImagePreprocessor.hpp` | 继承 IPreprocessor，使用 Delta 算法编码像素数据 |
| `ImagePreprocessor.cpp` | encode: 解析→Delta 编码→存宽高头；decode: 读头→Delta 解码→恢复 PNG |
| `include/ImageParser.hpp` | 定义 `ImageData` 结构体 + parser/restore 静态方法 |
| `ImageParser.cpp` | 使用 stb_image 解析→像素数组；stb_image_write 像素→PNG 字节流 |

**数据流架构：**
```
原始图片 (.jpg/.png)
  → ImageParser::parse()
  → 像素数组 (RGB, width × height × 3)
  → ImagePreprocessor::encode(quality)
     → 存入 8-byte 头 (width + height)
     → Delta::encode(pixels, quality)  // 逐像素差分编码
     → 输出的 Delta 预处理数据
  → 后续可再经过 LZ/Deflate 等进一步压缩
```

**与当前 algorithm 分支的 ImageCompressor 对比：**

| | ImageCompressor（当前） | ImagePreprocessor（yhc） |
|---|---|---|
| 输入 | 任意图片字节流 | 任意图片字节流 |
| 处理 | stb JPEG 编码/解压 或 PNG 重编码 | stb 解码→Delta 差分→输出像素差异数据 |
| 输出 | 标准 JPEG/PNG 文件 | Delta 编码后的中间数据 + 8-byte头 |
| 质量控制 | JPEG quality 参数 | Delta shift 参数（quality 映射） |
| 用途 | 直接输出可读图片文件 | 作为中间预处理步骤，配合 LZ 类算法二次压缩 |

**关键结论：** 两种方案互补而非竞争。`ImagePreprocessor` 是**预处理中间层**，`ImageCompressor` 是**端到端格式转换器**。

### 2.2 `LZMine` 算法 — KMP + DP + LiteralRun

**文件清单：** 4 个文件（约 670 行）

| 文件 | 说明 |
|------|------|
| `src/algorithm/LZMine.cpp` | KMP 匹配器 + DP 最优分段 + LiteralRun 编码 |
| `src/algorithm/include/LZMine.hpp` | 类声明：compress/compress_ultra/decompress，支持 KMPNEXT 匹配 |
| `src/algorithm/include/KMPMatcher.hpp` | KMP next 数组生成 |
| `src/core/include/LZMineCompressor.hpp` | Compressor 接口包装 |

**算法特点：**
- 使用 KMP 字符串匹配算法在搜索窗口中查找最长匹配
- DP 动态规划选择最优 (offset, length, next_byte) 三元组分段
- 未匹配区域用 LiteralRun 原样编码
- 支持 `compress_ultra`（多 range 搜索）

**与现有算法对比：**

| 算法 | 匹配方法 | 编码 |
|------|---------|------|
| LZSS | 滑动窗口 + Hash 查表 | (offset, length) 二元组 |
| LZDP | Array-of-Length 前向扫描 | (offset, length) 对 / DP Flate 级联 |
| DPFlate | 同 LZDP + 后置 Huffman | LZ 编码 + Deflate 级联 |
| **LZMine** | **KMP 字符串匹配** | **(offset, length, next_byte) 三元组 + LiteralRun** |

**合并价值：** 新的匹配范式，在特定数据分布下可能有压缩率优势。需要与现有算法做 Benchmark 对比。

### 2.3 `Delta` 算法 — 逐像素差分编码

**文件清单：** 2 个文件（约 200 行）

| 文件 | 说明 |
|------|------|
| `src/algorithm/Delta.cpp` | DeltaEncode/DeltaDecode 流式实现 + static encode/decode API |
| `src/algorithm/include/Delta.hpp` | 头文件声明 |

**当前状态：** algorithm 分支的 `src/core/AlgorithmFactory.cpp` 已引用 `DeltaEncode` / `DeltaDecode`，但源码中**不存在 Delta.hpp/.cpp**。这意味着：
- 要么 Delta 文件曾存在于某次提交但被删除
- 要么 AlgorithmFactory 中有死代码引用

**合并行动：**
- 从 yhc 移植 `Delta.hpp` / `Delta.cpp` 到 `src/algorithm/`
- 或确认 AlgorithmFactory 中的 Delta 引用是死代码并移除

### 2.4 `Archiver` — 自定义文件归档

**文件清单：** 2 个文件（约 170 行）

| 文件 | 说明 |
|------|------|
| `src/preprocessor/Archiver.cpp` | pack/unpack 静态方法，自定义打包协议 |
| `src/preprocessor/include/Archiver.hpp` | File 结构体 + Archiver 类声明 |

**打包协议：**
```
[Magic: 0x503B0304] (4 bytes)
[File Count] (4 bytes)
  [Name Length] (2 bytes)
  [File Name] (n bytes)
  [File Size] (4 bytes)
  [File Data] (n bytes)
```

**与当前 WCX 协议对比：**

| | Archiver（yhc） | WCX（当前 algorithm） |
|---|---|---|
| 用途 | 多文件打包/解包 | 单文件压缩包装 |
| 元数据 | 文件名 + 文件大小 | 算法ID + 原始/压缩大小 + flags |
| 算法标记 | 无 | 有（algo_code） |
| 版本兼容 | 无版本字段 | 有 VERSION 字段 |

**合并评估：** 当前 algorithm 分支已有 `src/archiver/` + WCX 协议，Archiver 功能重叠。仅在需要 WebAssembly 多文件打包时有用。

### 2.5 `src/bindings/wasm/` — WebAssembly 绑定

**文件清单：** 2 个文件

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | Emscripten 编译配置 |
| `wasm_bindings.cpp` | 暴露 DeflateCompressor + Archiver 到 JavaScript |

**合并价值：** 提供 Web 端部署能力，是全新的部署目标。

### 2.6 `src/gui/core/` — 简化版 GUI 核心

**文件清单：** 4 个文件（约 500 行）

| 文件 | 说明 |
|------|------|
| `engine.py` | 简化引擎：仅支持 LZSS/LZMINE/DEFLATE + Archiver.pack/unpack |
| `models.py` | 基础数据模型 + ResourceType 枚举 + IMAGE_EXTENSIONS 定义 |
| `strategy.py` | 决策引擎桩代码（RandomForest / NeuralNetwork） |
| `file_helper.py` | 文件辅助工具 |

**合并评估：** 当前 algorithm 分支的 GUI 功能远更完善，`gui/core/` 不需要合并。仅 `ResourceType.IMAGE` + `IMAGE_EXTENSIONS` 可能对类型判别有用。

---

## 三、合并优先级建议

### ✅ 高优先级 — 建议合并

| 序号 | 内容 | 理由 | 预计工作量 |
|------|------|------|-----------|
| 1 | **Delta 算法源码** | `AlgorithmFactory.cpp` 已引用但源码缺失，属于**补全缺失依赖** | 30 分钟 |
| 2 | **LZMine 算法** | 新算法（KMP+DP），拓宽压缩范式选择 | 2 小时 |
| 3 | **LZMineCompressor** | LZMine 的算法工厂注册 + pybind 绑定 | 1 小时 |

### 🟡 中优先级 — 视需求决定

| 序号 | 内容 | 理由 | 预计工作量 |
|------|------|------|-----------|
| 4 | **preprocessor 模块** | 图片预处理管线（Delta 预处理 + stb 解析），与当前 ImageCompressor 互补 | 3 小时 |
| 5 | **Archiver** | 当前已有 WCX + archiver，功能重叠，仅在 WASM 场景下有用 | 1 小时 |

### 🔵 低优先级 — 建议暂不合并

| 序号 | 内容 | 理由 |
|------|------|------|
| 6 | **WASM 绑定** | 新部署目标，需要 Emscripten 工具链 + CI 支持 |
| 7 | **gui/core/** | 功能与当前 GUI 重叠，当前版本更完善 |
| 8 | **gui/widgets/** | 与当前 main_window.py 架构不兼容 |

---

## 四、不可合并的内容（冲突说明）

| 内容 | 冲突原因 |
|------|---------|
| `src/algorithm/Deflate.cpp` (yhc) vs `src/algorithm_new/Deflate.cpp` (algorithm) | 两份独立的 Deflate 实现，algorithm_new 版本更完善 |
| `src/core/AlgorithmFactory.cpp` (yhc) vs (algorithm) | 各自注册了不同的算法，algorithm 有 ADE support + Group 分组 |
| `src/gui/main.py` | 完全不同的入口架构（QApplication vs 模块化） |
| `src/processor/` | algorithm 有更复杂的 Pipeline + ChunkedStreamAdapter |
| `src/api/` (yhc) vs `src/api_new/` (algorithm) | algorithm_new 有更完整的 WCXProtocol + file API |
| `pybind_module.cpp` (yhc) vs `pybind_module_new.cpp` (algorithm) | algorithm_new 的绑定更全面（包含 Pipeline、Enum、ImageJpeg 等） |

---

## 五、推荐执行计划

### 阶段一：安全修复（30 分钟）
```
1. 从 yhc 移植 src/algorithm/Delta.hpp → src/algorithm/include/Delta.hpp
2. 从 yhc 移植 src/algorithm/Delta.cpp → src/algorithm/Delta.cpp
3. 更新 src/algorithm/CMakeLists.txt 添加 Delta.cpp
4. 编译验证
```

### 阶段二：新算法移植（2 小时）
```
1. 从 yhc 移植 LZMine.hpp / LZMine.cpp / KMPMatcher.hpp → src/algorithm/
2. 从 yhc 移植 LZMineCompressor.hpp → src/core_new/include/
3. 移植 LZMineCompressor.cpp → src/core_new/
4. 更新 AlgorithmFactory.hpp 添加 AlgorithmID::LZMine
5. 更新 ExternalCodecs.cpp 添加 LZMineCompressor 实现
6. 更新 api.cpp compress/decompress dispatch
7. 更新 WCXProtocol.cpp algo_code 映射
8. 更新 pybind_module_new.cpp 绑定
9. 更新 models.py / compressor.py / main_window.py GUI 注册
10. 编译 + 测试
```

### 阶段三：预处理管线（3 小时，可选）
```
1. 从 yhc 移植 src/preprocessor/ → src/algorithm_new/
2. 或创建独立的 src/preprocessor/ 并接入 CMakeLists
3. 接入 API 层
```

### 阶段四：WASM 支持（可选，需 Emscripten）
```
1. 安装 Emscripten SDK
2. 适配 CMakeLists.txt 为 WASM 目标
3. 测试 JS 互操作
```

---

## 六、当前状态快照

```
algorithm 分支已具备但 yhc 缺失：
  ✅ DPFlate (Deflate pipeline: LZDP LZ-parse → raw deflate stream → Huffman)
  ✅ Brotli / Zstd (外部库集成)
  ✅ WCX 协议 (v3 单文件容器，含 algo_code + flags)
  ✅ ADE 决策引擎 (算法自动选择 + SilentExplorer + 训练数据)
  ✅ GUI 流式压缩 + 文件夹归档
  ✅ JPEG/PNG 图片压缩 (stb_image 端到端)
  ✅ 流式 API (compressFile / decompressFile)
  ✅ Pipeline API (push / finish / pull)
  ✅ 算法对比 + 热力图可视化

yhc 分支有但 algorithm 缺失：
  🔶 Delta 算法源码 (AlgorithmFactory 引用但无实现)
  🔶 LZMine 算法 (KMP + DP + LiteralRun)
  🔶 图片预处理管线 (ImageParser + ImagePreprocessor)
  🔶 自定义 Archiver (多文件打包，0x503B0304 协议)
  🔶 WASM 绑定 (Emscripten)
```