# API / Pybind 绑定栈审计

> 审计日期：2026-05-19  
> 范围：`src/bindings/` 下全部 Python 绑定，对照 **物理目录**（`algorithm` / `core` / `api` 与 `*_new`）及 **CMake 链接库**。  
> GUI / `./run` 实际加载模块：`**core_engine`**（`src/bindings/pybind/`）。

---

## 1. 结论摘要


| 问题                                                                    | 答案                                                                                           |
| --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `core_engine` 是否链接旧 `libalgorithm.a` / `libcore.a` / `libapi.a`？      | **否**。仅链接 `algorithm_new`、`core_new`、`api_new`、`ade`、`utils`。                                |
| 为何代码里常见 `compressor::core`、`compressor::algorithm`、`compressor::api`？ | **命名空间与目录名不一致**：新栈在 `*_new` 目录下仍使用 `compressor::core` / `algorithm` / `api` 命名空间（与旧栈同名，易混淆）。 |
| 旧 `src/algorithm`、`src/core`、`src/api` 是否仍被构建？                        | **是**（根 `CMakeLists.txt` 仍 `add_subdirectory`），主要用于 **遗留 CTest 可执行文件**，不进入 `core_engine`。    |
| 是否存在“绑定仍指向旧文件夹”的源文件？                                                  | **默认构建的 `core_engine` 无**；仓库内有 **未编入 CMake 的实验绑定**（见 §4）。                                    |


---

## 2. Pybind 模块一览


| Python 模块         | CMake 目标          | 编译源文件                                 | `target_link_libraries`                                | GUI 使用                                                 |
| ----------------- | ----------------- | ------------------------------------- | ------------------------------------------------------ | ------------------------------------------------------ |
| `**core_engine`** | `core_engine`     | `pybind_module.cpp`, `pybind_ade.cpp` | `api_new`, `core_new`, `algorithm_new`, `ade`, `utils` | **是**（`gui.engine.bridge`）                             |
| `core_engine_new` | `core_engine_new` | `pybind_module_new.cpp`               | `api_new`, `core_new`, `algorithm_new`                 | **否**（当前 **编译失败**：误用不存在的 `compressor::api_new::` 命名空间） |


`core_engine` 的 include 路径（`pybind/CMakeLists.txt`）仅包含：

- `src/core_new/include`
- `src/algorithm_new/include`
- `src/api_new/include`
- `src/archiver/include`
- `src/utils/include`
- `src/ade/include`

**不包含** `src/core/include`、`src/algorithm/include`、`src/api/include`。

---

## 3. `core_engine` 绑定明细表

说明列：

- **源码目录**：头文件/实现所在物理路径。
- **命名空间**：C++ 符号命名空间（≠ 目录名）。
- **旧栈？**：是否链接或包含 `src/algorithm`、`src/core`、`src/api`（无 `_new`）。

### 3.1 公共类型与调试


| Python 符号                                | C++ 类型 / 函数        | 源码目录       | 命名空间                | 旧栈？ |
| ---------------------------------------- | ------------------ | ---------- | ------------------- | --- |
| `CompressorResult`                       | `CompressorResult` | `core_new` | `compressor::core`  | 否   |
| `ICompressor`                            | `ICompressor`      | `core_new` | `compressor::core`  | 否   |
| `enable_debug_log` / `disable_debug_log` | `debug::DebugLog`  | `utils`    | `compressor::debug` | 否   |


### 3.2 内存压缩器（GUI `compress()` 主路径）


| Python 符号           | C++ 类型              | 实现文件                          | 算法实现目录                               | 旧栈？ |
| ------------------- | ------------------- | ----------------------------- | ------------------------------------ | --- |
| `DeflateCompressor` | `DeflateCompressor` | `core_new/GuiCompressors.cpp` | `algorithm_new`（`Deflate` pipeline）  | 否   |
| `LZSSCompressor`    | `LZSSCompressor`    | `core_new/GuiCompressors.cpp` | `algorithm_new`（`LZSS` pipeline）     | 否   |
| `LZDPCompressor`    | `LZDPCompressor`    | `core_new/GuiCompressors.cpp` | `algorithm_new`（`LZDP` pipeline）     | 否   |
| `DPFlateCompressor` | `DPFlateCompressor` | `core_new/GuiCompressors.cpp` | `algorithm_new`（`DPFlate` pipeline）  | 否   |
| `GzipCompressor`    | `GzipCompressor`    | `core_new/ExternalCodecs.cpp` | zlib（外链，非 `algorithm/`）              | 否   |
| `BrotliCompressor`  | `BrotliCompressor`  | `core_new/ExternalCodecs.cpp` | Brotli（`third_party` / FetchContent） | 否   |
| `ZstdCompressor`    | `ZstdCompressor`    | `core_new/ExternalCodecs.cpp` | 外链 codec                             | 否   |


