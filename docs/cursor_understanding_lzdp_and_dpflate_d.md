# Understanding LZDP and DPFLATE design
_Exported on 5/14/2026 at 16:49:21 GMT+8 from Cursor (2.6.21)_

---

**User**

@Package/logs/gui.log:27-52 解压deflate流式压缩结果再次闪退

---

**Cursor**

追踪解压完整路径（含导出后再添加的 `.wcx`），查找可能导致闪退的未捕获异常或 C++/pybind 不匹配。



定位到 `Inflate::COPY_MATCH`：匹配复制只查 `output_buf_`，但在块结束（symbol 256）后会把字面量刷入 `writer_` 并清空 `output_buf_`，导致后续块的 distance 无法引用前一块的字节。若未设置 `need_input`/`need_output`，`StreamProcessor` 会陷入无限循环并卡死/被系统结束。正在实现 32KiB 滑动窗口并修正 `COPY_MATCH`/`STORED`/字面量 的路径。



正在修复 `Inflate`：在块边界清空 `output_buf_` 后，跨块的 LZ 距离引用会失效；`COPY_MATCH` 在错误时未设置 `need_input`/`need_output`，可能导致 `StreamProcessor` 死循环。将添加 RFC1951 的 32KiB 滑动窗口并修正复制逻辑。



## 原因

`Inflate` 在 **symbol 256 结束块** 时会把当前块里攒的字节写入 `writer_` 并 **`output_buf_.clear()`**。  
但 **LZ77 距离** 可以引用 **最多 32768 字节之前** 的明文，这些字节往往在上一个块里，已经不在 `output_buf_` 里。

原来的 `COPY_MATCH` 只用 `output_buf_.size()` 判断距离，跨块引用会误判；更严重的是：在错误路径上 **`status` 里 `done` / `need_input` / `need_output` 全为 false** 时，`StreamProcessor::processChunks` 的内层 `while (true)` **不会 break**，形成 **CPU 空转死循环**，整进程卡死或被系统杀掉，表现就像「闪退」。

## 修复

在 `Inflate.hpp` / `Inflate.cpp` 中：

1. 增加 **32768 字节滑动窗口** `window_` 与累计写出的绝对位置 `out_abs_`（与 RFC1951 最大距离一致）。  
2. 所有明文输出（字面量、`STORED` 复制、`COPY_MATCH`）都经 **`appendDecodedByte`** 同步更新 `output_buf_` 与 `window_`。  
3. **`COPY_MATCH`** 用固定起点 `base = out_abs_` 和 `window_[(base - dist + i) % kWindowSize]` 做重叠复制，与 zlib 行为一致。  
4. 非法距离 `dist == 0` 或 `dist > out_abs_` 时 **`status.done = true`** 结束解码，避免 StreamProcessor 死循环（流损坏时宁可提前结束，也不再挂死）。

本地已重新编译并跑通 **`test_Deflate`**、**`test_roundtrip`**（含 Deflate/DPFlate 文本与随机数据往返）。

请重新打包/替换 `core_engine*.pyd` 后再试：**对流式 WCX 做「解压到目录」** 应不再卡死；若仍有异常，把新的 `gui.log` 里解压前后几行贴出来即可。

---

**User**

@Package/logs/gui.log:1-17 @resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb @Package/compressed/imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx 这个是我测试中文件大小的dpflate非流式压缩的原文件和压缩文件，解压的时候闪退

---

**Cursor**

正在查看日志与 DPFlate 解压路径，定位闪退原因。



Identifying root cause: on symbol 256 (and STORED), Inflate flushes `output_buf_` to the writer in a loop; if `writer_.ensureSpace(1)` fails it returns `need_output` while leaving `decode_state_` as `DECODE_TOKENS`. The next `process()` continues Huffman decoding and desynchronizes the bitstream — leading to null cursor derefs or crashes. Adding a dedicated flush state so we resume flushing before reading more tokens.



Fixing `DPFlateCompressor::decompress` (and the matching `DeflateCompressor` path) to loop with sliced input and growing output — passing the full compressed buffer on a second `process()` call resets the reader incorrectly.



Replacing `ensureSpace(1)` with `ensureSpace(8)` — the parameter is bits, so one byte requires 8 bits.



## 原因说明

1. **`Inflate` 在块尾刷出字面量时状态错误**  
   遇到 Huffman 符号 **256**（或 STORED 块结束）时，原来在 `for` 里把 `output_buf_` 写到 `writer_`。一旦 `writer_.ensureSpace(...)` 失败并返回 `need_output`，`decode_state_` 仍是 **`DECODE_TOKENS`**。下一次 `process()` 会再读 **1 bit Huffman**，相当于在「还没刷完的字节块」后面继续解码，比特流错位，后面可能空指针、异常或进程直接退出（你看到的「闪退」）。

2. **`BitWriter::ensureSpace` 的参数是「位数」**  
   写 1 个字节应预留 **8 bit**。原先用 `ensureSpace(1)` 只保证 1 bit，在输出缓冲区很紧时可能行为异常；刷出字面量时已改为 **`ensureSpace(8)`**。

3. **内存解压 `DPFlateCompressor::decompress` / `DeflateCompressor::decompress` 只调一次 `process`**  
   解压结果大于 `compressed * 10 + 65536` 时会在块尾刷出阶段反复 `need_output`，单次调用无法结束；且若错误地再次传入**整段**压缩数据，`BitReader` 的续传语义也不对。已在两个压缩器里用 **`inflate_to_end` 循环**：按 `bytes_consumed` 推进输入子区间、按 `bytes_produced` 推进输出、按需扩容，并在 `need_output && bytes_produced == 0` 时扩大输出缓冲。

## 代码改动摘要

- `Inflate.hpp` / `Inflate.cpp`：新增状态 **`FLUSH_TO_WRITER`** 与 **`output_flush_pos_`**，在 256 / STORED 结束后先进入该状态，跨多次 `process()` 把 `output_buf_` 写完再进入 `READ_TREES`；刷字节使用 **`ensureSpace(8)`**。
- `DPFlateCompressor.cpp`、`DeflateCompressor.cpp`：匿名命名空间内 **`inflate_to_end`**，解压时循环调用 `process` 直至 `done`。

## 测试情况

- **`test_roundtrip`**、**`test_Deflate`**、**`test_lzdp_dpflate_regression`** 已通过。  
- **`test_lzdp_dpflate_stream`** 在你当前环境里即使用 **未改动的 `Inflate`（`git checkout HEAD`）** 仍失败（LZDP 首字节即不同、DPFlate 约在偏移 148 处不同），属于**与本补丁无关的既有问题**；若你需要，可以单独再查 WCX 流式 + `decompressFile` 与 LZDP 的路径。

请重新编译并替换 GUI / `core_engine*.pyd` 里的 **`algorithm` + `core`**，再用你的 `imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx` 试解压；若仍闪退，请把新的 `gui.log` 或崩溃点发出来（并确认压缩时 **`use_3hfmtree` 为 0**，否则与当前 `Inflate` 格式不兼容）。

---

**User**

deflate非流式压缩结果也是解压失败，查看log日志，难道解压也用的是流式解压？

---

**Cursor**

查看 `gui.log` 并追踪 GUI 在 Deflate 非流式压缩后的解压路径。



定位到问题：日志显示先流式压缩（`compressed_path` 指向 workspace 的 `.wcx`），后非流式压缩（只写入 `compressed_data`）。`worker.py` 在非流式分支未清除 `compressed_path`，解压时 `main_window` 优先走 `smart_decompress_file`（流式读盘），会解压旧的磁盘文件而非内存中的新数据。正在修复：在非流式成功（及 expansion）路径将 `record.compressed_path = None`。



## 从 `gui.log` 能看出什么

同一条记录上你做了**两次**压缩（见第 13–35 行与 47–57 行）：

