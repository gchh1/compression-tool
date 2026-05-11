# Streaming Workspace Compression Spec (Draft v1)

## 1. Goal

Implement streaming compression without breaking existing algorithm correctness, while reducing peak CPU memory usage and keeping GUI decoupled from core compression details.

This spec defines:

- stream processing contract
- workspace file lifecycle
- temporary file export behavior
- memory/data-structure rules

---

## 2. Scope

### In scope

- File-to-file streaming compression/decompression
- Chunk-size configurable from Advanced Settings
- Workspace-based intermediate output (`compressed/`)
- GUI export from workspace artifacts
- Cleanup policy on app shutdown

### Out of scope

- Full algorithm rewrite
- Changing algorithm mathematical model
- Network/distributed storage

---

## 3. Core Design Principles

1. **Algorithm correctness first**: no semantic changes to token parsing, match generation, or bitstream logic.
2. **Streaming by bounded windows**: process input in chunks, keep only required tail/state in memory.
3. **Append-only output path**: flush safe compressed bytes to workspace file incrementally.
4. **Low-copy path**: avoid repeated vector erase/memmove; prefer span/ring buffer/sliding offset.
5. **GUI-core decoupling**: GUI triggers jobs and exports artifacts, but does not own stream internals.

---

## 4. Terminology

- **Chunk**: raw input slice read from source file each iteration.
- **Safe flush bytes**: compressed bytes guaranteed not to depend on future input.
- **Tail**: minimal unresolved input context retained for next iteration (for LZ/Huffman boundaries).
- **Workspace**: local temporary working directory managed by app runtime.

---

## 5. Workspace Layout

Recommended runtime workspace root:

`<app_data>/workspace/`

Subdirectories:

- `compressed/` - completed or in-progress compressed artifacts
- `decompressed/` - optional decompression outputs
- `jobs/` - job metadata / state snapshots
- `tmp/` - transient staging files

For current semantic preference in project, `compressed/` is mandatory.

---

## 6. Streaming Compression Flow

For each input file:

1. Create job ID and target workspace output:
  - `workspace/compressed/<job-id>.wcx.part`
2. Open input stream + output stream.
3. Write required format/file header to `.part` once.
4. Loop:
  - Read one chunk (`chunk_size` from settings).
  - Feed chunk into compressor state machine.
  - Compressor emits:
    - safe flush bytes (write to `.part` immediately),
    - unresolved tail/state (kept in memory).
  - Release no-longer-needed input segments.
5. Finalize:
  - Feed end-of-stream flag (`is_last_chunk=true`).
  - Flush remaining bytes + trailer/footer.
  - fsync/close and atomically rename:
    - `<job-id>.wcx.part` -> `<job-id>.wcx`
6. Record artifact metadata into job state for GUI listing/export.

---

## 7. LZ-family Streaming Requirements

Applicable to LZSS/LZDP/Deflate-like pipelines.

### 7.1 State continuity

- Preserve dictionary/sliding-window state across chunks.
- Preserve pending bit-writer state across chunks.
- Preserve pending token/Huffman block state if block not yet flush-safe.

### 7.2 Tail retention

- Keep only required suffix needed for correctness.
- Do not keep full processed prefix after safe flush boundary is committed.

### 7.3 Flush boundary

- Only flush bytes that are independent from future lookahead.
- At final chunk, flush all pending state and write end markers.

---

## 8. Data Structure Rules (Memory/CPU)

1. Avoid `erase(begin, begin+n)` on large vectors in hot loops.
2. Prefer one of:
  - ring buffer,
  - deque of immutable chunks + offset cursor,
  - contiguous buffer + logical read index (periodic compact).
3. Avoid redundant copy:
  - `span`/view for read-only traversal,
  - move ownership where possible.
4. Output buffering:
  - bounded output chunk buffer,
  - append to file in batches.

---

## 9. GUI Integration Contract

GUI uses stable high-level API only:

- `start_stream_compress(input_path, algorithm, options) -> job_id`
- `get_job_status(job_id) -> {progress, job_state, export_state, output_path?}`
- `export_job_output(job_id, dst_path) -> result`

GUI must not parse algorithm internals.

---

## 10. Export Behavior

When user clicks Export:

1. Validate `job_state == completed`.
2. Copy workspace artifact:
  - `workspace/compressed/<job-id>.wcx`
  - to user-selected destination.
3. Keep workspace copy by default (for history/retry), unless retention policy says prune.

---

## 11. Cleanup Policy

On graceful app exit:

- remove stale `.part` in workspace
- clean `tmp/`
- optionally prune old completed artifacts by retention config