### 3.3 LZDP 可视化（DP 面板）


| Python 符号             | C++ 类型                       | 源码目录                                 | 命名空间                    | 旧栈？ |
| --------------------- | ---------------------------- | ------------------------------------ | ----------------------- | --- |
| `LZDPTriple`          | `algorithm::Triple`          | `algorithm_new`（`Visualization.hpp`） | `compressor::algorithm` | 否   |
| `LZDPDPCandidate`     | `algorithm::DPCandidate`     | `algorithm_new`                      | `compressor::algorithm` | 否   |
| `LZDPDPState`         | `algorithm::DPState`         | `algorithm_new`                      | `compressor::algorithm` | 否   |
| `LZDPDPStep`          | `algorithm::DPStep`          | `algorithm_new`                      | `compressor::algorithm` | 否   |
| `LZDPDPVisualization` | `algorithm::DPVisualization` | `algorithm_new`                      | `compressor::algorithm` | 否   |


`LZDPCompressor.get_dp_visualization` 调用链：`core_new` → `algorithm_new` 可视化结构体。

### 3.4 归档（与 LZ 算法栈独立）


| Python 符号  | C++ 类型                      | 源码目录       | 旧栈？ |
| ---------- | --------------------------- | ---------- | --- |
| `File`     | `File`                      | `archiver` | 否   |
| `Archiver` | `Archiver::pack` / `unpack` | `archiver` | 否   |


### 3.5 Pipeline / WCX API（流式与大文件）


| Python 符号                                 | 底层 C++ API                                     | 源码目录                                 | 命名空间               | 旧栈？ |
| ----------------------------------------- | ---------------------------------------------- | ------------------------------------ | ------------------ | --- |
| `AlgorithmID`                             | `AlgorithmID` 枚举                               | `core_new`（`AlgorithmFactory.hpp`）   | `compressor::core` | 否   |
| `LzdpWholeFileParams`                     | 结构体                                            | `core_new`                           | `compressor::core` | 否   |
| `DpflatePipelineParams`                   | 结构体                                            | `core_new`                           | `compressor::core` | 否   |
| `LzssPipelineParams`                      | 结构体                                            | `core_new`                           | `compressor::core` | 否   |
| `DeflatePipelineParams`                   | 结构体                                            | `core_new`                           | `compressor::core` | 否   |
| `PipelineCompressResult`                  | `api::CompressResult`                          | `api_new`                            | `compressor::api`  | 否   |
| `WCXUnpackResult`                         | `api::WCXUnpackResult`                         | `api_new`                            | `compressor::api`  | 否   |
| `pack_wcx`                                | `api::pack_wcx`                                | `api_new`（`WCXProtocol`）             | `compressor::api`  | 否   |
| `unpack_wcx`                              | `api::unpack_wcx`                              | `api_new`                            | `compressor::api`  | 否   |
| `pipeline_compress`                       | `api::compress`                                | `api_new` → `algorithm_new` pipeline | `compressor::api`  | 否   |
| `pipeline_decompress`                     | `api::decompress`                              | `api_new`                            | `compressor::api`  | 否   |
| `pipeline_compress_file`                  | `api::compressFile`                            | `api_new` + streaming pipeline       | `compressor::api`  | 否   |
| `pipeline_decompress_file`                | `api::decompressFile`                          | `api_new`                            | `compressor::api`  | 否   |
| `pipeline_compress_directory`             | `api::compressDirectory`                       | `api_new`（当前返回未实现）                   | `compressor::api`  | 否   |
| `set_streaming_compress_cancel_requested` | `api::set_streaming_compress_cancel_requested` | `api_new` / `core_new`               | `compressor::api`  | 否   |


### 3.6 ADE / 参数回归 / 进化算法（`pybind_ade.cpp`）


| Python 符号                                                                                       | 源码目录      | 链接进 `core_engine` | 旧栈？ |
| ----------------------------------------------------------------------------------------------- | --------- | ----------------- | --- |
| `RandomForestConfig`, `ADEResult`, `ADE`                                                        | `src/ade` | `libade.a`        | 否   |
| `ParamRegressorTrainConfig`, `ParamRegressorTrainMetrics`, `ParamRegressorNet`                  | `src/ade` | `libade.a`        | 否   |
| `AlgorithmParams`, `ParameterBounds`, `OptimizationResult`, `EAAlgorithm`, `ParameterOptimizer` | `src/ade` | `libade.a`        | 否   |


ADE **不**依赖 `libcore.a` / `libalgorithm.a`；`ADE_WITH_CORE` 仅表示可与 GUI 压缩器协同，运行时通过 Python 回调 fitness，而非链接旧 core。

---

## 4. 仓库内未编入默认 `core_engine` 的绑定源文件