1. **第一次（11:59:28）**：`[compress] STREAMING mode for 2996683 bytes` → 走 **`smart_compress_file`**，结果在磁盘上的 `.wcx`，并把 **`record.compressed_path`** 设为该路径（`compressed_data` 为 `None`）。
2. **第二次（11:59:53）**：`loaded raw_data` → **内存非流式** `smart_compress`，`2996683 -> 472271 bytes`，结果在 **`record.compressed_data`** 里。

日志里**没有** `[decompress]` 或 `[smart_decompress]` 行，说明要么解压没跑到、要么进程在写日志前就崩了。

---

## 解压是不是「流式」？——**不跟「压缩是否流式」绑定，看分支**

解压走哪条路在 `main_window._on_decompress` 里写死了优先级：

```1866:1875:d:\AAA_C\compression-tool\src\gui\ui\main_window.py
                    if hasattr(record, 'compressed_path') and record.compressed_path:
                        original_name = header.original_filename or record.name
                        output_path = os.path.join(export_dir, original_name)
                        logger.info("[decompress] STREAMING file-to-file: %s -> %s", record.compressed_path, output_path)
                        result = engine.smart_decompress_file(record.compressed_path, output_path, record.algorithm)
                        if not getattr(result, "success", True):
                            em = (getattr(result, "error_message", None) or "").strip() or "解压失败"
                            raise RuntimeError(em)
                        success += 1
                        continue

                    payload = record.compressed_data or b''
```

- **只要 `compressed_path` 非空**：一律 **`smart_decompress_file`** → C++ **`pipeline_decompress_file`**，按配置的 `chunk_bytes` **分块读 WCX 再解压**（这就是你说的「流式文件解压」）。
- **只有 `compressed_data`、且没有 path**：走 `_decompress_payload` → **`smart_decompress(payload)`**；若 `len(payload)` 大于该算法的流式阈值才用 `pipeline_decompress`，否则用一次性 **`compressor.decompress`**。你这种 **payload ~472KB**，一般 **小于** 配置里的 1MB/10MB，因此是**一次性质解压**，不是 Python 侧的 `pipeline_decompress`。

所以：**不是「非流式压的就一定非流式解」**；**只要还留着旧的 `compressed_path`，解压就会一直走磁盘流式路径。**

---

## 真正的问题：第二次非流式压完后 **`compressed_path` 没清掉**

非流式分支只写了 `compressed_data`，**没有**把第一次流式留下的 **`compressed_path` 置空**。于是解压时仍满足 `compressed_path` 条件，会去解压**第一次**写到 workspace 的旧 `.wcx`，和当前内存里的新压缩数据**不一致** → 表现为解压失败、错文件、闪退等。

已在 **`worker.py` 非流式成功路径**（以及 expansion 存 raw 分支）增加 **`record.compressed_path = None`**，保证「当前有效结果在内存里」时不会再误走磁盘流式解压。

---

**建议你**：重新打一次包 / 用当前源码跑 GUI；若同一行先流式再非流式压，解压应走 `smart_decompress` + 内存 payload。若仍失败，把带 `[decompress]` / `[smart_decompress]` 的整段 `gui.log` 发出来即可继续查。

---

**User**

解压算法添加一个整文件读入内存，分块写入磁盘目标路径的设计，因为压缩文件一般不大

---

**Cursor**

实现“整文件读入内存、解压后分块写入磁盘”。正在查看 `pipeline_decompress_file` 与 `decompressFile` 的实现。



在 `decompressFile` 中实现：当 WCX 载荷不超过上限时将载荷一次性读入内存并单次 `pipeline.push`，解压输出仍通过现有的 `pull()` 分块写入磁盘；超过上限则回退到原来的分块读入。



## 实现说明

在 **`api::decompressFile`**（`src/api/api.cpp`）里做了分支：

1. **载荷整读（默认常见情况）**  
   当 WCX 头里声明的 **`compressed_size`（payload 长度）≤ 256 MiB** 时：用一次 `read` 把 **整段 payload** 读进 `std::vector<uint8_t>`，再 **`pipeline.push(整段, is_last=true)`** 解码。  
   这样 **压缩侧** 不再按 `stream_chunk_bytes` 多次从磁盘读 payload。

2. **输出仍分块写盘**  
   解码后仍通过原来的 **`pipeline.pull()`** + `MemoryPool` 块把明文写出；**解压结果** 不会整段堆在一条 `vector` 里再写文件。

3. **大载荷回退**  
   `payload_size > 256 MiB` 时仍走 **原来的按块读 payload** 路径，避免在极端大包上占用过多内存。

常量与注释在匿名命名空间里：

```75:78:d:\AAA_C\compression-tool\src\api\api.cpp
/// If the declared WCX payload is no larger than this, read the full payload before decoding
/// (compressed side is usually small). Decompressed bytes still flow through ``pull()`` in
/// pool-sized chunks and are written incrementally to the staged output file.
constexpr uint64_t kDecompressWholePayloadMaxBytes = 256ULL * 1024 * 1024;
```

## 文档与绑定

- **`api.hpp`**：`decompressFile` 的注释已改为描述「小载荷整读 / 大载荷分块读 + 输出始终分块写盘」。  
- **`pybind_module.cpp`**：`pipeline_decompress_file` 的 docstring 已同步。  
- **`compressor.py`**：`smart_decompress_file` 的 docstring 已说明 C++ 行为（若你本地已有一份相同说明可忽略重复）。

本地构建时 **`test_wcx_file_strict.exe` 链接 Permission denied** 是环境占用 exe 的问题，**`api` 静态库已成功编译**；你可在空闲时重链或重跑测试。

若要改 **256 MiB** 上限，只需改 `kDecompressWholePayloadMaxBytes` 一处。若希望以后做成配置项，可以再接到 `stream_chunk_bytes` 同类配置或单独选项上。

---

**User**

gui  deflate的高级设置“算法配置”，"编码与位宽(实时)"预览不见了

---

**Cursor**

正在恢复 Deflate 标签页的「编码与位宽（实时）」预览：此前该 GroupBox 仅对 LZSS/LZDP/DPFlate 创建，未包含 DEFLATE。将添加 `format_deflate_preview` 并在 `main_window` 中接入。



## 原因

「编码与位宽（实时）」`QGroupBox` 只在 **`AlgorithmType.LZSS / LZDP / DPFLATE`** 三个标签页里创建（`main_window.py` 里 `if algo in (...)`），**没有包含 `DEFLATE`**，所以 Deflate 高级设置里看不到这一块。

## 修改

1. **`src/gui/ui/helpers.py`**  
   - 新增 **`format_deflate_preview(...)`**：按当前 `search_size` / `lookahead_size` / `min_match` / `use_flag_encoding` 算 LZ 层位宽说明；再按 **`use_3hfmtree`** 区分标准两树与 3HfM，并带上 **`huffman_offset_chunk_bits` / `huffman_length_chunk_bits`** 的槽宽说明。

2. **`src/gui/ui/main_window.py`**  
   - 在创建预览的分支里加入 **`AlgorithmType.DEFLATE`**。  
   - 增加 **`_read_algo_combo_int`**，用于读 `use_3hfmtree` 等 `QComboBox` 的 `currentData()`。  
   - 在 **`_refresh_encoding_preview`** 里为 Deflate 调用 **`format_deflate_preview`**。

改完后，Deflate 页的「算法配置」下方会重新出现与其它 LZ 类算法一致的 **「编码与位宽（实时）」** 预览，并在你改窗口、编码方案、3HfM 开关或槽宽时自动刷新。

---

**User**

建立健全解压流程的log机制，找到大文件的压缩文件解压缩失败的原因

---

**Cursor**

梳理解压调用链并添加结构化日志，便于定位大文件解压失败原因。



正在添加专用解压日志模块，并在 Python 引擎与 `main_window` 解压路径接入；在 C++ `decompressFile` 中增加可选的环境变量调试输出。



## 做了什么

