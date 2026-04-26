# Web Compressor C++ Core Engine — Python 接口设计规范

> 版本：v1.0  
> 状态：待实施  
> 目标：统一 C++ ↔ Python 接口，一次设计，不再修改

---

## 1. 设计原则

| 原则 | 说明 |
|------|------|
| **命名统一** | C++ 与 Python 暴露名称一致，避免歧义 |
| **数据完整** | 所有结构体字段完整，不缺失 |
| **错误内置** | 统一返回结果对象，不抛异常到 Python |
| **批量支持** | 支持批量操作，减少 Python ↔ C++ 往返 |
| **向后兼容** | 本规范确定后，只增不减，不改已有签名 |

---

## 2. 数据结构

### 2.1 CompressorResult（压缩/解压结果）

C++ 定义：

```cpp
struct CompressorResult {
    std::vector<uint8_t> data;       // 压缩/解压后的字节流
    size_t original_size;            // 原始大小（字节）
    size_t compressed_size;          // 压缩后大小（字节）
    double compression_ratio;        // 压缩率（0.0 ~ 1.0）
    double time_ms;                  // 耗时（毫秒）
    bool success;                    // 是否成功
    std::string error_message;       // 错误信息（失败时有效）
};
```

Python 映射：

| 字段 | Python 类型 | 说明 |
|------|------------|------|
| `data` | `list[int]` | 字节流（元素范围 0~255） |
| `original_size` | `int` | 原始数据大小 |
| `compressed_size` | `int` | 压缩后数据大小 |
| `compression_ratio` | `float` | 压缩率，如 0.5 表示压缩了 50% |
| `time_ms` | `float` | 算法耗时，单位毫秒 |
| `success` | `bool` | `True` 表示成功，`False` 表示失败 |
| `error_message` | `str` | 失败原因，成功时为空字符串 |

---

### 2.2 WebFile（文件结构）

C++ 定义：

```cpp
struct WebFile {
    std::string name;                // 文件名（可含相对路径）
    std::vector<uint8_t> content;    // 文件内容字节流
};
```

Python 映射：

| 字段 | Python 类型 | 说明 |
|------|------------|------|
| `name` | `str` | 文件名，如 `"index.html"`、`"css/style.css"` |
| `content` | `list[int]` | 文件原始字节内容 |

> **注意**：旧版字段名为 `context`，现统一改为 `content`。

---

## 3. 压缩器接口

### 3.1 ICompressor（抽象基类）

所有压缩器必须实现以下接口：

```cpp
class ICompressor {
public:
    virtual ~ICompressor() = default;

    virtual auto compress(const std::vector<uint8_t>& data)
        -> CompressorResult = 0;

    virtual auto decompress(const std::vector<uint8_t>& data)
        -> CompressorResult = 0;

    virtual auto get_algorithm_name() -> std::string = 0;

    virtual auto compress_batch(const std::vector<std::vector<uint8_t>>& data_list)
        -> std::vector<CompressorResult>;
};
```

Python 映射：

| 方法 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `compress(data)` | `list[int]` | `CompressorResult` | 单文件压缩 |
| `decompress(data)` | `list[int]` | `CompressorResult` | 单文件解压 |
| `get_algorithm_name()` | 无 | `str` | 返回算法名称 |
| `compress_batch(data_list)` | `list[list[int]]` | `list[CompressorResult]` | 批量压缩 |

---

### 3.2 DeflateCompressor

```cpp
class DeflateCompressor : public ICompressor {
public:
    DeflateCompressor();
    auto compress(const std::vector<uint8_t>& data) -> CompressorResult override;
    auto decompress(const std::vector<uint8_t>& data) -> CompressorResult override;
    auto get_algorithm_name() -> std::string override;
};
```

Python 构造：

```python
compressor = core_engine.DeflateCompressor()
```

---

### 3.3 LZSSCompressor

```cpp
class LZSSCompressor : public ICompressor {
public:
    LZSSCompressor();
    auto compress(const std::vector<uint8_t>& data) -> CompressorResult override;
    auto decompress(const std::vector<uint8_t>& data) -> CompressorResult override;
    auto get_algorithm_name() -> std::string override;
};
```

Python 构造：

```python
compressor = core_engine.LZSSCompressor()
```

---

### 3.4 CompressorAlgorithm（算法枚举）

用于统一标识压缩算法类型，避免 Python 端使用字符串硬编码。

#### C++ 定义

```cpp
enum class CompressorAlgorithm {
    Deflate,    // Deflate (LZ77 + Huffman)
    LZSS,       // LZSS (Lempel-Ziv-Storer-Szymanski)
};
```

#### Python 绑定