On crash/restart:

- detect incomplete jobs from `.part` + metadata
- mark as failed/recoverable, never expose as completed output

---

## 12. Config Keys (Proposed)

Under `streaming`:

- `chunk_size_kb` (int)
- `workspace_root` (string, optional)
- `keep_completed_artifacts` (bool)
- `retention_days` (int)
- `max_workspace_size_mb` (int)

---

## 13. Validation Checklist

1. Roundtrip correctness:
  - small/medium/large files
  - boundary around chunk size
2. Memory usage:
  - peak RSS lower than non-streaming baseline on large files
3. Corruption handling:
  - interrupted writes do not appear as completed artifacts
4. Export behavior:
  - exported file byte-identical to workspace completed artifact
5. Algorithm continuity:
  - chunked output equals single-pass output for same algorithm/config (where deterministic)

---

## 14. Migration Strategy (No Full Refactor)

1. Keep existing GUI workflow.
2. Introduce new streaming workspace implementation behind feature flag.
3. Bind GUI to new APIs with fallback to old path.
4. Migrate algorithms one by one.
5. Remove old path only after parity tests pass.

---

## 15. Open Questions

1. Workspace root location: app local dir vs user profile appdata?
2. Should completed artifacts be auto-pruned or user-managed?
3. Do we require resumable compression jobs in v1?
4. For each algorithm, what is minimum tail size for correctness?

### 15.1 协议统一决策（已定）

**决策：以 `wcx` 作为唯一容器协议。**

约束：

1. 对外导出文件统一为 `.wcx`，不再新增并行容器格式。
2. C++ `archiver` 层不再自定义独立容器语义，改为实现/复用 `wcx` 协议读写。
3. GUI 与核心共享同一份 `wcx` 协议定义（字段、字节序、版本、校验规则）。
4. 新功能（流式、工作区、导出）全部基于 `wcx`，禁止“双协议并行扩展”。

---

### 15.2 技术债清偿计划（WCX 单协议迁移）

目标：清除当前 `wcx`（GUI）与 `archiver`（C++）双容器并存的设计债。

#### Phase 1：协议基线冻结

1. 冻结 `wcx` v2 头定义（字段顺序、类型、端序）。
2. 在文档中给出唯一“字节级布局表”与示例。
3. 明确 `compressed_size`/`original_size` 的占位与回填策略。

验收：

- 文档中只有一份权威容器定义，且被 GUI/C++ 同时引用。

#### Phase 2：C++ 侧对齐 WCX

1. `archiver` 新增 `WCXWriter/WCXReader`（或同等模块）。
2. 废弃/隔离 `EntryHeader` 作为容器头的职责（可保留内部结构体，但不再作为协议边界）。
3. 流式写入采用“占位头 + 回填”或“尾部索引”中的单一策略（与 WCX 定义一致）。

验收：

- C++ 生成的 `.wcx` 可被 GUI `file_protocol.py` 直接解析与解压。
- GUI 生成的 `.wcx` 可被 C++ 侧直接解析与解压。

#### Phase 3：绑定与调用面收敛

1. Python 绑定暴露统一接口（例如 `pipeline_compress_file_wcx` / `pipeline_decompress_file_wcx`）。
2. GUI 全部改走统一接口，不再拼接第二套容器头。
3. 旧接口标记 deprecated，并在日志中告警调用来源。

验收：

- 主流程日志中不再出现旧容器写入路径。

#### Phase 4：回归与兼容

1. 建立跨版本兼容测试（旧 `.wcx` 读、新 `.wcx` 读写）。
2. 建立损坏包测试（截断头、截断 payload、size 不匹配）。
3. 建立大文件流式测试（>1GB）验证头回填和导出流程。

验收：

- roundtrip、兼容、损坏场景全部通过。

#### Phase 5：删除旧路径

1. 删除/封存双协议分支代码。
2. 清理文档中的历史容器描述，仅保留 `wcx` 单协议说明。
3. 在 `CONTRIBUTING` 增加规则：禁止新增非 `wcx` 容器协议。
4. 按里程碑切换默认行为（建议）：
   - M1（当前）: **仅接受 WCX 容器**；解压/目录解包无裸流、无裸 Pack 静默回退（见 `wcx_decompress_input` 与 Python `file_protocol`）。
   - M2（预发布）: CI 与发布包与 M1 一致；主规格与阶段表不再描述已删除的 `WCX_STRICT_*` 等产品开关（读路径固定 WCX，无「关严格即兼容」分支）。
   - M3（正式）: 强化错误信息与遥测；无用户可见“兼容模式”。
   - M4（收尾）: 审计残留分支与日志前缀；`archiver` 与 `wcx` 边界收敛（§15.2 Phase 2）；按需提供只读迁移工具。