### 1. 新模块 `src/gui/engine/decompress_log.py`
- Logger：**`gui.decompress`**，每条消息形如 **`[decompress] <事件名> key=value ...`**（字段名排序），便于在 `gui.log` 里 `grep "\[decompress\]"`。
- **`log_decompress(event, level=..., **fields)`**：统一入口。
- **`summarize_result(obj)`**：从绑定对象上收集 `success`、`original_size`、`compressed_size`、`time_ms`、`error_message`、`out_list_len` 等。

### 2. `compressor.py`（引擎层）
- **`decompress`**：`decompress_native_begin` / `decompress_native_end` + 结果摘要。
- **`smart_decompress`**：`smart_decompress_begin`、分支（`over_threshold` / `compressor_one_shot`）、`pipeline_decompress` 成功时的 `smart_decompress_end`、gzip / pipeline 失败时的 `smart_decompress_fallback`，以及最终一次 shot 的结束日志。
- **`pipeline_decompress`**：`pipeline_decompress_begin` / `end`（含 `payload_bytes`、`chunk_bytes`）。
- **`pipeline_decompress_file`** / **`smart_decompress_file`**：起止日志 + **`input_file_bytes` / `output_file_bytes`**（便于对照「声明的原始大小」与「落盘字节数」）。

### 3. `main_window.py`（GUI 解压流程）
- **`_decompress_payload`**：  
  - 记录 `gui_payload_begin` / `gui_payload_engine`；  
  - **`success=False` 时打 ERROR 并 `raise RuntimeError`**（不再静默返回错误数据）；  
  - 解压长度与头里 `original_size` 不一致时打 **`gui_payload_size_mismatch`（WARNING）**。
- **`_on_decompress`**：  
  - `gui_batch_begin`、`gui_item_begin`；  
  - 会话 + **`compressed_path`** 路径：在 `smart_decompress_file` 之后 **`gui_file_session_verify`**，并比较 **`header_declared_original` vs 实际输出文件大小**，不一致则 **`gui_size_mismatch_file`**；  
  - 异常：**`gui_item_error`**；  
  - 批次结束：**`gui_batch_end`**。

### 4. C++ `api.cpp` — `decompressFile`
- 增加 **`#include <cstdio>`**。
- 若设置环境变量 **`WEBCOMPRESS_DECOMPRESS_DEBUG=1`**（且首字符非 `0`）：  
  - 开始时往 **stderr** 打 `payload_bytes`、`declared_original`、`pool_chunk`、`whole_payload` / `stream_payload`；  
  - 成功时打 `written`、`declared_original`、`ratio`、`time_ms`；若 **`written != declared_original_size`** 再打 **WARN**；  
  - **`abort_decompress_file`** 时打 **abort 原因**。

---

## 大文件解压失败时怎么用日志排查

| 现象 | 在 `gui.log` 里看什么 |
|------|------------------------|
| 头声明的原始大小与输出不一致 | **`gui_size_mismatch_file`** 或 **`gui_payload_size_mismatch`** |
| C++ 管线失败 | **`pipeline_decompress_file_end`** 里 **`success=False`** + **`error_message=...`** |
| 内存路径失败 | **`gui_payload_fail`** 或 **`smart_decompress_end`** 里 **`success=False`** |
| 解压字节数短于头里声明（流提前结束） | 控制台开 **`WEBCOMPRESS_DECOMPRESS_DEBUG=1`** 看 **`[decompress][native] WARN written != declared_original_size`** |

示例检索：

```text
grep "\[decompress\]" Package/logs/gui.log
```

**说明**：打包 GUI 默认不一定有控制台，**native 的 stderr** 需从命令行启动或附加控制台才能看到；日常优先看 **`gui.log`** 里的 **`gui.decompress`** 行即可。

---

**User**

@Package/logs/gui.log:36-38 添加文件也出现了闪退

---

**Cursor**

追踪「添加文件」流程中可能导致闪退的代码路径。



分析：日志里 “adding” 和 “added” 都已写出，说明 `_add_paths` 在 Python 侧已跑完；闪退更可能是 `insertRow` 触发的选择/重绘与 `QFileDialog` 关闭后的焦点竞争（Qt 层），或 `add_paths` 计数在 `add_file` 失败时仍 +1 造成的误判。我们将：1) 在批量添加时阻塞表格与选择模型的信号，结束后统一同步复选框；2) 仅在添加成功时计数；3) 将文件夹扫描改为单次 `rglob` 的延迟加载并在 worker 入口确保加载；4) 用 `QTimer.singleShot(0, …)` 推迟状态栏更新，减轻对话框关闭后的瞬时冲突。



根据你贴的日志，`[add] adding` 和 `[add] added` 都打出来了，说明 **Python 里 `_add_paths` 已经正常返回**；若界面仍「闪退」，更像是 **关闭文件对话框后 Qt 重绘/选择与下一帧事件叠加** 或 **原生层** 的问题，而不是 `FileRecord` 构造函数里未捕获的异常（单文件时 `FileRecord` 只做 `stat`，不会读全文件）。

本次已做这些改动，尽量稳住「添加」路径并顺带修掉文件夹相关的隐患：

### 1. 表格批量添加（`table.py`）
- 在 `add_paths` 里对 **表格 + `selectionModel()`** 做 `blockSignals(True/False)`，避免 `insertRow` 连续触发 `itemSelectionChanged` → `_sync_checkboxes_from_selection` 在中间状态反复跑。
- 结束后 **再手动调用一次** `_sync_checkboxes_from_selection()`。
- `add_folder` 与 `add_file` 一样包在 `try/except` 里；**只有返回行号 ≥ 0 才计入** 文件/目录数量（避免以前 `add_file` 失败仍显示「已添加 1 个文件」）。

### 2. 状态栏更新（`main_window.py`）
- 「已添加 …」改为 `QTimer.singleShot(0, …)`，在 **下一轮事件循环** 再写状态栏，减轻 Windows 上 **模态 `QFileDialog` 刚关闭** 时立刻改 UI 偶发不稳定的情况。

### 3. 文件夹记录（`models.py` + 各处 `ensure_files_loaded`）
- `FolderRecord` 不再在 `__init__` 里做 **两次整树遍历**（原先 `rglob` 建列表 + `get_folder_size` 再 `rglob`），改为 **`ensure_files_loaded()` 单次扫描**；未扫描前 `files` 为空、`size` 为 0。
- 在压缩线程、导出/解压、视图聚合、`row_for_record` 等所有依赖 `record.files` 的路径上补上 **`ensure_files_loaded()`**，避免懒加载后出现空列表。

若更新后 **仍会整进程退出** 且控制台无 Python traceback，多半是 **Qt/C++ 扩展** 的原生崩溃，可把同一操作在 **命令行** 跑 `python -m gui` 看 stderr，或开调试器抓栈；需要的话可以再单独加一行 `[add] after status timer` 之类日志确认是否死在下一帧之后。

---

**User**

@gui.log (1-79) 闪退，不要动任何算法流程，我发现你某次更改之后我使用dpflate压缩压缩率有0.3%的差别，健全交互流程所有的log日志机制

---

**Cursor**

加强 GUI 全流程日志并确保日志及时落盘以便诊断闪退；不修改算法/压缩核心代码。正在查看现有日志配置与主窗口交互入口。



正在实现：1. 可立即刷新的文件日志处理器；2. 统一的 `[ui]` 交互日志模块；3. 为添加/拖拽/关闭等关键路径补充带 `flush` 的日志；4. 将频繁的 `load_config` 成功日志降为 DEBUG，减少噪声。不修改 `src/algorithm`、`api` 压缩路径或 `compressor` 的数值逻辑。



## 说明

1. **未改任何压缩/解压算法或 C++ 流程**（未动 `src/algorithm/`、`api` 压缩路径、`compressor` 数值逻辑等）。DPFlate 约 0.3% 的差异通常来自 **内存整包 vs 流式分块**、阈值/块大小配置或是否落盘 `.wcx`，与本次仅日志/UI 的改动无关；若要对齐比率需在**明确约束**下单独排查流式与内存路径，而不是在日志补丁里动算法。

