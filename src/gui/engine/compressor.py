from __future__ import annotations

import logging
import os
import shutil
import threading
import time
from pathlib import Path

from gui.config.settings import (
    load_config as _load_app_config,
    save_config as _save_app_config,
    get_algo_config as _get_algo_from_file,
    get_streaming_threshold as _get_threshold_from_file,
    get_effective_streaming_chunk_kb as _get_effective_chunk_kb,
)
from gui.engine.bridge import get_core_engine
from gui.models import (
    AlgorithmType,
    ALGORITHM_PARAMS,
    FileRecord,
    get_default_config,
    STREAMING_THRESHOLD_MB,
)

logger = logging.getLogger(__name__)

# Mirrors ``compressor::core::kFileCompress*`` in AlgorithmFactory.hpp
_FILE_COMPRESS_LZDP_WHOLE_FILE = 1

# Whole compressed WCX file read into RAM for Python file→disk decompress (not a WCX field).
_DECOMPRESS_FILE_READ_ALL_MAX_BYTES = 512 * 1024 * 1024
# Default file→disk decompress is **strategy 1** only (see ``smart_decompress_file``): full WCX bytes
# in process memory, then C++ ``api::decompress`` / ``Pipeline`` (which uses ``memory::MemoryPool``
# in ``src/utils/include/MemoryPool.hpp`` for **internal** fixed-size chunks during pull — not for
# holding the whole file as one pool slot), then write plaintext after decode completes.