验收：

- 代码库无可执行双协议分支。
- 新增 PR 若引入第二协议会被 CI/规则拦截。

---

## 16. 各算法流式设计（中文详细版）

本节按“你可以直接照着实现”的粒度，描述每个算法在工作区流式架构中的处理流程、状态保留点与伪代码。

### 16.1 总体分类与接入策略


| 算法                        | v1 模式           | 原因          | 关键约束          |
| ------------------------- | --------------- | ----------- | ------------- |
| Deflate / Inflate         | 分帧适配            | 现有实现稳定，改动最小 | 必须处理截断帧，禁止死循环 |
| LZSS / LZSSDecompress     | 分帧适配            | 低风险，快速接入    | 帧长度严格校验       |
| LZDP / LZDPDecompress     | 分帧适配            | 保持当前行为一致    | 参数与位宽逻辑不改     |
| Brotli / BrotliDecompress | 分帧适配            | 维持现有接口      | 终止帧缺失要报错      |
| Zstd / ZstdDecompress     | 分帧适配            | 简单稳妥        | 帧完整性强校验       |
| DeltaEncode / DeltaDecode | 真增量             | 算法天然流式      | 跨块保留 `prev`   |
| DPFlate                   | 过渡模式（接口流式，内部聚合） | 不重写 DP 语义   | 需要内存上限保护      |


说明：

- **分帧适配**：外层做 `长度头 + 帧负载 + 终止帧`，每帧独立压缩/解压。
- **真增量**：同一个算法实例跨 chunk 连续推进状态，不依赖分帧。

---

### 16.2 通用文件流式流程（所有算法共用）

#### 16.2.1 压缩主流程

```pseudo
function stream_compress_to_workspace(input_path, algo_id, options):
    job_id = create_job_id()
    part_path = workspace/compressed/{job_id}.wcx.part
    artifact_path = workspace/compressed/{job_id}.wcx  // 工作区成品，不是用户导出目录

    reader = open_binary_reader(input_path)
    writer = open_binary_writer(part_path)
    state = create_stream_state(algo_id, options)
    stats = {original_size: 0, compressed_size: 0}

    // WCX 头里有 original_size / compressed_size，流式开始时未知
    // 先写占位头，并记录可回填位置（要求 writer 支持 seek）
    header_patch_points = write_container_header_placeholder(writer, algo_id, options)

    while true:
        chunk = reader.read(options.chunk_size)
        is_last = chunk.empty()
        stats.original_size += chunk.size

        produced = state.process(chunk, is_last)  // 可能返回多个输出片段
        writer.append(produced.safe_bytes)
        stats.compressed_size += produced.safe_bytes.size

        if produced.error:
            mark_job_failed(job_id, produced.error_message)
            close(writer)
            return FAIL

        if is_last:
            break

    // 回填头字段：original_size / compressed_size
    patch_container_header(writer, header_patch_points, stats)
    write_container_footer(writer)
    writer.fsync_close()
    atomic_rename(part_path, artifact_path)
    mark_job_completed(job_id, artifact_path)
    return OK
```

WCX 协议说明（与上面伪代码对应）：

- 字段语义上，Python/GUI 的 `CompressedFileHeader` 与 C++ `compressor::api::wcx::HeaderView` 对应（如 `algorithm` ↔ `algo_code`）；**字节布局以 §16.2.1.1 与 `WCXProtocol.cpp` 为准**。
- 其中 `compressed_size` 在流式循环前不可知，因此“while 前写 header”只有在**占位 + 回填**时才规范。
- 如果底层 writer 不支持 seek 回填，则必须改协议（例如尾部索引/尾部元数据），不能直接按当前 WCX 头格式一次写死。

**WCX v2 `algo_code` 字节（与 C++ `wcx::toAlgoCode` / Python `ALGO_CODE_MAP` 对齐）**

| 值 | 含义 |
|----|------|
| 0 | 无压缩 / 占位 / 未知 |
| 1 | Deflate（及 Inflate 对称标注） |
| 2 | LZSS |
| 3 | LZDP |
| 4 | Huffman（当前无 `core::AlgorithmID`，仅 Python 路径写头） |
| 5 | DPFlate |
| 6 | Gzip 容器流（当前无 `core::AlgorithmID`，仅 Python 路径写头） |
| 7 | Brotli |
| 8 | Zstd |