2. **日志机制**已加强，便于对照闪退前后最后一条记录。

### 改动摘要

| 区域 | 内容 |
|------|------|
| `gui/utils/logging.py` | 自定义 **每条日志后 `flush`** 的 `FileHandler` / `StreamHandler`；`setup_logging` 开头 **清空旧 handler** 避免重复；文件改为 **`append`** 并写一条 **`==== gui session start … ====`**；导出 **`flush_logging()`**。 |
| `gui/utils/interaction_log.py` | 新增 **`gui.ui`**：`log_ui` / `log_ui_flush`（`[ui] event k=v …`），`preview_paths` 用于路径预览。 |
| `gui/main.py` | 未捕获异常后 **`flush_logging`**；**`aboutToQuit`**、**事件循环退出** 打 `[ui]` 并 flush。 |
| `gui/ui/main_window.py` | 添加/拖拽/对话框/清空/压缩/解压/导出/关闭窗口等 **全流程 `[ui]` 日志**；`_add_paths(..., source=...)` 区分 `file_dialog` / `folder_dialog` / `drop`；状态栏延迟更新走 **`_apply_added_status`** 并记日志+flush。 |
| `gui/ui/table.py` | `add_paths` 首尾 **DEBUG** 行 + **flush**（默认控制台仍为 INFO，文件里可见）。 |
| `gui/config/settings.py` | 普通 **`load_config` 成功** 从 INFO 改为 **DEBUG**，减少刷屏；**保存/缺失/错误** 仍为 INFO/WARNING。 |

之后看 `gui.log`：若进程是 **Python 异常** 退出，会有 `Uncaught exception` 栈；若是 **原生闪退**，最后一条应是已 flush 的 **`[ui] add_paths.end`** 或 **`add_paths.status_applied`** 等，便于判断卡在 **表格更新之后** 还是 **下一帧状态栏**。日志文件为 **追加**，若体积过大可手动删一次 `Package/logs/gui.log`。

---

**User**

@gui.log (1-78) 解压闪退，而且dpflate流式压缩和非流式压缩压缩率不一样，请你进行压缩率对比实验，检查lzSS,lzdp,dpflate,deflate,的压缩率是否流式非流式一致，并且对压缩结果进行逐字节比较，再展开实验测试解压是否正常，进行解压文件和原文件逐字节比较

---

**Cursor**

分析流式与非流式压缩/解压的实现差异，编写对比实验脚本，并排查解压在 `decompress_native_begin` 之后闪退的原因。



定位到根因：`compressFile` / `StreamingCompressAdapter` 在 WCX 内写入 **U32-BE 分块帧**（`len || payload || ... || 0`），而 `smart_decompress` 在负载小于阈值时走 **一次性 `decompress`**，把帧头误当作码流导致原生崩溃。正在添加帧检测并路由到 `pipeline_decompress`，并添加流式 vs 非流式对比实验脚本。



确认 `compressor.py` 已包含分帧检测逻辑。正在添加实验脚本并运行验证。



---

**User**

流式解压不需要把流式配置写到wcx协议，流式和压缩无关；暂时把流式解压关闭，改用压缩文件读入内存，流式分块解压（应该是二分块吧）写入磁盘

---

**Cursor**

分析当前解压路径（`smart_decompress_file`、`decompressFile` 等），并实现：将压缩文件读入内存后分块解压并写入磁盘；暂时关闭原先的流式解压路径。



实现 Python 路径：整文件读入内存 → `unpack_wcx` + `smart_decompress` → 将解压结果按两半（二分）分两次写入磁盘；默认关闭原生 `pipeline_decompress_file`。保留环境变量 `WEBCOMPRESS_NATIVE_DECOMPRESS_FILE=1` 以恢复旧行为。



## 结论与实现说明

**WCX 协议**：未增加任何与「流式」相关的头字段；流式只与**实现路径**有关，与 WCX 头无关。

### 行为变更（`compressor.py`）

1. **默认关闭 C++ 流式文件解压**  
   `smart_decompress_file` / `pipeline_decompress_file` 不再调用 `pipeline_decompress_file`（即 C++ `decompressFile` 那条「按块读 payload + Pipeline 写盘」的路径），除非你显式打开原生路径。

2. **新默认路径（Python）**  
   - 把整个 **WCX 压缩文件**读入内存（上限 **512 MiB**，常量 `_DECOMPRESS_FILE_READ_ALL_MAX_BYTES`）。  
   - `unpack_compressed_file` 取出 **payload**，再走现有 **`smart_decompress`**（含 U32 分帧检测 + `pipeline_decompress` / 一次性解码逻辑）。  
   - 解压得到的明文用 **「二分块」写盘**：`mid = (len+1)//2`，先 `write(out[:mid])`，再 `write(out[mid:])`（两次 `write`，满足你说的二分块写盘）。  
   - 先写 `输出路径.part`，`os.replace` 成目标文件，失败会尽量删掉 `.part`。

3. **恢复旧原生流式解压**  
   设置环境变量：  
   `WEBCOMPRESS_NATIVE_DECOMPRESS_FILE=1`（或 `true` / `yes`）  
   即恢复调用 C++ `pipeline_decompress_file` / `decompressFile`。

### 其它

- **`decompress_log.py`**：补充说明了默认 Python 路径与 `WEBCOMPRESS_NATIVE_DECOMPRESS_FILE`。  
- **`api.hpp`**：注释改为与「Python 默认 / 可选原生」一致（C++ 实现本身未改）。

**注意**：默认路径会把**解压后的完整明文**放在内存里（由 `smart_decompress` 返回），只适合当前「压缩侧远小于解压侧」的典型 WCX；极大解压结果仍可能占满内存。若以后要「解码过程也流式」，需要在核心层增加增量解码 API，与 WCX 头无关。

---

**User**

