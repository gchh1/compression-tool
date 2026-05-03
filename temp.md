GUI 也完全不兼容。问题出在两个地方：**pybind11 绑定层**和 **engine.py 调用方式**，两者都依赖已删除的旧接口。

---

### 当前 GUI → C++ 通信路径

```
engine.py
  └── import core_engine           ← pybind11 模块（编译自 pybind_module.cpp）
        ├── .DeflateCompressor()   ← 旧类，不存在
        ├── .LZSSCompressor()      ← 不存在
        ├── .LZMineCompressor()    ← 旧 ICompressor 包装
        └── .Archiver              ← 已删除的 preprocessor 模块
```

`import core_engine` 永远失败，整个 GUI 的实际行为是 **所有操作抛 RuntimeError**——根本跑不起来。

### 需要修改的内容

**1. `src/bindings/pybind/pybind_module.cpp` — 完全重写**

删除所有旧绑定，替换为新 API：

```cpp
// 删除：
// - ICompressor / DeflateCompressor / LZSSCompressor / LZMineCompressor
// - Archiver / File / CompressorResult (旧版)

// 替换为：
py::class_<api::CompressResult>(m, "CompressResult")
    .def_readonly("data", ...)
    .def_readonly("original_size", ...)
    .def_readonly("compressed_size", ...)
    .def_readonly("success", ...);

py::class_<api::WebFile>(m, "WebFile")
    .def_readwrite("name", ...)
    .def_readwrite("content", ...);

py::enum_<AlgorithmID>(m, "AlgorithmID")
    .value("None", AlgorithmID::None)
    .value("Deflate", AlgorithmID::Deflate)
    .value("Inflate", AlgorithmID::Inflate)
    .value("DeltaEncode", AlgorithmID::DeltaEncode)
    .value("DeltaDecode", AlgorithmID::DeltaDecode);

m.def("compress", &api::compress);
m.def("decompress", &api::decompress);
m.def("pack_and_compress", &api::packAndCompress);
m.def("decompress_and_unpack", &api::decompressAndUnpack);
```

**2. `src/gui/core/engine.py` — 重写 CompressionEngine**

当前每个算法 new 一个对象、传 `list(data)`。改为调用新 API：

```python
def compress(self, data: bytes, algorithm: AlgorithmType):
    chain = self._to_chain(algorithm)  # AlgorithmType → [AlgorithmID]
    result = self._engine.compress(list(data), chain)
    return result

def _to_chain(self, algo):
    if algo == AlgorithmType.DEFLATE:
        return [core_engine.AlgorithmID.Deflate]
    if algo == AlgorithmType.LZMINE:
        return [core_engine.AlgorithmID.LZMine]  # 如果注册了的话
    ...
```

`pack_files` / `unpack_archive` 同理，用 `pack_and_compress` / `decompress_and_unpack`。

**3. `src/gui/core/models.py` — 清理 AlgorithmType**

删除不存在的算法：

```python
class AlgorithmType(Enum):
    AUTO = "auto"
    DEFLATE = "deflate"
    # LZSS = "lzss"        ← 未编译在 algorithm CMake 中
    # HUFFMAN = "huffman"  ← HuffmanTree 不是独立压缩器
    LZMINE = "lzmine"       ← 保留（虽然也不兼容 IAlgorithm）
    NONE = "none"
```

**4. `AlgorithmSelector` (main_window.py:389)** — 同步算法列表

删除 LZSS、Huffman 下拉项，只保留实际可用的。

**5. `CMakeLists.txt` — 启用 pybind 构建**

当前 `BUILD_PYTHON=OFF`（我刚关的）。改回 `ON` 并确保 pybind_module 链接 `libapi.a`：

```cmake
target_link_libraries(core_engine PRIVATE api)
```

---

### 总结

| 文件 | 改动类型 | 工作量 |
|---|---|---|
| `pybind_module.cpp` | 完全重写 | ~80 行 → ~50 行 |
| `engine.py` | 重写 compress/decompress/pack/unpack | ~40 行 |
| `models.py` | 删除 LZSS、Huffman 枚举值 | 2 行 |
| `main_window.py` | 同步算法下拉列表 | 3 行 |
| `CMakeLists.txt` | 恢复 BUILD_PYTHON=ON，link api | 2 行 |

要我执行这些修改吗？