##### 16.2.1.1 WCX v2 容器头字节布局（权威）

与 `src/api/include/WCXProtocol.hpp` 中 `FIXED_HEADER_SIZE == 18` 及 `WCXProtocol.cpp` 读写逻辑一致。多字节整数均为 **little-endian**。偏移自文件开头起算。

| 偏移 | 长度 | 类型 | 字段 | 说明 |
| --- | --- | --- | --- | --- |
| 0 | 4 | `u8[4]` | `magic` | 固定为 ASCII `WCMP`（`0x57 0x43 0x4D 0x50`） |
| 4 | 1 | `u8` | `version` | 当前实现仅接受 **2** |
| 5 | 1 | `u8` | `algo_code` | 见上表 |
| 6 | 4 | `u32` LE | `original_size` | 逻辑未压缩大小（调用方负责 `UINT32_MAX` 封顶） |
| 10 | 4 | `u32` LE | `compressed_size` | **payload** 字节数（不含本头）。流式写入时先占位写 0，结束后 `seek` 到偏移 **10** 回填，见 `wcx::patchCompressedSize` |
| 14 | 1 | `u8` | `flags` | **bit0**：目录归档外层容器为 **1**（`FLAG_FOLDER`）；单文件 WCX 通常为 **0** |
| 15 | 2 | `u16` LE | `filename_len` | 紧随固定头之后的原文件名 UTF-8 长度，可为 0 |
| 17 | 1 | `u8` | `padding` | 当前写 **0**；解析读入，无附加语义 |
| 18 | `filename_len` | `u8[]` | `original_filename` | 可选 |

**头总长度** `header_len = 18 + filename_len`。**payload**（压缩比特流，或目录场景下内层 Pack 等）从偏移 `header_len` 起至文件末尾。回填正确且文件完整时，应满足 `file_size == header_len + compressed_size`。

#### 16.2.2 解压主流程

```pseudo
function stream_decompress_to_target(comp_path, target_output_path, algo_id, options):
    // 解压交互：先让 GUI 询问目标目录，再把最终输出直接写到目标路径
    reader = open_binary_reader(comp_path)
    writer = open_binary_writer(target_output_path.part)
    state = create_stream_state_for_decompress(algo_id, options)

    verify_container_header(reader)

    while not reader.eof():
        chunk = reader.read(options.chunk_size)
        is_last = reader.eof_after_this_read()

        produced = state.process(chunk, is_last)
        writer.append(produced.safe_bytes)

        if produced.error:
            close(writer)
            delete(target_output_path.part)
            return FAIL

    writer.fsync_close()
    atomic_rename(target_output_path.part, target_output_path)
    return OK
```

---

### 16.3 Deflate / Inflate（分帧适配）

#### 16.3.1 设计目标

- 不改 Deflate/Inflate 核心编码逻辑，只在外层增加帧协议。
- 每帧独立处理，降低状态耦合，快速验证正确性。
- 防止“末尾不完整帧”导致等待输入死循环。

#### 16.3.2 帧格式

```text
[u32_be frame_len][frame_payload][u32_be frame_len][frame_payload]...[u32_be 0]
```

#### 16.3.3 压缩伪代码

```pseudo
function deflate_framed_compress_process(read_bytes, is_last):
    input_buffer.append(read_bytes)

    while input_buffer.size >= frame_size:
        frame = input_buffer.take_prefix(frame_size)
        payload = deflate_one_shot(frame)    // 调用现有 Deflate 实现
        output_buffer.append(u32be(payload.size))
        output_buffer.append(payload)

    if is_last:
        if input_buffer.not_empty():
            payload = deflate_one_shot(input_buffer.all())
            output_buffer.append(u32be(payload.size))
            output_buffer.append(payload)
            input_buffer.clear()
        output_buffer.append(u32be(0))       // 终止帧
        finished = true

    return drain_output_buffer()
```

#### 16.3.4 解压伪代码（关键：截断处理）

```pseudo
function deflate_framed_decompress_process(read_bytes, is_last):
    input_buffer.append(read_bytes)

    loop:
        if input_buffer.size < 4:
            if is_last and input_buffer.size > 0:
                return error("incomplete frame header")
            break

        frame_len = parse_u32be(input_buffer[0:4])
        if frame_len == 0:
            consume 4 bytes
            finished = true
            break

        if input_buffer.size < 4 + frame_len:
            if is_last:
                return error("incomplete frame payload")
            break

        payload = input_buffer[4 : 4+frame_len]
        plain = inflate_one_shot(payload)
        output_buffer.append(plain)
        consume 4 + frame_len bytes

    if is_last and not finished:
        return error("missing terminator frame")

    return drain_output_buffer()
```

