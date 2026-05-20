# algorithm_new 与 GUI 绑定现状

## 本次日志中的直接原因

`gui/engine/bridge.py` 在 PyInstaller 环境下使用了未定义变量 `_meIPASS`（应为 `getattr(sys, "_MEIPASS")` 得到的 `_meipass`），导致 **任何压缩** 在 `CompressionEngine.__init__` → `get_core_engine()` 时即崩溃，与算法选择无关。

已修复：拼写 + 打包路径 `/_MEIPASS/core_engine/` + 按 `module_name*.pyd` 查找。

## 两套后端分工

| 模块 | C++ 栈 | GUI 能否完整跑通 |
|------|--------|------------------|
| `core_engine` | `algorithm` + `api` + `processor` + `archiver` + `ade` | **是**（当前 `run.bat` 只打包此模块） |
| `core_engine_new` | `algorithm_new` + `core_new`（文件 API 为主） | **否**（仅部分 LZDP 内存适配） |

仅把 CMake 链到 `algorithm_new` **不等于** GUI 已切换；Python 仍通过 `core_engine` 调用 **整条** 压缩管线。

## `core_engine_new` 已有

- `CompressorResult`
- `LZDPCompressor`（内存路径经临时文件适配，见 `pybind_core_new.cpp`）
- `LZDPCompressorRaw` / `LZDPCompressorConfig`
- `test_algorithm_new()`

## GUI 仍依赖旧 `core_engine` 的「砖块」（迁移清单）

### 压缩器类（`compressor._create_compressor`）

- [x] LZDP — `core_engine_new` 有适配器
- [ ] LZSS — `LZSSCompressor`
- [ ] DEFLATE — `DeflateCompressor`
- [ ] DPFLATE — `DPFlateCompressor`
- [ ] GZIP / BROTLI / ZSTD — `GzipCompressor` 等

### 流式 / 大文件（`compressor.smart_compress*`）

- [ ] `Pipeline` / `PipelineCompressResult`
- [ ] `DeflatePipelineParams` / `DpflatePipelineParams`
- [ ] `smart_compress_file` / `smart_decompress_file`（C++ `api::Pipeline`）

### WCX 容器（`file_protocol`）

- [ ] `pack_wcx` / `unpack_wcx`

### ADE（`gui/ade/*`）

- [ ] `ADE` / `ParameterOptimizer` / `ParamRegressorNet`
- [ ] 流式探索 `streaming_explore` 用的原生 API

### 演示 / 可视化

- [ ] `get_dp_visualization`（`token_parser` / 压缩演示）
- [ ] Huffman / block 相关导出

### 打包（`run.bat`）

- [ ] 若默认走 `core_engine_new`：需 `--target core_engine_new` 并 `--add-data ...;core_engine` 或改 `bridge` 子目录名
- [ ] MinGW 运行时 DLL 复制（目前只对 `core_engine` target POST_BUILD）

## 推荐迁移顺序

1. **保持 GUI 绑定 `core_engine`**，在 CMake 里让旧 `algorithm` 库内部转发到 `algorithm_new`（或逐步替换源文件），这样 Python **零改动**。
2. 或扩充 `pybind_core_new.cpp` + `api_new` 的 WCX/Pipeline 绑定，达到与 `pybind_module.cpp` 同 surface 后再切换 `COMPRESSION_TOOL_USE_ALGORITHM_NEW=1`。
3. 每完成一类 API，用 `tests/python/test_streaming_memory_roundtrip.py` 与 GUI 压缩/解压各测一条。

## 环境变量

- `COMPRESSION_TOOL_USE_ALGORITHM_NEW=1` — `bridge` 优先加载 `core_engine_new`，失败则回退 `core_engine`。