```cpp
py::enum_<CompressorAlgorithm>(m, "CompressorAlgorithm")
    .value("DEFLATE", CompressorAlgorithm::Deflate)
    .value("LZSS", CompressorAlgorithm::LZSS)
    .export_values();
```

#### Python 映射

| 枚举值 | C++ 值 | 说明 |
|--------|--------|------|
| `CompressorAlgorithm.DEFLATE` | `CompressorAlgorithm::Deflate` | Deflate 算法 |
| `CompressorAlgorithm.LZSS` | `CompressorAlgorithm::LZSS` | LZSS 算法 |

#### Python 使用

```python
import core_engine

# 直接使用 C++ 暴露的枚举
algo = core_engine.CompressorAlgorithm.DEFLATE

# 或在 Python 端定义同名枚举（便于类型提示）
from enum import Enum

class Algo(Enum):
    DEFLATE = core_engine.CompressorAlgorithm.DEFLATE
    LZSS = core_engine.CompressorAlgorithm.LZSS
```

---

### 3.5 CompressorFactory（工厂类）

用于统一创建压缩器实例，避免 Python 端硬编码类名。

#### C++ 定义

```cpp
class CompressorFactory {
public:
    static auto create(CompressorAlgorithm algorithm)
        -> std::shared_ptr<ICompressor>;

    static auto list_algorithms() -> std::vector<std::string>;
};
```

#### Python 绑定

```cpp
py::class_<CompressorFactory>(m, "CompressorFactory")
    .def_static("create", &CompressorFactory::create)
    .def_static("list_algorithms", &CompressorFactory::list_algorithms);
```

#### Python 映射

| 方法 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `create(algorithm)` | `CompressorAlgorithm` | `ICompressor` | 根据算法枚举创建压缩器 |
| `list_algorithms()` | 无 | `list[str]` | 返回所有支持的算法名 |

#### Python 使用示例

```python
import core_engine

factory = core_engine.CompressorFactory()

# 查看支持的算法
print(factory.list_algorithms())
# 输出：["deflate", "lzss"]

# 创建压缩器
compressor = factory.create(core_engine.CompressorAlgorithm.DEFLATE)
print(compressor.get_algorithm_name())
# 输出："Deflate (LZ77Fast + Huffman)"
```

---

## 4. 打包器接口

### 4.1 Archiver

提供多文件打包/解包，以及打包+压缩/解压+解包的一键接口。

```cpp
class Archiver {
public:
    // 基础：打包 / 解包
    static auto pack(const std::vector<WebFile>& files)
        -> std::vector<uint8_t>;

    static auto unpack(const std::vector<uint8_t>& data)
        -> std::vector<WebFile>;

    // 一键：打包 + 压缩
    static auto pack_and_compress(
        const std::vector<WebFile>& files,
        CompressorAlgorithm algorithm) -> CompressorResult;

    // 一键：解压 + 解包
    static auto decompress_and_unpack(
        const std::vector<uint8_t>& data,
        CompressorAlgorithm algorithm) -> std::vector<WebFile>;
};
```

Python 映射：

| 方法 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `pack(files)` | `list[WebFile]` | `list[int]` | 多文件打包为字节流 |
| `unpack(data)` | `list[int]` | `list[WebFile]` | 字节流解包为文件列表 |
| `pack_and_compress(files, algorithm)` | `list[WebFile]`, `CompressorAlgorithm` | `CompressorResult` | 打包后压缩 |
| `decompress_and_unpack(data, algorithm)` | `list[int]`, `CompressorAlgorithm` | `list[WebFile]` | 解压后解包 |

---

### 4.2 打包协议格式

打包后的字节流结构：

```
[Magic Number]      4 bytes   0x503B0304
[File Count]        4 bytes   文件数量

=== 每个文件 ===
[Name Length]       2 bytes   文件名长度（含路径）
[File Name]         n bytes   文件名（UTF-8，可含子文件夹路径）
[File Size]         4 bytes   文件内容大小
[File Content]      n bytes   文件内容
```

> **说明**：`[File Name]` 存储的是**完整相对路径**，天然支持多重子文件夹嵌套。
> 路径分隔符使用 `/`（正斜杠），解压时根据操作系统自动转换。

### 4.3 多文件嵌套打包示例

#### 目录结构

```
website/
├── index.html
├── css/
│   ├── main.css
│   └── components/
│       └── button.css
└── js/
    ├── utils/
    │   └── helper.js
    └── app.js
```

#### 打包后的字节流