---

### 16.4 LZSS / LZDP / Brotli / Zstd（统一分帧策略）

这四类在 v1 使用同一套外层流程，区别仅在 `compress_one_shot()` 与 `decompress_one_shot()` 的内部实现。

#### 16.4.1 统一流程

```pseudo
compress_one_frame(frame_plain):
    payload = algorithm.compress(frame_plain)
    return [u32be(len(payload)) + payload]

decompress_one_frame(frame_payload):
    plain = algorithm.decompress(frame_payload)
    return plain
```

#### 16.4.2 约束

1. 帧长度必须可信校验，不允许越界读取。
2. 任何单帧解码失败，整任务失败，输出保持 `.part`。
3. 终止帧（0 长度）缺失时，不可“静默成功”。

#### 16.4.3 适配器伪代码（通用）

```pseudo
function framed_adapter_process(read_bytes, is_last, codec):
    in_buf.append(read_bytes)

    while can_decode_complete_frame(in_buf):
        frame = pop_one_frame(in_buf)
        if frame.is_terminator:
            finished = true
            break
        plain_or_comp = codec(frame.payload)
        out_buf.append(plain_or_comp)

    if is_last and not finished:
        if in_buf.has_remaining_bytes():
            return error("truncated framed stream")
        return error("terminator not found")

    return drain(out_buf)
```

---

### 16.5 DeltaEncode / DeltaDecode（真增量）

#### 16.5.1 设计目标

- 维持单实例跨 chunk 连续处理，不分帧。
- 以最小内存实现稳定吞吐。

#### 16.5.2 关键状态

- `prev`：上一字节（预测器）
- 固定长度工作缓冲区（例如 4KB）

#### 16.5.3 伪代码

```pseudo
state.prev = 0

function delta_encode_process(read_bytes, is_last):
    for b in read_bytes:
        pixel = quantize_if_needed(b)
        out = pixel - state.prev
        state.prev = pixel
        emit(out)

    if is_last:
        finished = true
    return emitted_bytes

function delta_decode_process(read_bytes, is_last):
    for b in read_bytes:
        plain = b + state.prev
        state.prev = plain
        emit(plain)

    if is_last:
        finished = true
    return emitted_bytes
```

---

### 16.6 DPFlate（过渡模式）

#### 16.6.1 设计目标

- 不重写当前 DP 求解逻辑（避免破坏最优解析路径语义）。
- 先通过“接口流式 + 内部聚合”接入工作区流程。

#### 16.6.2 行为定义

- 非最后块：只收集输入，返回 `need_input`。
- 最后块：一次性构建树并输出全部压缩结果。

#### 16.6.3 伪代码

```pseudo
function dpflate_process(read_bytes, is_last):
    input_accumulator.append(read_bytes)

    if not is_last:
        if input_accumulator.size > max_buffer_limit:
            return error_or_fallback("dpflate buffer limit exceeded")
        return need_input()

    tokens = build_dp_tokens(input_accumulator)
    trees = build_huffman_trees(tokens)
    bitstream = encode_tokens(tokens, trees)
    emit(bitstream)
    finished = true
    return emitted_bytes
```

#### 16.6.4 内存策略

- 增加配置项：`dpflate_max_buffer_mb`
- 超限策略二选一：
  1. 直接失败并提示用户切换算法；
  2. 自动降级到分帧算法（需在 UI 明示）。

---

### 16.7 工作区导出与清理（你描述的产品流程）

#### 16.7.1 导出流程

```pseudo
on_gui_export_click(job_id, dst_path):
    job = load_job(job_id)
    assert job.job_state == COMPLETED
    src = workspace/compressed/{job_id}.wcx
    copy_file(src, dst_path)
    report_success_to_gui()
```

#### 16.7.2 程序退出清理

```pseudo
on_app_shutdown():
    delete_all("workspace/**/*.part")
    clean_dir("workspace/tmp")
    prune_old_completed_if_needed(retention_days, max_workspace_size_mb)
```

#### 16.7.3 崩溃恢复

```pseudo
on_app_startup():
    for part in find("workspace/compressed/*.part"):
        mark_related_job_failed(part, reason="interrupted previous session")
```

---

### 16.8 统一失败语义

遇到下列任何一种情况，任务必须失败，且不得升级 `.part` 为成品：

1. 帧头不足 4 字节且输入已结束。
2. 帧声明长度大于剩余可读字节且输入已结束。
3. 缺失终止帧。
4. 算法内部解码抛错/返回错误码。