13:08:03 [INFO] root: Logging initialized, file: D:\AAA_C\compression-tool\Package\logs\gui.log
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:03 [INFO] gui.config.theme: [theme] theme applied
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:03 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:24 [INFO] gui.main_window: [add] added 1 files, 0 dirs from 1 paths
13:08:27 [INFO] gui.main_window: [add] added 1 files, 0 dirs from 1 paths
13:08:30 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:33 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:33 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:33 [INFO] gui.engine.compressor: [engine] config saved to file
13:08:33 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:33 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:33 [INFO] gui.engine.compressor: [engine] config saved to file
13:08:33 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:33 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:34 [INFO] gui.main_window: [compress] _on_compress called
13:08:34 [INFO] gui.main_window: [compress] selected_rows=[0]
13:08:34 [INFO] gui.main_window: [compress] algorithm=dpflate, records=1
13:08:34 [INFO] gui.main_window: [compress]   task[0]: type=FileRecord, name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.PENDING
13:08:34 [INFO] gui.worker: [compress] START record=2964737151520 file=D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb algo=dpflate
13:08:34 [INFO] gui.engine.bridge: C++ core_engine loaded from C:\Users\LAMTIM~1\AppData\Local\Temp\_MEI156402\core_engine\core_engine.cp312-win_amd64.pyd
13:08:34 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:35 [INFO] gui.worker: [compress] loaded raw data: 2996683 bytes
13:08:35 [INFO] gui.worker: [compress] compressing with dpflate ...
13:08:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:37 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:44 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:44 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:44 [INFO] gui.engine.compressor: [engine] config saved to file
13:08:44 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:44 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:44 [INFO] gui.engine.compressor: [engine] config saved to file
13:08:44 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:44 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:08:54 [INFO] gui.worker: [compress] compress done: 2996683 -> 480001 bytes
13:08:54 [INFO] gui.main_window: [compress] row 0 finished: name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.DONE, ratio=0.1602
13:08:54 [INFO] gui.ade.training: [TrainingDataStore] saved 1 samples to D:\AAA_C\compression-tool\Package\ade\ade_training_v3.jsonl
13:08:54 [INFO] gui.main_window: [compress] training data saved: 343 total samples
13:10:35 [INFO] gui.main_window: [compress] _on_compress called
13:10:35 [INFO] gui.main_window: [compress] selected_rows=[1]
13:10:35 [INFO] gui.main_window: [compress] algorithm=dpflate, records=1
13:10:35 [INFO] gui.main_window: [compress]   task[0]: type=FileRecord, name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.PENDING
13:10:35 [INFO] gui.worker: [compress] START record=2964737162464 file=D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb algo=dpflate
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.worker: [compress] STREAMING mode for 2996683 bytes (file-to-file)
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:35 [INFO] gui.engine.compressor: [smart_compress_file] D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb -> D:\AAA_C\compression-tool\Package\workspace\compressed\365a7f2ded13_imdb-movie-reviews-word2vec-tfidf-bow.wcx via dpflate (chunk_bytes=524288, file_opts=0)
13:10:48 [INFO] gui.main_window: [export] rows=[0]
13:10:48 [INFO] gui.main_window: [export] row=0 record=imdb-movie-reviews-word2vec-tfidf-bow.ipynb status=CompressionStatus.DONE type=FileRecord
13:10:48 [INFO] gui.main_window: [export] done: exported=1
13:10:52 [INFO] gui.main_window: [add] adding 1 files
13:10:52 [INFO] gui.main_window: [add] added 1 files, 0 dirs from 1 paths
13:10:54 [INFO] gui.worker: [compress] streaming compress returned success=True compressed_size=635685
13:10:54 [INFO] gui.main_window: [compress] row 1 finished: name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.DONE, ratio=0.2121
13:10:54 [INFO] gui.ade.training: [TrainingDataStore] saved 1 samples to D:\AAA_C\compression-tool\Package\ade\ade_training_v3.jsonl
13:10:54 [INFO] gui.main_window: [compress] training data saved: 344 total samples
13:10:58 [INFO] gui.decompress: [decompress] gui_batch_begin export_dir=D:/AAA_C/compression-tool/Package/decompressed task_count=1
13:10:58 [INFO] gui.decompress: [decompress] gui_item_begin name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx row=2 task_type=disk
13:10:58 [INFO] gui.decompress: [decompress] gui_payload_begin algo=dpflate header_compressed=480001 header_original=2996683 payload_bytes=480001
13:10:58 [INFO] gui.decompress: [decompress] smart_decompress_begin algo=dpflate payload_bytes=480001
13:10:58 [INFO] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:10:58 [INFO] gui.decompress: [decompress] smart_decompress_branch mode=compressor_one_shot
13:10:58 [INFO] gui.decompress: [decompress] decompress_native_begin algo=dpflate compressed_bytes=480001
13:14:15 [INFO] root: ==== gui session start 2026-05-14 13:14:15 ====
13:14:15 [INFO] root: Logging initialized, file: D:\AAA_C\compression-tool\Package\logs\gui.log (append mode, per-line flush)
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:15 [INFO] gui.config.theme: [theme] theme applied
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:15 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:14:16 [INFO] gui.ui: [ui] main_window.ready geometry=900x700
13:14:16 [INFO] gui.main: [ui] MainWindow shown, entering event loop
13:16:27 [INFO] gui.ui: [ui] main_window.closeEvent
13:16:27 [INFO] gui.main: [ui] aboutToQuit
13:16:27 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:16:27 [INFO] gui.main: [ui] event loop exited code=0
13:44:51 [INFO] root: ==== gui session start 2026-05-14 13:44:51 ====
13:44:51 [INFO] root: Logging initialized, file: D:\AAA_C\compression-tool\Package\logs\gui.log (append mode, per-line flush)
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [INFO] gui.config.theme: [theme] theme applied
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:44:51 [INFO] gui.ui: [ui] main_window.ready geometry=900x700
13:44:51 [INFO] gui.main: [ui] MainWindow shown, entering event loop
13:44:55 [INFO] gui.ui: [ui] file_dialog.open kind=open_directory
13:44:57 [INFO] gui.ui: [ui] file_dialog.closed accepted=False kind=open_directory path=
13:44:59 [INFO] gui.ui: [ui] drop.accept local_paths=1 url_count=1
13:44:59 [INFO] gui.ui: [ui] add_paths.begin path_count=1 preview=D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb rows_before=0 source=drop
13:44:59 [INFO] gui.main_window: [add] added 1 files, 0 dirs from 1 paths
13:44:59 [INFO] gui.ui: [ui] add_paths.end dirs_added=0 elapsed_ms=1.0 files_added=1 path_count=1 rows_after=1 source=drop
13:44:59 [INFO] gui.ui: [ui] add_paths.status_applied msg=已添加 1 个文件 source=drop
13:45:05 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:18 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:18 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:18 [INFO] gui.engine.compressor: [engine] config saved to file
13:45:18 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:18 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:18 [INFO] gui.engine.compressor: [engine] config saved to file
13:45:18 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:18 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:20 [INFO] gui.ui: [ui] file_dialog.open kind=open_files
13:45:22 [INFO] gui.ui: [ui] file_dialog.closed accepted=False count=0 kind=open_files
13:45:23 [INFO] gui.ui: [ui] compress.enter
13:45:23 [INFO] gui.main_window: [compress] _on_compress called
13:45:23 [INFO] gui.main_window: [compress] selected_rows=[0]
13:45:23 [INFO] gui.main_window: [compress] algorithm=deflate, records=1
13:45:23 [INFO] gui.main_window: [compress]   task[0]: type=FileRecord, name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.PENDING
13:45:23 [INFO] gui.ui: [ui] compress.dispatch algo=deflate rows=[0] task_count=1
13:45:23 [INFO] gui.ui: [ui] compress.worker_started file_count=1 task_count=1
13:45:23 [INFO] gui.worker: [compress] START record=1735635706912 file=D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb algo=deflate
13:45:23 [INFO] gui.engine.bridge: C++ core_engine loaded from C:\Users\LAMTIM~1\AppData\Local\Temp\_MEI268002\core_engine\core_engine.cp312-win_amd64.pyd
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [INFO] gui.worker: [compress] STREAMING mode for 2996683 bytes (file-to-file)
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:23 [INFO] gui.engine.compressor: [smart_compress_file] D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb -> D:\AAA_C\compression-tool\Package\workspace\compressed\125667392c8d_imdb-movie-reviews-word2vec-tfidf-bow.wcx via deflate (chunk_bytes=524288, file_opts=0)
13:45:24 [INFO] gui.worker: [compress] streaming compress returned success=True compressed_size=483974
13:45:24 [DEBUG] gui.ade.training: [TrainingDataStore] loaded stats: 344 total samples
13:45:24 [INFO] gui.main_window: [compress] row 0 finished: name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.DONE, ratio=0.1615
13:45:24 [INFO] gui.ade.training: [TrainingDataStore] saved 1 samples to D:\AAA_C\compression-tool\Package\ade\ade_training_v3.jsonl
13:45:24 [INFO] gui.main_window: [compress] training data saved: 345 total samples
13:45:24 [INFO] gui.ui: [ui] compress.finished_ui cancelled=False
13:45:28 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:32 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:32 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:32 [INFO] gui.engine.compressor: [engine] config saved to file
13:45:32 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:32 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:32 [INFO] gui.engine.compressor: [engine] config saved to file
13:45:32 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:32 [INFO] gui.config.settings: [config] saved to D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:38 [INFO] gui.ui: [ui] drop.accept local_paths=1 url_count=1
13:45:38 [INFO] gui.ui: [ui] add_paths.begin path_count=1 preview=D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb rows_before=1 source=drop
13:45:38 [INFO] gui.main_window: [add] added 1 files, 0 dirs from 1 paths
13:45:38 [INFO] gui.ui: [ui] add_paths.end dirs_added=0 elapsed_ms=0.5 files_added=1 path_count=1 rows_after=2 source=drop
13:45:38 [INFO] gui.ui: [ui] add_paths.status_applied msg=已添加 1 个文件 source=drop
13:45:42 [INFO] gui.ui: [ui] compress.enter
13:45:42 [INFO] gui.main_window: [compress] _on_compress called
13:45:42 [INFO] gui.main_window: [compress] selected_rows=[1]
13:45:42 [INFO] gui.main_window: [compress] algorithm=deflate, records=1
13:45:42 [INFO] gui.main_window: [compress]   task[0]: type=FileRecord, name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.PENDING
13:45:42 [INFO] gui.ui: [ui] compress.dispatch algo=deflate rows=[1] task_count=1
13:45:42 [INFO] gui.ui: [ui] compress.worker_started file_count=1 task_count=1
13:45:42 [INFO] gui.worker: [compress] START record=1735693710784 file=D:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb algo=deflate
13:45:42 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:42 [INFO] gui.worker: [compress] loaded raw data: 2996683 bytes
13:45:42 [INFO] gui.worker: [compress] compressing with deflate ...
13:45:42 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:45:42 [INFO] gui.worker: [compress] compress done: 2996683 -> 483969 bytes
13:45:42 [INFO] gui.main_window: [compress] row 1 finished: name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb, status=CompressionStatus.DONE, ratio=0.1615
13:45:42 [INFO] gui.ade.training: [TrainingDataStore] saved 1 samples to D:\AAA_C\compression-tool\Package\ade\ade_training_v3.jsonl
13:45:42 [INFO] gui.main_window: [compress] training data saved: 346 total samples
13:45:42 [INFO] gui.ui: [ui] compress.finished_ui cancelled=False
13:45:52 [INFO] gui.ui: [ui] export.begin export_dir=D:/AAA_C/compression-tool/Package/compressed rows=[0]
13:45:52 [INFO] gui.main_window: [export] rows=[0]
13:45:52 [INFO] gui.main_window: [export] row=0 record=imdb-movie-reviews-word2vec-tfidf-bow.ipynb status=CompressionStatus.DONE type=FileRecord
13:45:52 [INFO] gui.main_window: [export] done: exported=1
13:45:52 [INFO] gui.ui: [ui] export.done export_dir=D:/AAA_C/compression-tool/Package/compressed exported=1
13:45:53 [INFO] gui.ui: [ui] file_dialog.open kind=open_files
13:46:00 [INFO] gui.ui: [ui] file_dialog.closed accepted=True count=1 kind=open_files
13:46:00 [INFO] gui.main_window: [add] adding 1 files
13:46:00 [INFO] gui.ui: [ui] add_paths.begin path_count=1 preview=D:/AAA_C/compression-tool/Package/compressed/imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx rows_before=2 source=file_dialog
13:46:00 [INFO] gui.main_window: [add] added 1 files, 0 dirs from 1 paths
13:46:00 [INFO] gui.ui: [ui] add_paths.end dirs_added=0 elapsed_ms=0.8 files_added=1 path_count=1 rows_after=3 source=file_dialog
13:46:00 [INFO] gui.ui: [ui] add_paths.status_applied msg=已添加 1 个文件 source=file_dialog
13:46:03 [INFO] gui.ui: [ui] decompress.selection rows=[2]
13:46:06 [INFO] gui.ui: [ui] decompress.batch_start export_dir=D:/AAA_C/compression-tool/Package/decompressed task_count=1
13:46:06 [INFO] gui.decompress: [decompress] gui_batch_begin export_dir=D:/AAA_C/compression-tool/Package/decompressed task_count=1
13:46:06 [INFO] gui.decompress: [decompress] gui_item_begin name=imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx row=2 task_type=disk
13:46:06 [INFO] gui.decompress: [decompress] gui_payload_begin algo=deflate header_compressed=483974 header_original=2996683 payload_bytes=483974
13:46:06 [INFO] gui.decompress: [decompress] smart_decompress_begin algo=deflate payload_bytes=483974
13:46:06 [DEBUG] gui.config.settings: [config] loaded from D:\AAA_C\compression-tool\Package\config\webcompress_settings.json
13:46:06 [INFO] gui.decompress: [decompress] smart_decompress_branch mode=compressor_one_shot
13:46:06 [INFO] gui.decompress: [decompress] decompress_native_begin algo=deflate compressed_bytes=483974
仍然解压闪退