class CompressionEngine:
    """Global algorithm knobs + native compress/decompress share one ``_config``.

    A re-entrant lock serializes **all** reads/writes of ``_config`` and native entry points that
    consult it (including ADE ``SilentExplorer`` background threads), so ADE cannot interleave
    ``set_config`` with a main-thread decompress or worker compress.
    """

    _instance = None
    _config: dict[AlgorithmType, dict[str, int]] = None
    _streaming_threshold_mb: float = STREAMING_THRESHOLD_MB
    _engine_op_lock = threading.RLock()

    def __init__(self):
        self._engine = get_core_engine()
        with CompressionEngine._engine_op_lock:
            if CompressionEngine._config is None:
                try:
                    file_cfg = _load_app_config()
                    CompressionEngine._config = _get_algo_from_file(file_cfg)
                    CompressionEngine._streaming_threshold_mb = _get_threshold_from_file(file_cfg)
                    logger.info("[engine] config loaded from file, threshold=%.1fMB", CompressionEngine._streaming_threshold_mb)
                except Exception as e:
                    logger.warning("[engine] failed to load config file: %s, using defaults", e)
                    CompressionEngine._config = get_default_config()

    @classmethod
    def _get_config_unlocked(cls) -> dict[AlgorithmType, dict[str, int]]:
        """Return live config dict; caller must hold ``_engine_op_lock``."""
        if cls._config is None:
            cls._config = get_default_config()
        return cls._config

    @classmethod
    def set_config(cls, config: dict[AlgorithmType, dict[str, int]], save: bool = True):
        with cls._engine_op_lock:
            cls._config = config
            if save:
                cls._save_to_file()

    @classmethod
    def get_config(cls) -> dict[AlgorithmType, dict[str, int]]:
        with cls._engine_op_lock:
            return cls._get_config_unlocked()

    @classmethod
    def snapshot_for_algorithm(cls, algorithm: AlgorithmType) -> dict[str, int]:
        """Copy of engine config for ``algorithm`` at call time (for per-record demos)."""
        with cls._engine_op_lock:
            cfg = cls._get_config_unlocked().get(algorithm, {})
            return {str(k): int(v) for k, v in cfg.items()}


    @classmethod
    def set_streaming_threshold(cls, mb: float, save: bool = True):
        with cls._engine_op_lock:
            cls._streaming_threshold_mb = max(0.0, float(mb))
            if save:
                cls._save_to_file()

    @classmethod
    def get_streaming_threshold(cls) -> float:
        with cls._engine_op_lock:
            return cls._streaming_threshold_mb

    @property
    def available(self) -> bool:
        return self._engine is not None

    @classmethod
    def _save_to_file(cls):
        try:
            from gui.config.settings import (
                load_config as _load_full,
                save_config as _save_full,
            )

            full = _load_full()
            algo_dict = {}
            for algo, params in cls._config.items():
                algo_dict[algo.value] = {k: v for k, v in params.items()}
            full["algorithms"] = algo_dict
            if "streaming" not in full:
                full["streaming"] = {}
            full["streaming"]["threshold_mb"] = cls._streaming_threshold_mb
            _save_full(full)
            logger.info("[engine] config saved to file")
        except Exception as e:
            logger.error("[engine] failed to save config: %s", e)

    @classmethod
    def reset_to_defaults(cls):
        with cls._engine_op_lock:
            cls._config = get_default_config()
            cls._streaming_threshold_mb = STREAMING_THRESHOLD_MB
            cls._save_to_file()

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        return self.smart_compress(data, algorithm)

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        return self.smart_decompress(data, algorithm)

    def _make_pipeline_result(
        self,
        success: bool,
        original_size: int,
        compressed_size: int,
        time_ms: float,
        error_message: str,
        data: bytes | None = None,
    ):
        r = self._engine.PipelineCompressResult()
        r.success = success
        r.original_size = original_size
        r.compressed_size = compressed_size
        r.compression_ratio = (
            (compressed_size / original_size) if original_size > 0 else 0.0
        )
        r.time_ms = time_ms
        r.error_message = error_message
        r.data = data if data is not None else b""
        return r

    def _gzip_bytes_compress(self, data: bytes):
        import gzip
        import io

        t0 = time.perf_counter()
        cfg = self.get_config().get(AlgorithmType.GZIP, {})
        level = max(0, min(9, int(cfg.get("compression_level", 6))))
        buf = io.BytesIO()
        try:
            with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=level) as gz:
                gz.write(data)
            raw = buf.getvalue()
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(True, len(data), len(raw), ms, "", raw)
        except Exception as e:
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(False, len(data), 0, ms, str(e), None)

    def _gzip_bytes_decompress(self, data: bytes):
        import gzip
        import io

        t0 = time.perf_counter()
        buf = io.BytesIO(data)
        try:
            with gzip.GzipFile(fileobj=buf, mode="rb") as gz:
                out = gz.read()
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(True, len(data), len(out), ms, "", out)
        except Exception as e:
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(False, len(data), 0, ms, str(e), None)

    def _gzip_smart_compress_file(self, input_path: str, output_path: str):
        import gzip

        t0 = time.perf_counter()
        p = Path(input_path)
        orig = p.stat().st_size if p.is_file() else 0
        cfg = self.get_config().get(AlgorithmType.GZIP, {})
        level = max(0, min(9, int(cfg.get("compression_level", 6))))
        try:
            with open(input_path, "rb") as fin, gzip.open(
                output_path, "wb", compresslevel=level
            ) as fout:
                shutil.copyfileobj(fin, fout, length=1024 * 1024)
            comp = Path(output_path).stat().st_size
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(True, orig, comp, ms, "", None)
        except Exception as e:
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(False, orig, 0, ms, str(e), None)

    def _gzip_smart_decompress_file(self, input_path: str, output_path: str):
        import gzip

        t0 = time.perf_counter()
        p = Path(input_path)
        comp_sz = p.stat().st_size if p.is_file() else 0
        try:
            with gzip.open(input_path, "rb") as fin, open(output_path, "wb") as fout:
                shutil.copyfileobj(fin, fout, length=1024 * 1024)
            dec = Path(output_path).stat().st_size
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(True, comp_sz, dec, ms, "", None)
        except Exception as e:
            ms = (time.perf_counter() - t0) * 1000.0
            return self._make_pipeline_result(False, comp_sz, 0, ms, str(e), None)

    def smart_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            return self.compress(data, algorithm)

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if algorithm == AlgorithmType.GZIP:
            return self._gzip_bytes_compress(data)

        return self.pipeline_compress(data, algorithm)

    def smart_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        from gui.engine.decompress_log import log_decompress, summarize_result

        log_decompress(
            "smart_decompress_begin",
            algo=algorithm.value,
            payload_bytes=len(data),
        )

        if algorithm == AlgorithmType.TRANSFORMER:
            r = self.decompress(data, algorithm)
            log_decompress("smart_decompress_end", **summarize_result(r))
            return r

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if algorithm == AlgorithmType.GZIP:
            r = self._gzip_bytes_decompress(data)
            log_decompress("smart_decompress_end", **summarize_result(r))
            return r

        r = self.pipeline_decompress(data, algorithm)
        log_decompress("smart_decompress_end", **summarize_result(r))
        return r

    def smart_compress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
        viz_path: str | None = None,
        algo_config: dict | None = None,
    ):
        """Streaming compress file-to-file without loading into Python memory.

        When ``viz_path`` is provided, visualization events are written to a ``.viz``
        v2 file in the same pass (uses ``compressFileWithViz`` internally).

        When ``algo_config`` is provided it overrides the global engine config for
        this call only — safe for parallel per-record compression.
        """
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        from gui.utils.workspace import ensure_workspace_layout

        ensure_workspace_layout()

        # Snapshot config under lock; release before native call for parallelism
        with CompressionEngine._engine_op_lock:
            if algorithm == AlgorithmType.GZIP:
                logger.info(
                    "[smart_compress_file] %s -> %s via gzip (stdlib, chunked copy)",
                    input_path,
                    output_path,
                )
                return self._gzip_smart_compress_file(input_path, output_path)
            algo_id = self._get_pipeline_id(algorithm)
            if algo_id is None:
                raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")
            cfg = _load_app_config()
            chunk_bytes = int(_get_effective_chunk_kb(algorithm, cfg)) * 1024
            file_opts = 0
            lzdp_wf = None
            dpflate_p = None
            deflate_p = None
            if algorithm == AlgorithmType.LZDP:
                file_opts |= _FILE_COMPRESS_LZDP_WHOLE_FILE
                lzdp_wf = self._lzdp_whole_file_params_for_file_pipeline(algo_config)
            elif algorithm == AlgorithmType.DPFLATE:
                dpflate_p = self._dpflate_pipeline_params_for_file_pipeline(algo_config)
            elif algorithm == AlgorithmType.DEFLATE:
                deflate_p = self._deflate_pipeline_params_for_file_pipeline(algo_config)
            do_viz = viz_path is not None

        # Native call WITHOUT lock — allows parallel file compression
        if do_viz:
            return self._engine.pipeline_compress_file_with_viz(
                input_path, output_path, viz_path, [algo_id], chunk_bytes
            )

        logger.info(
            "[smart_compress_file] %s -> %s via %s (chunk_bytes=%d, file_opts=%d)",
            input_path, output_path, algorithm.value, chunk_bytes, file_opts,
        )
        return self._engine.pipeline_compress_file(
            input_path, output_path, [algo_id], chunk_bytes, file_opts, lzdp_wf, dpflate_p, deflate_p
        )

    def _python_decompress_wcx_file_to_disk(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType,
        *,
        log_event_prefix: str,
    ):
        """WCX **默认解压策略 1**：整份 WCX 读入内存 → 解头得到 payload → C++ ``pipeline_decompress``
        （内部 ``api::decompress`` + ``Pipeline`` + ``memory::MemoryPool``，见 ``src/utils/include/MemoryPool.hpp``，
        Pool 只服务管线块复用，不替代「整段 payload / 整段明文」的连续缓冲）→ **解码全部完成后**
        再一次性写入目标文件（先 ``.part`` 再 ``os.replace``）。

        设计文档里的 **「二分块」**（``streaming-compression-design.md`` §3.2 Phase 2）指 LZDP temp A/B 的滚动读，
        与「明文写盘是否分块」无关。

        **策略 3**（从磁盘分块读 WCX payload）：仅当 ``WEBCOMPRESS_NATIVE_DECOMPRESS_FILE=1`` 走
        ``pipeline_decompress_file`` / ``decompressFile``。
        """
        from gui.engine.decompress_log import log_decompress, summarize_result
        from gui.engine.file_protocol import unpack_compressed_file

        t0 = time.perf_counter()
        in_sz = os.path.getsize(input_path) if os.path.isfile(input_path) else -1
        log_decompress(
            f"{log_event_prefix}_begin",
            mode="python_strategy1_read_all_decompress_all",
            algo=algorithm.value,
            input=input_path,
            output=output_path,
            input_file_bytes=in_sz,
        )
        if in_sz < 0:
            return self._make_pipeline_result(False, 0, 0, 0.0, "input file not found", None)
        if in_sz > _DECOMPRESS_FILE_READ_ALL_MAX_BYTES:
            return self._make_pipeline_result(
                False,
                0,
                0,
                (time.perf_counter() - t0) * 1000.0,
                f"compressed file too large for memory read path ({in_sz} > {_DECOMPRESS_FILE_READ_ALL_MAX_BYTES})",
                None,
            )
        try:
            blob = Path(input_path).read_bytes()
            hdr, payload = unpack_compressed_file(blob)
        except Exception as e:
            ms = (time.perf_counter() - t0) * 1000.0
            log_decompress(
                f"{log_event_prefix}_fail",
                mode="python_strategy1_read_all_decompress_all",
                err=f"unpack WCX: {e}",
            )
            return self._make_pipeline_result(False, 0, 0, ms, f"unpack WCX: {e}", None)

        if hdr.algorithm != algorithm:
            logger.warning(
                "[decompress_file] WCX header algorithm %s != caller %s (using caller)",
                hdr.algorithm.value,
                algorithm.value,
            )

        try:
            dr = self.pipeline_decompress(payload, algorithm)
        except Exception as e:
            logger.warning(
                "[decompress_file] pipeline_decompress failed, fallback smart_decompress: %s",
                e,
            )
            dr = self.smart_decompress(payload, algorithm)
        if not dr.success:
            ms = (time.perf_counter() - t0) * 1000.0
            em = (getattr(dr, "error_message", None) or "decode failed").strip()
            log_decompress(
                f"{log_event_prefix}_fail",
                mode="python_strategy1_read_all_decompress_all",
                err=em,
                **summarize_result(dr),
            )
            return self._make_pipeline_result(
                False, int(hdr.original_size), 0, ms, em, None
            )

        out_bytes = dr.data if isinstance(dr.data, (bytes, bytearray)) else bytes(dr.data)
        orig_decl = int(hdr.original_size)
        written = len(out_bytes)
        part_path = f"{output_path}.part"
        try:
            outp = Path(output_path)
            outp.parent.mkdir(parents=True, exist_ok=True)
            with open(part_path, "wb") as fout:
                if out_bytes:
                    fout.write(out_bytes)
            os.replace(part_path, output_path)
        except OSError as e:
            try:
                Path(part_path).unlink(missing_ok=True)
            except OSError:
                pass
            ms = (time.perf_counter() - t0) * 1000.0
            log_decompress(
                f"{log_event_prefix}_fail",
                mode="python_strategy1_read_all_decompress_all",
                err=str(e),
            )
            return self._make_pipeline_result(False, orig_decl, 0, ms, str(e), None)

        ms = (time.perf_counter() - t0) * 1000.0
        result = self._make_pipeline_result(True, orig_decl or written, written, ms, "", None)
        out_sz = os.path.getsize(output_path) if os.path.isfile(output_path) else -1
        fields = summarize_result(result)
        fields["output_file_bytes"] = out_sz
        log_decompress(f"{log_event_prefix}_end", **fields)
        return result

    def smart_decompress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
    ):
        """Decompress a WCX file to disk.

        **默认（策略 1）**：整 WCX 读入内存，解码完成后再写盘；见 ``_python_decompress_wcx_file_to_disk``。
        C++ 侧 ``MemoryPool`` 仅用于 ``pipeline_decompress`` 内部管线块（``MemoryPool.hpp``），不表示分块读整文件。

        **策略 3（显式）**：``WEBCOMPRESS_NATIVE_DECOMPRESS_FILE=1`` → ``pipeline_decompress_file`` /
        ``decompressFile``（从磁盘分块读 payload）。
        """
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        if algorithm == AlgorithmType.GZIP:
            logger.info(
                "[smart_decompress_file] %s -> %s via gzip (stdlib, chunked copy)",
                input_path,
                output_path,
            )
            from gui.engine.decompress_log import log_decompress, summarize_result

            in_sz = os.path.getsize(input_path) if os.path.isfile(input_path) else -1
            log_decompress(
                "smart_decompress_file_begin",
                algo="gzip",
                input=input_path,
                output=output_path,
                input_file_bytes=in_sz,
            )
            with CompressionEngine._engine_op_lock:
                r = self._gzip_smart_decompress_file(input_path, output_path)
            out_sz = os.path.getsize(output_path) if os.path.isfile(output_path) else -1
            f = summarize_result(r)
            f["output_file_bytes"] = out_sz
            log_decompress("smart_decompress_file_end", **f)
            return r
        with CompressionEngine._engine_op_lock:
            decomp_id = self._get_decompress_pipeline_id(algorithm)
            if decomp_id is None:
                raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")
            cfg = _load_app_config()
            chunk_bytes = int(_get_effective_chunk_kb(algorithm, cfg)) * 1024

            in_sz = os.path.getsize(input_path) if os.path.isfile(input_path) else -1
            native = os.environ.get("WEBCOMPRESS_NATIVE_DECOMPRESS_FILE", "").strip().lower() in (
                "1",
                "true",
                "yes",
            )
            if native:
                from gui.engine.decompress_log import log_decompress, summarize_result

                logger.info(
                    "[smart_decompress_file] %s -> %s via %s (NATIVE pipeline_decompress_file chunk_bytes=%d)",
                    input_path,
                    output_path,
                    algorithm.value,
                    chunk_bytes,
                )
                log_decompress(
                    "smart_decompress_file_begin",
                    mode="native_cpp",
                    algo=algorithm.value,
                    input=input_path,
                    output=output_path,
                    chunk_bytes=chunk_bytes,
                    input_file_bytes=in_sz,
                )
                result = self._engine.pipeline_decompress_file(
                    input_path, output_path, [decomp_id], chunk_bytes
                )
                out_sz = os.path.getsize(output_path) if os.path.isfile(output_path) else -1
                fields = summarize_result(result)
                fields["output_file_bytes"] = out_sz
                log_decompress("smart_decompress_file_end", **fields)
                return result

            logger.info(
                "[smart_decompress_file] %s -> %s via %s (default strategy-1: read-all WCX, full decode, then single disk write; native off)",
                input_path,
                output_path,
                algorithm.value,
            )
            return self._python_decompress_wcx_file_to_disk(
                input_path, output_path, algorithm, log_event_prefix="smart_decompress_file"
            )

    _ALGO_TO_PIPELINE_ID = None

    def _get_pipeline_id(self, algorithm: AlgorithmType):
        if CompressionEngine._ALGO_TO_PIPELINE_ID is None:
            eng = self._engine
            CompressionEngine._ALGO_TO_PIPELINE_ID = {
                AlgorithmType.DEFLATE: eng.AlgorithmID.DEFLATE,
                AlgorithmType.LZSS: eng.AlgorithmID.LZSS,
                AlgorithmType.LZDP: eng.AlgorithmID.LZDP,
                AlgorithmType.DPFLATE: eng.AlgorithmID.DPFLATE,
                AlgorithmType.BROTLI: eng.AlgorithmID.BROTLI,
                AlgorithmType.ZSTD: eng.AlgorithmID.ZSTD,
            }
        return CompressionEngine._ALGO_TO_PIPELINE_ID.get(algorithm)

    def _get_decompress_pipeline_id(self, algorithm: AlgorithmType):
        mapping = {
            AlgorithmType.DEFLATE: self._engine.AlgorithmID.INFLATE,
            AlgorithmType.LZSS: self._engine.AlgorithmID.LZSS_DECOMPRESS,
            AlgorithmType.LZDP: self._engine.AlgorithmID.LZMINE_DECOMPRESS,
            AlgorithmType.DPFLATE: self._engine.AlgorithmID.INFLATE,
            AlgorithmType.BROTLI: self._engine.AlgorithmID.BROTLI_DECOMPRESS,
            AlgorithmType.ZSTD: self._engine.AlgorithmID.ZSTD_DECOMPRESS,
        }
        return mapping.get(algorithm)

    def _lzdp_whole_file_params_for_file_pipeline(self, algo_cfg: dict | None = None):
        """``LzdpWholeFileParams`` for C++ whole-file LZDP; mirrors ``_create_compressor`` LZDP knobs."""
        eng = self._engine
        p = eng.LzdpWholeFileParams()
        c = algo_cfg if algo_cfg is not None else self.get_config().get(AlgorithmType.LZDP, {})
        p.search_size = int(c.get("search_size", 4096))
        p.lookahead_size = int(c.get("lookahead_size", 256))
        p.min_match = int(c.get("min_match", 0))
        p.dp_top = int(c.get("dp_top", 3))
        p.use_flag_encoding = bool(int(c.get("use_flag_encoding", 0)))
        p.match_engine = int(c.get("match_engine", 0))
        return p

    def _deflate_pipeline_params_for_file_pipeline(self, algo_cfg: dict | None = None):
        """``DeflatePipelineParams`` for C++ streaming ``algorithm::Deflate``; mirrors GUI Deflate row."""
        eng = self._engine
        p = eng.DeflatePipelineParams()
        c = algo_cfg if algo_cfg is not None else self.get_config().get(AlgorithmType.DEFLATE, {})
        p.search_size = int(c.get("search_size", 4096))
        p.lookahead_size = int(c.get("lookahead_size", 256))
        p.min_match = int(c.get("min_match", 0))
        p.max_chain_length = int(c.get("max_chain_length", 256))
        return p

    def _dpflate_pipeline_params_for_file_pipeline(self, algo_cfg: dict | None = None):
        """``DpflatePipelineParams`` for C++ streaming DPFlate; mirrors ``_create_compressor`` knobs."""
        eng = self._engine
        p = eng.DpflatePipelineParams()
        c = algo_cfg if algo_cfg is not None else self.get_config().get(AlgorithmType.DPFLATE, {})
        p.search_size = int(c.get("search_size", 4096))
        p.lookahead_size = int(c.get("lookahead_size", 256))
        p.min_match = int(c.get("min_match", 0))
        p.max_chain_length = int(c.get("max_chain_length", 256))
        p.dp_sub_match_max = int(c.get("dp_sub_match_max", 6))
        p.use_flag_encoding = bool(int(c.get("use_flag_encoding", 0)))
        p.match_engine = int(c.get("match_engine", 1))
        p.use_3hfmtree = bool(int(c.get("use_3hfmtree", 0)))
        base = int(c.get("huffman_chunk_bits", 8))
        p.huffman_offset_chunk_bits = int(c.get("huffman_offset_chunk_bits", base))
        p.huffman_length_chunk_bits = int(c.get("huffman_length_chunk_bits", base))
        return p

    def pipeline_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        with CompressionEngine._engine_op_lock:
            algo_id = self._get_pipeline_id(algorithm)
            if algo_id is None:
                raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

            cfg = _load_app_config()
            chunk_bytes = int(_get_effective_chunk_kb(algorithm, cfg)) * 1024
            lzdp_wf = None
            dpflate_p = None
            deflate_p = None
            if algorithm == AlgorithmType.LZDP:
                lzdp_wf = self._lzdp_whole_file_params_for_file_pipeline()
            elif algorithm == AlgorithmType.DPFLATE:
                dpflate_p = self._dpflate_pipeline_params_for_file_pipeline()
            elif algorithm == AlgorithmType.DEFLATE:
                deflate_p = self._deflate_pipeline_params_for_file_pipeline()

            return self._engine.pipeline_compress(
                data,
                [algo_id],
                lzdp_wf,
                dpflate_p,
                deflate_p,
                chunk_bytes,
            )

    def pipeline_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")

        cfg = _load_app_config()
        chunk_bytes = int(_get_effective_chunk_kb(algorithm, cfg)) * 1024
        lzdp_wf = None
        dpflate_p = None
        deflate_p = None
        if algorithm == AlgorithmType.LZDP:
            lzdp_wf = self._lzdp_whole_file_params_for_file_pipeline()
        elif algorithm == AlgorithmType.DPFLATE:
            dpflate_p = self._dpflate_pipeline_params_for_file_pipeline()
        elif algorithm == AlgorithmType.DEFLATE:
            deflate_p = self._deflate_pipeline_params_for_file_pipeline()

        from gui.engine.decompress_log import log_decompress, summarize_result

        log_decompress(
            "pipeline_decompress_begin",
            algo=algorithm.value,
            payload_bytes=len(data),
            chunk_bytes=chunk_bytes,
        )
        with CompressionEngine._engine_op_lock:
            result = self._engine.pipeline_decompress(
                data,
                [decomp_id],
                lzdp_wf,
                dpflate_p,
                deflate_p,
                chunk_bytes,
            )
        log_decompress("pipeline_decompress_end", **summarize_result(result))
        return result