失败处理伪代码：

```pseudo
function fail_job(job_id, err):
    set_job_state(job_id, FAILED, err)
    keep_part_file_for_debug(job_id)
    notify_gui(job_id, err)
```

---

### 16.9 建议实施顺序

1. Deflate / Inflate（先打通全链路与失败语义）
2. DeltaEncode / DeltaDecode（验证真增量路径）
3. LZSS / LZDP（复用分帧模板）
4. Brotli / Zstd（同模板接入）
5. DPFlate（最后接入并加内存保护）

---

## 17. GUI 流式进度更新与压缩写入计划

本节定义“GUI 如何基于流式处理过程实时更新进度”，并给出可落地的分阶段计划。

### 17.1 目标

1. 让用户看到真实进度，而不是假进度条。
2. GUI 不直接参与算法细节，只消费标准进度事件。
3. 进度计算与工作区写入一致，避免“显示完成但文件未落盘”。

### 17.2 进度模型（统一口径）

#### 17.2.1 核心指标

- `bytes_total`：输入文件总字节数
- `bytes_read`：已从输入文件读取字节数
- `bytes_processed`：已被算法消费字节数（可选，若易获得）
- `bytes_written_part`：已写入 `.part` 的字节数
- `ratio_current`：当前压缩率（`bytes_written_part / max(bytes_read, 1)`）
- `elapsed_ms`：耗时
- `throughput_mbps`：吞吐速率
- `job_state`：`created/queued/running/finalizing/completed/failed/cancel_requested/cancelled`
- `export_state`：`not_exported/exporting/exported/export_failed`

#### 17.2.2 进度百分比

GUI 主进度使用：

`progress = bytes_read / bytes_total`

补充说明：

- `bytes_read` 反映“扫描进度”，最平滑。
- `bytes_written_part` 用于显示“输出文件正在增长”，但不作为主百分比。
- `finalizing` 阶段可显示副文案：“正在写入尾部与封口”。

### 17.3 事件接口（GUI 与核心解耦）

定义统一事件结构（Python/C++ 绑定后都遵守）：

```pseudo
struct StreamProgressEvent {
    string job_id
    string job_state              // created/queued/running/finalizing/completed/failed/cancel_requested/cancelled
    string export_state           // not_exported/exporting/exported/export_failed
    uint64 bytes_total
    uint64 bytes_read
    uint64 bytes_written_part
    double ratio_current
    double elapsed_ms
    double throughput_mbps
    string message                // 可读提示
    string error_code             // 失败时填写
    string error_message          // 失败时填写
}
```

核心接口建议：

```pseudo
start_stream_compress(input_path, algorithm, options) -> job_id
subscribe_progress(job_id, callback(StreamProgressEvent))
cancel_job(job_id)
query_job(job_id) -> StreamProgressEvent
export_job_output(job_id, dst_path)
```

### 17.4 压缩线程与 GUI 更新流程

#### 17.4.1 后台线程主循环（核心）

```pseudo
function worker_run(job):
    emit(job_state="queued", export_state="not_exported")
    init_reader_writer_and_codec()
    emit(job_state="running", export_state="not_exported")

    while true:
        if job.cancel_requested:
            emit(job_state="cancel_requested", message="正在取消任务")
            writer.abort_keep_part()
            emit(job_state="cancelled", export_state="not_exported", message="任务已取消")
            return

        chunk = reader.read(chunk_size)
        is_last = chunk.empty()

        job.bytes_read += chunk.size
        produced = codec.process(chunk, is_last)

        writer.append(produced.safe_bytes)
        job.bytes_written_part += produced.safe_bytes.size

        emit_progress(job)      // 节流发射，见 17.5

        if produced.error:
            writer.abort_keep_part()
            emit(job_state="failed", export_state="not_exported",
                 error_code=produced.error.code,
                 error_message=produced.error.message)
            return

        if is_last:
            break

    emit(job_state="finalizing", export_state="not_exported", message="正在封口并提交工作区文件")
    writer.commit_part_to_final()
    emit(job_state="completed", export_state="not_exported", message="压缩完成，可导出")
```

#### 17.4.2 GUI 侧处理

```pseudo
on_progress_event(evt):
    progress_bar.value = int(100 * evt.bytes_read / max(evt.bytes_total, 1))
    status_label.text = format_state_text(evt)
    speed_label.text = format_speed(evt.throughput_mbps)
    ratio_label.text = format_ratio(evt.ratio_current)

    if evt.job_state == "completed":
        enable_export_button(evt.job_id)
    elif evt.job_state in ["failed", "cancelled"]:
        show_error_or_warning(evt)
```