| 文件                                    | 设计意图                                           | 是否 CMake 编译                      | 旧/新栈                                          |
| ------------------------------------- | ---------------------------------------------- | -------------------------------- | --------------------------------------------- |
| `pybind/pybind_visualization_new.cpp` | Brick 可视化、`NewLZDPCompressor` 等                | **否**（未列入 `pybind11_add_module`） | 头文件指向 `**core_new` + `algorithm_new`**        |
| `pybind/pybind_core_new.cpp`          | 实验性 `LZDPCompressorRaw` / `test_algorithm_new` | **否**                            | `**core_new` + `algorithm_new`**              |
| `pybind_new/pybind_module_new.cpp`    | 备用模块 `core_engine_new`                         | 目标存在但 **当前编译失败**                 | 意图为新栈；代码中 `api_new::` 命名空间 **写错**（应为 `api::`） |


这些文件**不应**视为“仍绑定旧 algorithm/core”，而是**未接入生产构建的实验代码**。

---

## 5. 旧栈（`src/algorithm`、`src/core`、`src/api`）现状


| 组件    | 目录                  | CMake 库              | 进入 `core_engine` | 主要消费者                                                                 |
| ----- | ------------------- | -------------------- | ---------------- | --------------------------------------------------------------------- |
| 旧算法   | `src/algorithm`     | `libalgorithm.a`     | 否                | `test_Deflate`, `test_lzss_noflag`, `test_cartesian_stream_matrix`, … |
| 旧核心封装 | `src/core`          | `libcore.a`          | 否                | 同上 + `ChunkedStreamAdapter`                                           |
| 旧 API | `src/api`           | `libapi.a`           | 否                | 同上                                                                    |
| 新算法   | `src/algorithm_new` | `libalgorithm_new.a` | **是**            | `core_engine`, `api_new`                                              |
| 新核心封装 | `src/core_new`      | `libcore_new.a`      | **是**            | `core_engine`, `api_new`                                              |
| 新 API | `src/api_new`       | `libapi_new.a`       | **是**            | `core_engine`                                                         |


### 5.1 易混淆：同名命名空间


| 命名空间                    | 旧栈头文件路径                       | 新栈头文件路径                      |
| ----------------------- | ----------------------------- | ---------------------------- |
| `compressor::core`      | `src/core/include/`           | `src/core_new/include/`      |
| `compressor::algorithm` | `src/algorithm/include/`（若存在） | `src/algorithm_new/include/` |
| `compressor::api`       | `src/api/include/`            | `src/api_new/include/`       |


**判定绑定归属时以 CMake `target_include_directories` + `target_link_libraries` 为准**，不能只看命名空间字符串。

### 5.2 重复头文件名（仅旧栈仍有独立实现）

以下名称在 **旧 `src/core`** 与 `**src/core_new**` 均存在，但 `core_engine` 只包含 `**core_new**` 路径：

- `ICompressor.hpp`
- `GzipCompressor.hpp`（旧实现：`src/core/GzipCompressor.cpp`；新实现：`src/core_new/ExternalCodecs.cpp`）

---

## 6. 与 `bindings/README.md` 的差异

`src/bindings/README.md` 仍写“应严格包装 `api/`、避免直连 `algorithm/` / `core/`”。

**实际生产绑定（`core_engine`）**：

- Pipeline 经 `**api_new`**（命名空间仍为 `compressor::api`）；
- 内存压缩器经 `**core_new` 的 GUI shim** 直连 `**algorithm_new` pipeline**（符合当前架构，但未在 README 中更新）。

建议在迁移完成后更新 README，或将其指向本文档。

---

## 7. 维护检查清单

1. 修改绑定后执行：`cmake --build build_py --target core_engine`，确认 `linkLibs.rsp` **无** `libcore.a` / `libalgorithm.a` / `libapi.a`。
2. Python 烟测：`LZSSCompressor().get_algorithm_name()` 应返回含 `**algorithm_new`** 的字符串。
3. 新增 pybind 源文件时，必须写入 `pybind/CMakeLists.txt` 的 `pybind11_add_module(...)`，否则不会进入 `.pyd`。
4. 删除旧栈前，先对照 `tests/CMakeLists.txt` 中仍链接 `api`/`core`/`algorithm` 的测试目标。

---

## 8. 参考路径


| 用途         | 路径                                                |
| ---------- | ------------------------------------------------- |
| 生产 pybind  | `src/bindings/pybind/pybind_module.cpp`           |
| ADE pybind | `src/bindings/pybind/pybind_ade.cpp`              |
| 链接配置       | `src/bindings/pybind/CMakeLists.txt`              |
| GUI 加载     | `src/gui/engine/bridge.py` → `import core_engine` |
| 新栈 API 实现  | `src/api_new/api.cpp`                             |
| 新栈 GUI 压缩器 | `src/core_new/GuiCompressors.cpp`                 |