```
偏移     内容                          说明
─────────────────────────────────────────────────────
0000     [50 3B 03 04]                 Magic Number: 0x503B0304
0004     [00 00 00 06]                 File Count: 6

─── File 1 ─────────────────────────────────────────
0008     [00 0A]                       Name Length: 10
000A     "index.html"                   File Name
0014     [00 00 0E 00]                 File Size: 3584
0018     [3C 68 74 6D 6C 3E ...]       File Content: <html>...

─── File 2 ─────────────────────────────────────────
0E18     [00 0C]                       Name Length: 12
0E1A     "css/main.css"                 File Name
0E26     [00 00 08 00]                 File Size: 2048
0E2A     [62 6F 64 79 20 7B ...]       File Content: body { ... }

─── File 3 ─────────────────────────────────────────
162A     [00 19]                       Name Length: 25
162C     "css/components/button.css"    File Name (嵌套 2 层)
1645     [00 00 02 00]                 File Size: 512
1649     [2E 62 74 6E 20 7B ...]       File Content: .btn { ... }

─── File 4 ─────────────────────────────────────────
1849     [00 13]                       Name Length: 19
184B     "js/utils/helper.js"           File Name (嵌套 2 层)
185E     [00 00 04 00]                 File Size: 1024
1862     [66 75 6E 63 74 69 ...]       File Content: function helper() { ... }

─── File 5 ─────────────────────────────────────────
1C62     [00 09]                       Name Length: 9
1C64     "js/app.js"                    File Name (嵌套 1 层)
1C6D     [00 00 10 00]                 File Size: 4096
1C71     [63 6F 6E 73 6F 6C ...]       File Content: console.log("app");

─── File 6 ─────────────────────────────────────────
2C71     [00 0B]                       Name Length: 11
2C73     "README.md"                    File Name
2C7E     [00 00 01 00]                 File Size: 256
2C82     [23 20 57 65 62 73 ...]       File Content: # Website...
```

#### Python 端打包代码

```python
import core_engine

files = [
    core_engine.WebFile(name="index.html", content=read_bytes("website/index.html")),
    core_engine.WebFile(name="css/main.css", content=read_bytes("website/css/main.css")),
    core_engine.WebFile(name="css/components/button.css", content=read_bytes("website/css/components/button.css")),
    core_engine.WebFile(name="js/utils/helper.js", content=read_bytes("website/js/utils/helper.js")),
    core_engine.WebFile(name="js/app.js", content=read_bytes("website/js/app.js")),
    core_engine.WebFile(name="README.md", content=read_bytes("website/README.md")),
]

# 打包
packed = core_engine.Archiver.pack(files)
print(f"打包后大小：{len(packed)} bytes")

# 一键打包 + 压缩
result = core_engine.Archiver.pack_and_compress(
    files, core_engine.CompressorAlgorithm.DEFLATE
)
print(f"压缩率：{result.compression_ratio:.1%}")
```

#### Python 端解包还原代码

```python
import os

# 解压 + 解包
restored = core_engine.Archiver.decompress_and_unpack(
    result.data, core_engine.CompressorAlgorithm.DEFLATE
)

# 还原到磁盘
output_dir = "output/"
for f in restored:
    # f.name = "css/components/button.css"
    filepath = os.path.join(output_dir, f.name)
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, "wb") as fp:
        fp.write(bytes(f.content))
    print(f"还原：{f.name} ({len(f.content)} bytes)")
```

#### 路径限制

| 字段 | 大小 | 最大长度 |
|------|------|---------|
| `[Name Length]` | 2 bytes | 0~65535 字节 |

实际使用中完全够用：
- Windows 最大路径长度：260 字符
- Linux 最大路径长度：4096 字符
- 本协议支持：65535 字节

---

## 5. 完整 pybind11 绑定代码

```cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ICompressor.hpp"
#include "DeflateCompressor.hpp"
#include "LZSSCompressor.hpp"
#include "Archiver.hpp"

namespace py = pybind11;
using namespace compressor::core;

PYBIND11_MODULE(core_engine, m) {
    m.doc() = "Web Compressor C++ Core Engine";

    // ===== 数据结构 =====

    py::class_<CompressorResult>(m, "CompressorResult")
        .def(py::init<>())
        .def_readwrite("data", &CompressorResult::data)
        .def_readwrite("original_size", &CompressorResult::original_size)
        .def_readwrite("compressed_size", &CompressorResult::compressed_size)
        .def_readwrite("compression_ratio", &CompressorResult::compression_ratio)
        .def_readwrite("time_ms", &CompressorResult::time_ms)
        .def_readwrite("success", &CompressorResult::success)
        .def_readwrite("error_message", &CompressorResult::error_message);

    py::class_<WebFile>(m, "WebFile")
        .def(py::init<>())
        .def_readwrite("name", &WebFile::name)
        .def_readwrite("content", &WebFile::content);

    // ===== 压缩器 =====

    py::class_<ICompressor, std::shared_ptr<ICompressor>>(m, "ICompressor")
        .def("compress", &ICompressor::compress)
        .def("decompress", &ICompressor::decompress)
        .def("get_algorithm_name", &ICompressor::get_algorithm_name)
        .def("compress_batch", &ICompressor::compress_batch);

    py::class_<DeflateCompressor, ICompressor,
               std::shared_ptr<DeflateCompressor>>(m, "DeflateCompressor")
        .def(py::init<>());

    py::class_<LZSSCompressor, ICompressor,
               std::shared_ptr<LZSSCompressor>>(m, "LZSSCompressor")
        .def(py::init<>());

    // ===== 工厂 =====

    py::class_<CompressorFactory>(m, "CompressorFactory")
        .def_static("create", &CompressorFactory::create)
        .def_static("list_algorithms", &CompressorFactory::list_algorithms);

    // ===== 打包器 =====

    py::class_<Archiver>(m, "Archiver")
        .def_static("pack", &Archiver::pack)
        .def_static("unpack", &Archiver::unpack)
        .def_static("pack_and_compress", &Archiver::pack_and_compress)
        .def_static("decompress_and_unpack", &Archiver::decompress_and_unpack);
}
```

---

## 6. Python 端使用示例

### 6.1 单文件压缩

```python
import core_engine
from enum import Enum

class CompressorAlgorithm(Enum):
    DEFLATE = "deflate"
    LZSS = "lzss"

# 工厂模式创建压缩器
factory = core_engine.CompressorFactory()
compressor = factory.create(CompressorAlgorithm.DEFLATE)

# 压缩
data = list(b"Hello, World!" * 1000)
result = compressor.compress(data)

print(f"原始大小：{result.original_size}")
print(f"压缩大小：{result.compressed_size}")
print(f"压缩率：{result.compression_ratio:.1%}")
print(f"耗时：{result.time_ms:.2f}ms")

# 解压
decompressed = compressor.decompress(result.data)
assert decompressed.success
```

### 6.2 批量压缩

```python
data_list = [
    list(b"<html>...</html>"),
    list(b"body { color: red; }"),
    list(b"console.log('hello');"),
]

results = compressor.compress_batch(data_list)
for i, res in enumerate(results):
    print(f"文件 {i}: 压缩率 {res.compression_ratio:.1%}")
```

### 6.3 打包 + 压缩整个网站

```python
files = [
    core_engine.WebFile(name="index.html", content=list(b"<html>...</html>")),
    core_engine.WebFile(name="css/style.css", content=list(b"body{...}")),
    core_engine.WebFile(name="js/app.js", content=list(b"...")),
]

# 一键打包 + 压缩
result = core_engine.Archiver.pack_and_compress(files, CompressorAlgorithm.DEFLATE)

# 一键解压 + 解包
restored = core_engine.Archiver.decompress_and_unpack(result.data, CompressorAlgorithm.DEFLATE)
for f in restored:
    print(f"{f.name}: {len(f.content)} bytes")
```

---

## 7. 与当前代码的差异对比

| 项目 | 当前实现 | 本规范 |
|------|---------|--------|
| `CompressorResult.compression_ratio` | ❌ C++ 结构体缺失 | ✅ 添加 |
| `CompressorResult.success` | ❌ 缺失 | ✅ 添加 |
| `CompressorResult.error_message` | ❌ 缺失 | ✅ 添加 |
| `LZSSCompressor` 绑定 | ❌ 未绑定 | ✅ 绑定 |
| `CompressorFactory` | ❌ 不存在 | ✅ 新增 |
| `ICompressor.compress_batch` | ❌ 不存在 | ✅ 新增 |
| `Archiver.pack_and_compress` | ❌ 不存在 | ✅ 新增 |
| `Archiver.decompress_and_unpack` | ❌ 不存在 | ✅ 新增 |
| `WebFile.content` | ⚠️ 旧名 `context` | ✅ 统一为 `content` |

---

## 8. 实施建议

1. **修改 C++ 结构体**：在 `ICompressor.hpp` 中给 `CompressorResult` 添加缺失字段
2. **实现工厂类**：新增 `CompressorFactory.hpp/cpp`
3. **实现批量接口**：在 `ICompressor` 基类中提供默认实现
4. **实现一键接口**：在 `Archiver` 中添加 `pack_and_compress` / `decompress_and_unpack`
5. **更新 pybind**：按本规范重写 `pybind_module.cpp`
6. **更新 Python 封装层**：同步修改 `gui/core/engine.py` 中的调用代码

---

*本规范确定后，所有接口签名冻结，后续只增不减。*