### 17.5 更新频率与节流策略

避免 GUI 高频刷新卡顿，采用“双阈值节流”：

- 时间阈值：至少间隔 `100ms` 才发一次
- 字节阈值：或每新增 `>= 1MB` 再发一次
- 收尾事件（`finalizing/completed/failed/cancel_requested/cancelled`）强制立即发

伪代码：

```pseudo
if now - last_emit_ts >= 100ms or (bytes_read - last_emit_bytes) >= 1MB:
    emit_progress()
```

### 17.6 状态机定义（重写版）

本设计采用双状态机，避免“压缩完成”和“已导出”混为一谈。

#### 17.6.1 JobState（压缩任务状态机）

状态集合：

- `created`：任务对象刚创建，尚未入队
- `queued`：已入队，等待 worker
- `running`：正在读取输入/调用算法/写 `.part`
- `cancel_requested`：收到取消请求，等待 worker 安全停机
- `finalizing`：已读完输入，正在写尾部并 `part -> artifact`
- `completed`：工作区成品就绪（可导出）
- `failed`：任务失败（保留 `.part`）
- `cancelled`：任务取消完成（`.part` 根据策略保留或清理）

允许迁移（唯一合法路径）：

```text
created -> queued
queued -> running
running -> finalizing
finalizing -> completed

running -> cancel_requested
cancel_requested -> cancelled

queued -> failed
running -> failed
finalizing -> failed
cancel_requested -> failed
```

禁止迁移（示例）：

- `completed -> running`（禁止重入）
- `failed -> completed`（失败后不可直接变完成）
- `cancelled -> completed`（取消任务不可导出为完成）

终态（terminal states）：

- `completed` / `failed` / `cancelled`

#### 17.6.2 ExportState（导出状态机）

状态集合：

- `not_exported`：默认状态，未导出
- `exporting`：正在复制工作区成品到用户目标目录
- `exported`：导出成功
- `export_failed`：导出失败（权限、路径冲突、磁盘不足等）

允许迁移：

```text
not_exported -> exporting
exporting -> exported
exporting -> export_failed
export_failed -> exporting   // 用户修复问题后重试
```

约束：

- 仅当 `job_state == completed` 才允许 `not_exported -> exporting`
- `job_state in [failed, cancelled]` 时，导出按钮必须禁用

#### 17.6.3 事件触发规则

1. 每次状态迁移必须发一个事件（不可吞事件）。
2. 状态迁移事件不走节流，必须实时发送。
3. 进度事件可节流（见 17.5），但最后一个 `completed/failed/cancelled` 事件必须强制发送。
4. `query_job(job_id)` 必须返回当前双状态的最新快照。

### 17.7 取消与失败处理计划

#### 17.7.1 用户取消

1. GUI 发 `cancel_job(job_id)`
2. Worker 在下一轮循环检查取消标记
3. 停止读取与编码，关闭句柄
4. 发送 `cancelled` 事件

#### 17.7.2 失败

1. 核心返回错误码和错误消息
2. Worker 发送 `failed` 事件（包含 `error_code` + `error_message`）
3. GUI 展示可读错误，并提供“查看日志/重试”按钮

### 17.8 导出按钮与工作区联动

GUI 导出按钮启用条件：

- `job_state == completed`
- `export_state in [not_exported, export_failed]`
- `workspace/compressed/{job_id}.wcx` 存在

导出动作：

```pseudo
on_export_click(job_id, dst):
    disable_export_button_temporarily()
    emit_export_state(job_id, "exporting")
    result = export_job_output(job_id, dst)
    if result.ok:
        emit_export_state(job_id, "exported")
        toast("导出成功")
    else:
        emit_export_state(job_id, "export_failed")
        toast_error(result.message)
```

### 17.9 分阶段实施计划（执行清单）

#### Phase A：事件与状态基础设施

1. 新增 `StreamProgressEvent` 数据结构
2. 打通 `start/subscribe/query/cancel` 基础接口
3. Worker 先发模拟事件验证 GUI 渲染

验收：

- GUI 能看到状态从 `queued -> running -> completed`

#### Phase B：接入真实压缩循环

1. 用真实 `bytes_read/bytes_written_part` 填充事件
2. 接入节流策略
3. 打通 `finalizing` 状态

验收：

- 进度条稳定推进，完成前会短暂进入 `finalizing`

#### Phase C：失败与取消