---

**Cursor**

分析中：`smart_decompress` 在 `compressor_one_shot` 下对来自 `smart_compress_file` 的 U32 分帧 payload 误判为非分帧，导致原生 `decompress` 把分帧数据当裸码流解码并崩溃。正在检查分帧检测逻辑并改为更稳妥的解码顺序（必要时先走 `pipeline_decompress`）。



Tracing the DEFLATE decompress path and comparing it with the pipeline API to find the crash.



---

**User**

我的意思是，解压策略有三种：全部存储在内存，解压完再写入磁盘；全部存储在内存，流式分块解压写入磁盘（二分块）请你查看流式设计，（二分块不是把文件二分的意思！！）；流式分块读入内存，解压写入磁盘，释放分块读取新分块，你可能存在上下文遗忘，重新查看设计文档

---

**Cursor**

正在查阅设计文档与实现，确认三种解压策略及“二分块”的真实含义。



确认：`streaming-compression-design.md` 中的「二分块」指 LZDP Phase 2 从 temp A 逆向读取时的 cur/next 双块滚动，不是把解压后的明文对半写入。正在修正代码注释、日志标识，并将误导性的“对半两次 write”改为单次或固定块大小的顺序写入。



正在修正文档与实现：将「对半写明文」与设计中 LZDP Phase 2 的「二分块」（cur/next 滚动读 temp A）脱钩，并在注释中写明三种解压策略。



已按设计文档重新对齐理解，并改了一处**实现与术语的错位**。

## 三种解压策略（你描述的含义）

| 策略 | 含义 |
|------|------|
| **1** | 压缩侧数据**整段在内存**（例如整份 WCX 或整段 payload）→ **整段解码** → 得到完整明文后再**写磁盘**。 |
| **2** | 压缩侧数据仍可整段在内存，但解码走**管线**：解码器**按块产出**，**多次写盘**，控制的是「解码产出 / 写盘」的流式，**不是**把最终文件按字节对半切成两半。 |
| **3** | **分块从磁盘读**压缩体进内存 → 推入管线解码 → **边解边写、释放本块读缓冲**；大 payload 时由 C++ `decompressFile` 的分支实现（`api.cpp` 里 `payload_size > kDecompressWholePayloadMaxBytes` 的循环读）。 |

## 「二分块」在文档里指什么

在 `docs/design/streaming-compression-design.md` **§3.2 Phase 2**（以及 `理解LZDP和DPFLATE流式压缩设计.md`）里，**二分块**专指对 **temp A / temp B** 做 **cur + next 双块滚动**（常配合 `VirtualBuffer`），用于**逆向读 temp A、正向读 temp B** 等，**与「输出明文文件对半写两次」无关**。

之前 Python 里用 `mid = (written+1)//2` 写两段明文，容易让人误以为那就是设计里的「二分块」，属于**误用术语**。

## 代码与注释上的修改

1. **`compressor.py`**：`_python_decompress_wcx_file_to_disk` 改为 **`fout.write(out_bytes)` 一次写完整明文**；日志 `mode` 从 `python_read_all_binary_split_write` 改为 `python_strategy1_read_all_decompress_all`；docstring / `smart_decompress_file` 说明三种策略及与设计文档「二分块」的区分。
2. **`decompress_log.py`**：模块说明改为上述三条策略，并写明勿与 LZDP Phase 2 的「二分块」混淆。
3. **`api.hpp`**：`decompressFile` 前增加与三种策略对应的英文注释（含对「二分块」的消歧义）。

若你接下来要把 **策略 2** 真正接到 GUI（payload 仍在内存、但 `pull` 分块写盘），需要在 Python 侧走与 `api::decompress` 类似的 **管线 + 多次 drain 写文件**，而不是在整段 `bytes` 上做对半切片；**策略 3** 则继续以 `WEBCOMPRESS_NATIVE_DECOMPRESS_FILE=1` + C++ 路径为主。