1. 接入 `cancel_job`
2. 接入错误码与错误消息
3. GUI 增加失败提示与重试入口

验收：

- 人工制造截断输入时显示 `failed`，不出现假完成

#### Phase D：导出联动与回归

1. `completed` 后启用导出按钮
2. 导出路径冲突、权限异常处理
3. 回归测试不同算法与不同文件规模

验收：

- 导出文件可用，且与工作区成品一致

### 17.10 测试用例（最少集）

1. 小文件（<1MB）：秒完成，状态正确。
2. 大文件（>1GB）：进度平滑，无 UI 卡死。
3. 截断流：进入 `failed`，不产生 completed 成品。
4. 用户取消：进入 `cancelled`，资源释放。
5. 导出失败（无权限）：GUI 给出清晰错误。
6. 连续多任务：每个 `job_id` 进度独立不串台。

---

## 18. 整改记录要求（必须写入 .md）

本项目在「WCX 单协议化 + 流式规范化」整改期间，所有关键改动必须留下可追溯记录。**按日期的整改条目**写入 `docs/design/wcx-streaming-remediation-log.md`；本文件保留阶段表（§18.4）、记录模板（§18.3）与架构/流式产品约束，避免把工作日志与主规格揉在同一份超长文档中。

### 18.1 记录原则

1. **每次合并前必须有文档记录**：至少包含“改了什么、为什么、影响范围、验证结果”。
2. **命名整改必须可追踪**：旧名、新名、影响文件、兼容策略必须成对出现。
3. **协议变更必须字节级说明**：字段、偏移、端序、版本、兼容行为不可省略。
4. **失败与回滚要记录**：包括失败原因、回滚动作、后续计划。

### 18.2 建议文档位置

- **主规范（架构 / 验收 / 阶段表）**：`docs/design/streaming-workspace-spec.md`
- **整改工作日志（按日期条目）**：`docs/design/wcx-streaming-remediation-log.md`
- **协议字节级变更史（可选拆分）**：`docs/design/wcx-protocol-change-log.md`

### 18.3 每次整改的必填模板

```md
### [YYYY-MM-DD HH:mm] <整改标题>

- **阶段**: Phase X
- **负责人**: <name>
- **目标**: <本次要解决的问题>
- **改动文件**:
  - `path/a`
  - `path/b`
- **接口/命名变更**:
  - `<old_name>` -> `<new_name>`
- **协议影响**:
  - 是否影响 WCX 字段/偏移: 是/否
  - 兼容策略: <说明>
- **验证**:
  - 构建: 通过/失败
  - 测试: <用例与结果>
- **风险与回滚**:
  - 风险: <说明>
  - 回滚方式: <说明>
- **结论**: <完成/部分完成/阻塞>
```

### 18.4 阶段进展追踪表（持续维护）


| 阶段      | 目标             | 状态  | 最近更新时间     | 备注                     |
| ------- | -------------- | --- | ---------- | ---------------------- |
| Phase 1 | WCX 协议基线冻结     | 进行中 | 2026-05-11 | `algo_code` 与全头字节布局见 §16.2.1 / §16.2.1.1（与 `WCXProtocol` 对齐） |
| Phase 2 | C++ 侧对齐 WCX 读写 | 进行中 | 2026-05-11 | 文件/目录 API 已写读 WCX 外层；`archiver` 内聚 `WCXWriter/WCXReader` 仍待（§15.2） |
| Phase 3 | 绑定与调用面收敛       | 进行中 | 2026-05-11 | 详见 `wcx-streaming-remediation-log.md`（C++/pybind/GUI 命名与路径） |
| Phase 4 | 回归与兼容测试        | 进行中 | 2026-05-11 | CI：`test_wcx_*`、`test_PackWriter*`；损坏包 / >1GB 流式矩阵仍待补（§15.2 Phase 4） |
| Phase 5 | 删除旧路径与规则固化     | 进行中 | 2026-05-11 | 读路径仅 WCX（`wcx_decompress_input`）；已移除 `WCX_STRICT_*` 与裸流/裸 Pack 回退；CONTRIBUTING 规则与 archiver 边界收敛仍待 |


### 18.5 整改事项存放位置

按日期的 WCX / 流式相关整改条目写入独立文档，不在本主规格中逐条展开：

- **`docs/design/wcx-streaming-remediation-log.md`**：历史条目（自 2026-05-11 从本文件迁出）及后续新增条目；撰写时使用 §18.3 模板。

本文件仍负责：**阶段总表（§18.4）**、**记录模板（§18.3）**与第 1–17 节及第 15 节等架构与产品约束。