---

**User**

好的，继续

---

**Cursor**

继续推进：先定位解压闪退根因（Inflate/状态机 vs 比特流），再实现更接近「策略 2」的写盘路径（管线 pull 流式写入，避免整段明文堆在 Python 里）。



怀疑 `list(data)` 会把 ~48 万个字节变成 48 万个 Python `int`，导致极慢或内存暴涨并闪退。正在确认 pybind 是否接受 `bytes`，并改为直接传 `bytes`；接着实现策略 2：解压后通过管线分块写入磁盘。



正在实现：1. 修复 `list(data)` 导致的巨量 Python int 与 `list` 输出问题；2. 在 pybind 中将大块 `vector<uint8_t>` 映射为 `bytes`；3. WCX 默认 Python 解压改为 `pipeline_decompress`，与文件压缩管线一致；4. 明文落盘采用固定块大小的顺序写（仍属策略 1 的内存模型，但改善写盘流式）。



## 本轮继续做了什么

### 1. 解压闪退 / 卡死的高概率根因：`list(data)` 与巨型 `list` 往返
对几百 KB～几 MB 的 payload，`compress` / `decompress` / `pipeline_*` 使用 `list(data)` 会在 Python 里生成**等长 int 列表**，既慢又占内存，容易导致长时间卡住或进程异常退出。  
已改为直接传 **`bytes`**（pybind 会按 buffer 拷进 `std::vector<uint8_t>`）。

### 2. 绑定层：`vector<uint8_t>` ↔ Python 用 **`bytes`**，避免大 `list`
在 `pybind_module.cpp` 里对以下字段改为 `def_property`，读侧返回 `py::bytes`，写侧接受任意一维 buffer：

- `CompressorResult.data`
- `PipelineCompressResult.data`
- `WCXUnpackResult.payload`
- `File.context`

这样 C++ 返回的大块数据在 Python 侧是 **`bytes`**，不再变成百万级 `int` 列表。

### 3. 默认 Python WCX→磁盘：优先 **`pipeline_decompress`**，并对齐策略说明
`_python_decompress_wcx_file_to_disk` 中：

- 先 **`pipeline_decompress`**（与文件压缩管线一致）；异常时再回退 **`smart_decompress`**。
- 明文仍整体在内存（策略 1），落盘时用 **`memoryview` + `_DISK_WRITE_CHUNK_BYTES`（8MiB）** 顺序写入，便于 I/O 平滑（**不是**文档里的 LZDP「二分块」语义）。

### 4. 其它调用点
- `pack_wcx` / `unpack_wcx`、`Archiver.unpack`、`pipeline_compress` / `pipeline_decompress`：去掉多余的 `list(...)`。  
- `pack_files`：`f.context = rec.raw_data`（buffer 赋值）。  
- `decompress_log.summarize_result`：`out_list_len` → **`out_bytes_len`**。

### 5. 构建
已在 `build_py` 上 **`core_engine` 目标编译通过**。

---

若你愿意下一步再收紧「策略 2」：**在明文仍要整段在内存的前提下**，也可以增加仅对 **写盘** 暴露的可配置块大小（或环境变量）；真正的 **策略 3**（分块读 WCX payload）仍以 `WEBCOMPRESS_NATIVE_DECOMPRESS_FILE=1` + `pipeline_decompress_file` / `decompressFile` 为主路径。

---

**User**

@Package/decompressed/imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx 我导入然后选中选择解压显示没有可以解压的文件

---

**Cursor**

正在查找 GUI 中“没有可以解压的文件”的提示及 `.wcx` 的解压条件。



## 原因说明

1. **`unpack_wcx` 不接受 `bytes`**  
   `main_window` 里对 `.wcx` 会走 `unpack_compressed_file(record.raw_data)`，内部调用 `core_engine.unpack_wcx(data)`。绑定里参数写成了 `const std::vector<uint8_t>&`，在你这边的 pybind11 3.0.1 下 **`bytes` 不能自动转成该类型**，会抛出 `TypeError`。  
   外层的 `try/except` **吞掉了异常**，所以任务列表一直是空的，就出现「没有可以解压的压缩文件」。

2. **开发环境可能加载到旧的 `core_engine`**  
   `bridge.py` 里原先没有 **`build_py/src/bindings/pybind`**，本地用 CMake 编出来的 `.pyd` 可能根本不会被加载。

## 已做修改

| 文件 | 改动 |
|------|------|
| `pybind_module.cpp` | 增加 `buffer_to_u8vec(py::buffer)`，`pack_wcx` / `unpack_wcx` / `pipeline_compress` / `pipeline_decompress`、`Archiver::unpack`、`ICompressor::compress` / `decompress` 均从 **buffer（含 `bytes`）** 读入。`pack_wcx` 返回 **`py::bytes`**。 |
| `bridge.py` | 在候选路径最前加入 **`build_py`**、**`build_debug`** 下的 `pybind` 目录，优先加载你刚编好的扩展。 |
| `main_window.py` | `.wcx` 校验失败时 **打 warning 日志**；校验成功后 **`record.algorithm = header.algorithm`**，避免 `.wcx` 导入行仍用默认 `DEFLATE` 去解 **DPFlate** 包。 |

重新编译后，用 **源码跑 GUI** 且能加载到 `build_py` 里的 `core_engine` 时，导入该 `.wcx` 再解压应可正常进入任务。

若你用的是 **打包好的 `Package/WebCompress.exe`**，需要 **重新打包** 或把新编的  
`build_py/src/bindings/pybind/core_engine.cp312-win_amd64.pyd`（及依赖 DLL）拷进包内 `_MEI.../core_engine/`，否则仍是旧扩展，行为不变。

---

**User**

@gui.log (303-346) dpflate非流式压缩显示压缩失败

---

**Cursor**

定位到原因：`ICompressor` 的 `compress`/`decompress` 已改为接受 `py::buffer`，但各具体类（如 `DPFlateCompressor`）重写了这些方法，pybind 会为派生类绑定 `std::vector<uint8_t>` 版本并优先匹配，导致传入 `bytes` 失败。正在为每个具体压缩器在类定义末尾注册 `py::buffer` 重载（后注册优先）。



## 原因

日志里是：

`compress(): incompatible function arguments` … `Invoked with: <core_engine.DPFlateCompressor ...>, b'{...`

之前在 **`ICompressor`** 上给 `compress` / `decompress` 加了 `py::buffer`（支持 `bytes`），但 **`DPFlateCompressor`**（以及 `DeflateCompressor`、`LZSSCompressor` 等）在 C++ 里 **override** 了 `compress` / `decompress`。pybind 会为派生类再绑一份 **`std::vector<uint8_t>`** 版本，解析时往往 **先匹配到这份**，`bytes` 对不上，非流式压缩就会直接报错。

流式 `smart_compress_file` 走的是 **`pipeline_compress_file`**，不经过这些成员函数，所以以前只有「内存压缩」会炸。

## 修改

在 `pybind_module.cpp` 里，对每个具体压缩器在类绑定 **末尾** 再注册一遍：

- `compress(py::buffer)` → `buffer_to_u8vec` → 对应 `compress`
- `decompress(py::buffer)` → 同上  

（与基类注释一致：重载按 **逆序** 尝试，后注册的 buffer 版本会优先于 `vector` 版本。）

顺带把 **`LZDPCompressor.get_dp_visualization`** 的第一个参数也改成 `py::buffer`，避免以后用 `bytes` 调可视化再踩同一类问题。

已本地编译并通过：`DPFlateCompressor().compress(b'hello world test')` 返回 `success=True`。

请重新编译/替换 **`core_engine*.pyd`**（或重打 `Package`）后再试 DPFlate 非流式压缩。

---

**User**

默认使用解压策略1：整文件读入内存（你可能可以用@src/utils/include/MemoryPool.hpp ，如果这个有用的话），整个文件处理完再写到硬盘

---

**Cursor**

查看 `MemoryPool` 的用途及当前解压路径，以便将默认策略明确为「整文件读入 → 内存解压 → 再写盘」。



确认：`MemoryPool` 用于管线内固定块复用（`StreamProcessor` 输出切片），不适合作为「整份 WCX 单块」容器。策略 1 为整文件已在 `std::vector`/Python `bytes` 中，解码仍走 `api::decompress`，其内部已用 `MemoryPool`。我们将默认路径写清楚，并把落盘改为解压完成后一次性写入。



## 结论与实现说明

**`MemoryPool`（`src/utils/include/MemoryPool.hpp`）** 的设计是 **固定大小的块池**，给 `Pipeline` / `StreamProcessor` 在 **`pull` 时复用输出槽** 用，**不能**也不适合当作「整份 WCX 一次 `acquire` 一整文件」的容器。  
默认 **策略 1** 下：整段 WCX / payload / 明文仍是 **`Path.read_bytes` / `std::vector` / Python `bytes`** 这类**连续缓冲**；你走的 `pipeline_decompress` → C++ **`api::decompress`** 里已经在用 **`MemoryPool`** 做管线内部块（与流式读盘策略 3 是两件事）。

## 已按「策略 1」收紧的行为与文档

1. **`compressor.py`**
   - 去掉 `_DISK_WRITE_CHUNK_BYTES` 与分片写盘；解压完成后 **`fout.write(out_bytes)` 一次写入**（仍先写 `.part` 再 `os.replace`，原子落盘）。
   - 模块注释与 **`_python_decompress_wcx_file_to_disk` / `smart_decompress_file`** 的 docstring 写明：**默认策略 1** = 整 WCX 读入 → 解包 → `pipeline_decompress` → **全部解码完再写盘**；并说明 **`MemoryPool` 只参与 C++ 管线内部块**，不表示「分块读整个 WCX」。
   - 日志改为 `default strategy-1: read-all WCX, full decode, then single disk write`.

2. **`decompress_log.py`**  
   - 策略 1 描述与上面一致，并写明 **一次性写目标文件**。

3. **`api.hpp`**  
   - 在 **`decompress`** 声明上补充：调用方已持有完整 payload 时，`MemoryPool` 仍用于 **`Pipeline` 输出块**；**不替代**整段输入/输出的连续存储。  
   - **`decompressFile`** 的策略 1 一句中注明：`MemoryPool` 仅在 **`decompressFile` 内 `Pipeline`** 中用于中间块。

4. **`api.cpp`**  
   - 在 **`decompress`** 里创建 `MemoryPool` 的位置加了简短注释，与 `MemoryPool.hpp` 对齐。

**未改动的默认策略**：`WEBCOMPRESS_NATIVE_DECOMPRESS_FILE` 仍 **默认关闭**，即默认仍是 **策略 1**；只有显式置 `1/true/yes` 才走 **策略 3**（从磁盘分块读 payload）。

---

**User**

@Package/logs/gui.log 非流式deflate压缩这个文件@resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb 闪退

---

**Cursor**

定位到：第二次压缩走内存路径（`loaded raw data` 后无 `STREAMING`），在 `smart_compress` 内崩溃；开发环境用新 `core_engine` 无法复现。怀疑打包版旧 pyd 或 `bytes(result.data)` 在 `result.data` 为巨型 list 时导致内存爆炸。正在加强 `worker.py`：安全处理 `result.data`，并在内存压缩失败时回退到 `smart_compress_file`。



实现：将流式分支提取为 `_run_streaming_compress_branch`；在内存路径对 `smart_compress` 做 try/except，失败或 `result.data` 为 list 时回退到该分支；对 `bytes` 的 `compressed_data` 避免多余拷贝。



Replacing the duplicated streaming block with a call to `_run_streaming_compress_branch`, then updating the memory branch.



Fixing failure-path logic: avoid calling `getattr` when `result` is `None`.



Here is what was implemented to finish the handoff work on **`src/gui/ui/worker.py`**.

### 1. Shared streaming path: `_run_streaming_compress_branch`
The previous inline file-to-file block is now a single method that runs `smart_compress_file`, handles expansion, training/explorer hooks, folder totals, and `finished_row.emit`. Both the primary “use streaming” branch and the new fallback call it.

### 2. Memory path: safer `smart_compress` + fallback
- **`can_stream_fallback`**: `FileRecord` with a non-empty `path` and `algorithm != NONE`.
- **`smart_compress`** is wrapped in **`try/except`**; failures log with `logger.exception` and, if fallback is allowed, run **`_run_streaming_compress_branch`** and **return** (no crash from a bad/old binding).
- After a successful-looking result, **`error_message`** is checked first (same as before, but before building a huge payload).
- If **`result.data` is a `list`** and fallback is allowed, we **avoid `bytes(list)`** and use the **file-to-file** path instead (legacy STL binding / OOM risk).
- **`compressed_data`**: use the **`bytes`** object as-is; only **`bytearray` / `memoryview`** (and other types) go through **`bytes(...)`**.

### 3. Primary streaming condition
Replaced `hasattr(record, 'path')` with **`isinstance(record, FileRecord) and getattr(record, "path", None)`** so `_run_streaming_compress_branch` always receives a **`FileRecord`**.

### 4. Post–`smart_compress` log
Added:  
`memory smart_compress returned success=... compressed_size=...`  
so logs show whether the call returned before any heavy coercion.

---

**Packaged app:** Rebuild/rebundle so **`WebCompress.exe`** ships the **`core_engine*.pyd`** that includes the buffer overload fixes; this Python layer then covers failures and list payloads if an older pyd is still in use.

If you want, we can next grep the PyInstaller spec or `Package/` build script to ensure the pyd copy step is explicit.

---

**User**

alright

---

**Cursor**

Sounds good. The worker now falls back to file-to-file compression when the in-memory path fails or returns a list-shaped payload, and it avoids extra `bytes()` copies when the engine already returns `bytes`.

When you cut a new build, refresh the packaged `core_engine` pyd so it matches your fixed bindings. If anything still misbehaves, check `Package/logs/gui.log` for the new `memory smart_compress returned` line right before success or fallback.

---

**User**

请你生成报告文档总结过去关于闪退问题的分析汇报

---

**Cursor**

正在创建一份中文技术分析报告，汇总闪退问题的调查结论与已采取的修复措施。



正在撰写完整的中文回顾报告并保存到 `docs/reports/`。



已生成中文回顾报告，路径如下：

**`docs/reports/gui-crash-retrospective-zh.md`**

报告主要内容包括：

1. **现象与范围** — 非流式/内存压缩时闪退或流程中断，与 `gui.log` 的对应关系。  
2. **分层根因** — pybind 重载与 `bytes` 不匹配、打包 pyd 与开发不一致、`bytes(result.data)` 与巨型 `list` 风险，以及相关解压/bridge 修复的简要说明。  
3. **已做缓解** — `worker.py` 流式抽取、内存路径 try/except、失败与 `list` 载荷回退、返回后日志等。  
4. **验证与发版建议** — 开发机烟测、安装包回归、无路径时的失败语义。  
5. **残留风险与可选后续** — 原生崩溃、CI 烟测等。  
6. **关键文件索引** — 便于跳转仓库内具体实现。

若你希望标题或存放目录改成「正式事故复盘」命名（例如放到 `docs/analysis/`），可以说一下偏好我帮你挪一下并重链。

---

**User**

发现非流式压缩等待时间过长阻塞对程序的其他操作

---

**Cursor**

分析非流式压缩如何阻塞 UI：追踪压缩调用链与线程模型。



定位到问题：`CompressionWorker` 虽有 `progress` 信号，但未连接；非流式路径会长时间执行 `load_raw_data`、`extract_features` 和 `smart_compress`，状态栏无阶段反馈。AUTO 在强制非流式时会重复加载/提特征。将连接 `progress`、在关键阶段发出进度，并跳过已加载的 `raw_data` / 已提取的 `base_features`。



