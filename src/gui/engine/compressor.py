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

_HUFFMAN_CFG_KEYS = frozenset(
    {"huffman_chunk_bits", "huffman_offset_chunk_bits", "huffman_length_chunk_bits"}
)

# Whole compressed WCX file read into RAM for Python file→disk decompress (not a WCX field).
_DECOMPRESS_FILE_READ_ALL_MAX_BYTES = 512 * 1024 * 1024
# Default file→disk decompress is **strategy 1** only (see ``smart_decompress_file``): full WCX bytes
# in process memory, then C++ ``api::decompress`` / ``Pipeline`` (which uses ``memory::MemoryPool``
# in ``src/utils/include/MemoryPool.hpp`` for **internal** fixed-size chunks during pull — not for
# holding the whole file as one pool slot), then write plaintext after decode completes.


def _apply_huffman_slot_config(comp, algorithm: AlgorithmType, cfg: dict) -> None:
    """Apply 3HfM offset/length slot widths; legacy ``huffman_chunk_bits`` sets both when absent."""
    if algorithm not in (AlgorithmType.DPFLATE, AlgorithmType.DEFLATE):
        return
    base = int(cfg["huffman_chunk_bits"]) if "huffman_chunk_bits" in cfg else 8
    hob = int(cfg["huffman_offset_chunk_bits"]) if "huffman_offset_chunk_bits" in cfg else base
    hlb = int(cfg["huffman_length_chunk_bits"]) if "huffman_length_chunk_bits" in cfg else base
    comp.set_huffman_offset_chunk_bits(hob)
    comp.set_huffman_length_chunk_bits(hlb)


class CompressionEngine:
    """Global algorithm knobs + native compress/decompress share one ``_config``.

    A re-entrant lock protects ``_config`` and short native setup (compressor / pipeline params).
    Long-running C++ ``compress`` / ``pipeline_*`` calls run **outside** the lock so the UI can
    open「算法配置」during an active job. Each operation snapshots knobs under the lock first.
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

    @staticmethod
    def _deep_copy_config(
        cfg: dict[AlgorithmType, dict[str, int]],
    ) -> dict[AlgorithmType, dict[str, int]]:
        return {algo: dict(vals) for algo, vals in cfg.items()}

    @classmethod
    def reload_from_file(cls) -> None:
        """Refresh in-memory knobs from ``webcompress_settings.json`` (dialog open / app start)."""
        with cls._engine_op_lock:
            try:
                file_cfg = _load_app_config()
                cls._config = _get_algo_from_file(file_cfg)
                cls._streaming_threshold_mb = _get_threshold_from_file(file_cfg)
                logger.info(
                    "[engine] config reloaded from file, threshold=%.4fMB",
                    cls._streaming_threshold_mb,
                )
            except Exception as e:
                logger.warning("[engine] reload_from_file failed: %s", e)
                if cls._config is None:
                    cls._config = get_default_config()

    @classmethod
    def set_config(cls, config: dict[AlgorithmType, dict[str, int]], save: bool = True):
        with cls._engine_op_lock:
            base = cls._deep_copy_config(cls._get_config_unlocked())
            for algo, params in config.items():
                base[algo] = {k: int(v) for k, v in params.items()}
            cls._config = base
            if save:
                cls._save_to_file()

    @classmethod
    def get_config(cls) -> dict[AlgorithmType, dict[str, int]]:
        with cls._engine_op_lock:
            return cls._deep_copy_config(cls._get_config_unlocked())

    @classmethod
    def snapshot_for_algorithm(cls, algorithm: AlgorithmType) -> dict[str, int]:
        """Copy of engine config for ``algorithm`` at call time (for per-record demos)."""
        with cls._engine_op_lock:
            cfg = cls._get_config_unlocked().get(algorithm, {})
            return {str(k): int(v) for k, v in cfg.items()}

    def create_compressor_for_visualization(
        self,
        algorithm: AlgorithmType,
        params: dict[str, int] | None = None,
    ):
        """Build compressor with global config, then overlay ``params`` if provided."""
        if algorithm in (AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265):
            raise ValueError(f"{algorithm.value} does not support C++ compressor visualization")
        with CompressionEngine._engine_op_lock:
            compressor = self._create_compressor(algorithm)
            if params:
                for key, val in params.items():
                    setter = getattr(compressor, f"set_{key}", None)
                    if setter:
                        setter(int(val))
            return compressor

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

    def _create_compressor(
        self,
        algorithm: AlgorithmType,
        cfg: dict[AlgorithmType, dict[str, int]] | None = None,
    ):
        """Build native compressor; ``cfg`` is a snapshot (defaults to live config under lock)."""
        if cfg is None:
            with CompressionEngine._engine_op_lock:
                cfg = CompressionEngine._get_config_unlocked()
        if algorithm == AlgorithmType.LZSS:
            comp = self._engine.LZSSCompressor()
        elif algorithm == AlgorithmType.LZDP:
            comp = self._engine.LZDPCompressor()
        elif algorithm == AlgorithmType.DEFLATE:
            comp = self._engine.DeflateCompressor()
        elif algorithm == AlgorithmType.DPFLATE:
            comp = self._engine.DPFlateCompressor()
        elif algorithm == AlgorithmType.GZIP:
            comp = self._engine.GzipCompressor()
        elif algorithm == AlgorithmType.BROTLI:
            comp = self._engine.BrotliCompressor()
        elif algorithm == AlgorithmType.ZSTD:
            comp = self._engine.ZstdCompressor()
        elif algorithm == AlgorithmType.JPEG:
            comp = self._engine.ImageJpegCompressor()
        elif algorithm == AlgorithmType.PNG:
            comp = self._engine.ImagePngCompressor()
        elif algorithm == AlgorithmType.FLAC:
            comp = self._engine.AudioFlacCompressor()
        elif algorithm == AlgorithmType.AAC_LC:
            comp = self._engine.AudioAacCompressor()
        elif algorithm == AlgorithmType.H264:
            comp = self._engine.VideoH264Compressor()
        elif algorithm == AlgorithmType.OPENH264:
            comp = self._engine.VideoOpenH264Compressor()
        else:
            raise ValueError(f"Unsupported algorithm: {algorithm.value}")

        algo_cfg = cfg.get(algorithm, {})
        if algorithm == AlgorithmType.DEFLATE:
            allowed = frozenset(
                {
                    "search_size",
                    "lookahead_size",
                    "min_match",
                    "max_chain_length",
                    "use_flag_encoding",
                    "use_3hfmtree",
                    "huffman_offset_chunk_bits",
                    "huffman_length_chunk_bits",
                }
            )
            algo_cfg = {k: v for k, v in algo_cfg.items() if k in allowed}
        for key, val in algo_cfg.items():
            if key in _HUFFMAN_CFG_KEYS:
                continue
            setter = getattr(comp, f"set_{key}", None)
            if setter:
                setter(val)
        _apply_huffman_slot_config(comp, algorithm, algo_cfg)
        return comp

    def compress_with_overrides(
        self,
        data: bytes,
        algorithm: AlgorithmType,
        overrides: dict[str, int],
    ) -> float:
        """Compress in memory with param overrides; return ratio (1.0 on failure)."""
        if not data or not self.available:
            return 1.0

        if algorithm in (AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265):
            from gui.engine.ffmpeg_codec import VideoFFmpegCompressor

            codec = "h264" if algorithm == AlgorithmType.FFMPEG_H264 else "hevc"
            quality = int(overrides.get("quality", 23))
            preset = str(overrides.get("preset", "medium"))
            fc = VideoFFmpegCompressor(codec=codec, quality=quality, preset=preset)
            result = fc.compress(data)
            if not result["success"]:
                return 1.0
            return float(result["compression_ratio"] or 1.0)

        from gui.models import merge_decision_overrides_into_algo_config

        with CompressionEngine._engine_op_lock:
            cfg = CompressionEngine._deep_copy_config(CompressionEngine._get_config_unlocked())
        base = dict(cfg.get(algorithm, {}))
        cfg[algorithm] = merge_decision_overrides_into_algo_config(algorithm, base, overrides)
        try:
            compressor = self._create_compressor(algorithm, cfg)
            result = compressor.compress(data)
            if not getattr(result, "success", False):
                return 1.0
            return float(getattr(result, "compression_ratio", 1.0) or 1.0)
        except Exception as e:
            logger.debug("[engine] compress_with_overrides failed: %s", e)
            return 1.0

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm in (AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265):
            from gui.engine.ffmpeg_codec import VideoFFmpegCompressor

            codec = "h264" if algorithm == AlgorithmType.FFMPEG_H264 else "hevc"
            cfg = self.get_config().get(algorithm, {})
            quality = int(cfg.get("quality", 23))
            preset = str(cfg.get("preset", "medium"))
            fc = VideoFFmpegCompressor(codec=codec, quality=quality, preset=preset)
            result = fc.compress(data)
            cr = self._engine.CompressorResult()
            cr.original_size = result["original_size"]
            cr.compressed_size = result["compressed_size"]
            cr.compression_ratio = result["compression_ratio"]
            cr.time_ms = result["time_ms"]
            cr.data = list(result["data"])
            cr.success = result["success"]
            cr.error_message = result["error_message"]
            return cr

        if algorithm == AlgorithmType.TRANSFORMER:
            from gui.algorithms.transformer_compressor import TransformerCompressor, HAS_TORCH

            if not HAS_TORCH:
                raise RuntimeError("PyTorch not available for Transformer compression")
            tc = TransformerCompressor()
            result = tc.compress(data)
            cr = self._engine.CompressorResult()
            cr.original_size = result.original_size
            cr.compressed_size = result.compressed_size
            cr.compression_ratio = result.compression_ratio
            cr.time_ms = result.time_ms
            cr.data = list(result.compressed_data)
            cr.success = result.success
            cr.error_message = result.error_message or ("OK" if result.success else "Compression failed")
            return cr

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        with CompressionEngine._engine_op_lock:
            cfg_snap = CompressionEngine._deep_copy_config(
                CompressionEngine._get_config_unlocked()
            )
        compressor = self._create_compressor(algorithm, cfg_snap)
        # === DEBUG_BLOCK_BEGIN (可删除) ===
        if algorithm == AlgorithmType.LZSS:
            try:
                with open("lzss_gui_debug.log", "a") as f:
                    f.write(f"[Python::CompressionEngine.compress] LZSS data_size={len(data)}\n")
            except Exception:
                pass
        # === DEBUG_BLOCK_END ===
        result = compressor.compress(data)

        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.compression_ratio = result.compression_ratio
        cr.time_ms = result.time_ms
        cr.data = result.data
        cr.success = result.success
        cr.error_message = result.error_message
        return cr

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm in (AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265):
            from gui.engine.ffmpeg_codec import VideoFFmpegCompressor

            codec = "h264" if algorithm == AlgorithmType.FFMPEG_H264 else "hevc"
            fc = VideoFFmpegCompressor(codec=codec)
            result = fc.decompress(data)
            cr = self._engine.CompressorResult()
            cr.original_size = result["original_size"]
            cr.compressed_size = result["compressed_size"]
            cr.compression_ratio = result["compression_ratio"]
            cr.time_ms = result["time_ms"]
            cr.data = list(result["data"])
            cr.success = result["success"]
            cr.error_message = result["error_message"]
            return cr

        if algorithm == AlgorithmType.TRANSFORMER:
            cr = self._engine.CompressorResult()
            cr.success = False
            cr.error_message = "Transformer (beta) 暂不支持解压"
            return cr

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        from gui.engine.decompress_log import log_decompress, summarize_result

        log_decompress(
            "decompress_native_begin",
            algo=algorithm.value,
            compressed_bytes=len(data),
        )

        with CompressionEngine._engine_op_lock:
            cfg_snap = CompressionEngine._deep_copy_config(
                CompressionEngine._get_config_unlocked()
            )
        compressor = self._create_compressor(algorithm, cfg_snap)
        result = compressor.decompress(data)

        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.time_ms = result.time_ms
        cr.data = result.data
        cr.success = result.success
        cr.error_message = result.error_message
        log_decompress("decompress_native_end", **summarize_result(cr))
        return cr

    def should_use_streaming(
        self, data_size: int, algorithm: AlgorithmType | None = None
    ) -> bool:
        from gui.config.settings import get_effective_streaming_threshold_mb

        mb = get_effective_streaming_threshold_mb(algorithm, _load_app_config())
        return data_size > mb * 1024 * 1024

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

    def smart_compress(
        self,
        data: bytes,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
        *,
        force_memory_codec: bool = False,
    ):
        if algorithm == AlgorithmType.TRANSFORMER:
            return self.compress(data, algorithm)

        if algorithm == AlgorithmType.GZIP:
            return self.compress(data, algorithm)

        if algorithm in (AlgorithmType.FLAC, AlgorithmType.AAC_LC, AlgorithmType.H264,
                         AlgorithmType.OPENH264,
                         AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265):
            logger.debug("[smart_compress] media algorithm: %s, data=%d bytes, calling C++ compress...",
                        algorithm.value, len(data))
            result = self.compress(data, algorithm)
            logger.debug("[smart_compress] media compress returned: success=%s size=%s",
                        getattr(result, 'success', '?'), getattr(result, 'compressed_size', '?'))
            return result

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if force_memory_codec:
            return self.pipeline_compress(data, algorithm)

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

        if algorithm in (AlgorithmType.FLAC, AlgorithmType.AAC_LC, AlgorithmType.H264,
                         AlgorithmType.OPENH264,
                         AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265):
            r = self.decompress(data, algorithm)
            log_decompress("smart_decompress_end", **summarize_result(r))
            return r

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if self.should_use_streaming(len(data), algorithm):
            from gui.config.settings import get_effective_streaming_threshold_mb

            eff_mb = get_effective_streaming_threshold_mb(algorithm, _load_app_config())
            logger.info(
                "[smart_decompress] using streaming mode for %d bytes (threshold=%.1f MB)",
                len(data),
                eff_mb,
            )
            log_decompress(
                "smart_decompress_branch",
                mode="over_threshold",
                threshold_mb=eff_mb,
            )
            if algorithm == AlgorithmType.GZIP:
                r = self._gzip_bytes_decompress(data)
                if r.success:
                    log_decompress("smart_decompress_end", **summarize_result(r))
                    return r
                logger.warning(
                    "[smart_decompress] gzip streaming failed: %s, fallback to normal",
                    r.error_message,
                )
                log_decompress(
                    "smart_decompress_fallback",
                    reason="gzip_fail",
                    err=r.error_message,
                )
            else:
                try:
                    r = self.pipeline_decompress(data, algorithm)
                    if not r.success and "bad_alloc" in str(getattr(r, "error_message", "") or "").lower():
                        logger.warning(
                            "[smart_decompress] pipeline returned std::bad_alloc; falling back to one-shot"
                        )
                        log_decompress(
                            "smart_decompress_fallback",
                            reason="pipeline_bad_alloc",
                            err=str(r.error_message),
                        )
                    else:
                        log_decompress("smart_decompress_end", **summarize_result(r))
                        return r
                except Exception as e:
                    logger.warning("[smart_decompress] streaming failed, fallback to normal: %s", e)
                    log_decompress(
                        "smart_decompress_fallback",
                        reason="pipeline_exception",
                        err=str(e),
                    )

        from gui.engine.file_protocol import is_u32_be_chunk_framed_stream_payload

        framed = algorithm in (
            AlgorithmType.DEFLATE,
            AlgorithmType.LZSS,
            AlgorithmType.LZDP,
            AlgorithmType.DPFLATE,
        ) and is_u32_be_chunk_framed_stream_payload(data)

        if framed:
            log_decompress(
                "smart_decompress_branch",
                mode="pipeline_framed_wire",
                reason="u32_chunk_framed_payload",
            )
            try:
                r = self.pipeline_decompress(data, algorithm)
                if not r.success and "bad_alloc" in str(getattr(r, "error_message", "") or "").lower():
                    logger.warning(
                        "[smart_decompress] framed pipeline returned std::bad_alloc; falling back to one-shot"
                    )
                    log_decompress(
                        "smart_decompress_fallback",
                        reason="framed_pipeline_bad_alloc",
                        err=str(r.error_message),
                    )
                else:
                    log_decompress(
                        "smart_decompress_end",
                        branch="pipeline_framed_wire",
                        **summarize_result(r),
                    )
                    return r
            except Exception as e:
                logger.warning(
                    "[smart_decompress] framed pipeline failed, fallback one_shot: %s",
                    e,
                )
                log_decompress(
                    "smart_decompress_fallback",
                    reason="framed_pipeline_exception",
                    err=str(e),
                )

        log_decompress("smart_decompress_branch", mode="compressor_one_shot")
        r = self.decompress(data, algorithm)
        log_decompress(
            "smart_decompress_end",
            branch="compressor_one_shot",
            **summarize_result(r),
        )
        return r

    def smart_compress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
        *,
        chunk_bytes_override: int | None = None,
    ):
        """Streaming compress file-to-file without loading into Python memory."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        # Native paths may use ``WEBCOMPRESS_WORKSPACE`` (e.g. DPFlate spill); set before file I/O.
        from gui.utils.workspace import ensure_workspace_layout

        ensure_workspace_layout()
        if algorithm == AlgorithmType.GZIP:
            logger.info(
                "[smart_compress_file] %s -> %s via gzip (stdlib, chunked copy)",
                input_path,
                output_path,
            )
            return self._gzip_smart_compress_file(input_path, output_path)

        with CompressionEngine._engine_op_lock:
            cfg_snap = CompressionEngine._deep_copy_config(
                CompressionEngine._get_config_unlocked()
            )
            algo_id = self._get_pipeline_id(algorithm)
            if algo_id is None:
                raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")
            file_cfg = _load_app_config()
            if chunk_bytes_override is not None and int(chunk_bytes_override) > 0:
                chunk_bytes = int(chunk_bytes_override)
            else:
                chunk_bytes = int(_get_effective_chunk_kb(algorithm, file_cfg)) * 1024
            file_opts = 0
            lzdp_wf = None
            dpflate_p = None
            deflate_p = None
            lzss_p = None
            if algorithm == AlgorithmType.LZDP:
                file_opts |= _FILE_COMPRESS_LZDP_WHOLE_FILE
                lzdp_wf = self._lzdp_whole_file_params_from_cfg(
                    cfg_snap.get(AlgorithmType.LZDP, {})
                )
            elif algorithm == AlgorithmType.DPFLATE:
                dpflate_p = self._dpflate_pipeline_params_from_cfg(
                    cfg_snap.get(AlgorithmType.DPFLATE, {})
                )
            elif algorithm == AlgorithmType.DEFLATE:
                deflate_p = self._deflate_pipeline_params_from_cfg(
                    cfg_snap.get(AlgorithmType.DEFLATE, {})
                )
            elif algorithm == AlgorithmType.LZSS:
                lzss_p = self._lzss_pipeline_params_from_cfg(
                    cfg_snap.get(AlgorithmType.LZSS, {})
                )
        logger.info(
            "[smart_compress_file] %s -> %s via %s (chunk_bytes=%d, file_opts=%d)",
            input_path,
            output_path,
            algorithm.value,
            chunk_bytes,
            file_opts,
        )
        _MEDIA_VIDEO_SET = frozenset({AlgorithmType.H264, AlgorithmType.OPENH264,
                                       AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265})
        if algorithm in _MEDIA_VIDEO_SET:
            logger.info("[smart_compress_file] DEBUG: media video algo=%s about to call C++ pipeline_compress_file", algorithm.value)
        return self._engine.pipeline_compress_file(
            input_path,
            output_path,
            [algo_id],
            chunk_bytes,
            file_opts,
            lzdp_wf,
            dpflate_p,
            deflate_p,
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
        from gui.engine.web_dict import postprocess_after_codec

        out_bytes = postprocess_after_codec(
            out_bytes, web_dict_preprocess=bool(getattr(hdr, "web_dict_preprocess", False))
        )
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
            r = self._gzip_smart_decompress_file(input_path, output_path)
            out_sz = os.path.getsize(output_path) if os.path.isfile(output_path) else -1
            f = summarize_result(r)
            f["output_file_bytes"] = out_sz
            log_decompress("smart_decompress_file_end", **f)
            return r

        if algorithm in (AlgorithmType.JPEG, AlgorithmType.PNG):
            from gui.engine.decompress_log import log_decompress, summarize_result

            in_sz = os.path.getsize(input_path) if os.path.isfile(input_path) else -1
            log_decompress(
                "smart_decompress_file_begin",
                algo=algorithm.value,
                input=input_path,
                output=output_path,
                input_file_bytes=in_sz,
                mode="raw_image_no_wcx",
            )
            t0 = time.perf_counter()
            try:
                data = Path(input_path).read_bytes()
                dr = self.pipeline_decompress(data, algorithm)
            except Exception as e:
                ms = (time.perf_counter() - t0) * 1000.0
                r = self._make_pipeline_result(False, 0, 0, ms, f"image decompress: {e}", None)
                log_decompress("smart_decompress_file_end", **summarize_result(r))
                return r
            if not dr.success:
                ms = (time.perf_counter() - t0) * 1000.0
                em = getattr(dr, "error_message", "") or "decode failed"
                r = self._make_pipeline_result(False, 0, 0, ms, em, None)
                log_decompress("smart_decompress_file_end", **summarize_result(r))
                return r
            out_bytes = dr.data if isinstance(dr.data, (bytes, bytearray)) else bytes(dr.data)
            Path(output_path).write_bytes(out_bytes)
            ms = (time.perf_counter() - t0) * 1000.0
            r = self._make_pipeline_result(True, in_sz, len(out_bytes), ms, "", None)
            log_decompress("smart_decompress_file_end", **summarize_result(r))
            return r

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")
        file_cfg = _load_app_config()
        chunk_bytes = int(_get_effective_chunk_kb(algorithm, file_cfg)) * 1024

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

    def pack_files(self, records: list[FileRecord]) -> bytes:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        files = []
        for rec in records:
            f = self._engine.File()
            f.filepath = rec.path
            f.context = rec.raw_data
            files.append(f)

        packed = self._engine.Archiver.pack(files)
        return bytes(packed)

    def unpack_archive(self, data: bytes) -> list[FileRecord]:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        raw_files = self._engine.Archiver.unpack(data)
        records = []
        for wf in raw_files:
            records.append(
                FileRecord(
                    path=wf.filepath,
                    name=Path(wf.filepath).name,
                    extension=Path(wf.filepath).suffix.lower(),
                    size=len(wf.context),
                    raw_data=bytes(wf.context),
                )
            )
        return records

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
                AlgorithmType.JPEG: eng.AlgorithmID.IMAGE_JPEG,
                AlgorithmType.PNG: eng.AlgorithmID.IMAGE_PNG,
                AlgorithmType.FLAC: eng.AlgorithmID.AUDIO_FLAC,
                AlgorithmType.AAC_LC: eng.AlgorithmID.AUDIO_AAC_LC,
                AlgorithmType.H264: eng.AlgorithmID.VIDEO_H264,
            }
        return CompressionEngine._ALGO_TO_PIPELINE_ID.get(algorithm)

    def _get_decompress_pipeline_id(self, algorithm: AlgorithmType):
        mapping = {
            AlgorithmType.DEFLATE: self._engine.AlgorithmID.INFLATE,
            AlgorithmType.LZSS: self._engine.AlgorithmID.LZSS_DECOMPRESS,
            AlgorithmType.LZDP: self._engine.AlgorithmID.LZMINE_DECOMPRESS,
            AlgorithmType.DPFLATE: self._engine.AlgorithmID.DPFLATE,
            AlgorithmType.BROTLI: self._engine.AlgorithmID.BROTLI_DECOMPRESS,
            AlgorithmType.ZSTD: self._engine.AlgorithmID.ZSTD_DECOMPRESS,
            AlgorithmType.JPEG: self._engine.AlgorithmID.IMAGE_JPEG_DECOMPRESS,
            AlgorithmType.PNG: self._engine.AlgorithmID.IMAGE_PNG_DECOMPRESS,
            AlgorithmType.FLAC: self._engine.AlgorithmID.AUDIO_FLAC_DECOMPRESS,
            AlgorithmType.AAC_LC: self._engine.AlgorithmID.AUDIO_AAC_LC_DECOMPRESS,
            AlgorithmType.H264: self._engine.AlgorithmID.VIDEO_H264_DECOMPRESS,
        }
        return mapping.get(algorithm)

    def _lzss_pipeline_params_from_cfg(self, c: dict) -> object:
        """``LzssPipelineParams`` for C++ pipeline LZSS; mirrors ``_create_compressor`` LZSS knobs."""
        eng = self._engine
        p = eng.LzssPipelineParams()
        p.search_size = int(c.get("search_size", 4095))
        p.lookahead_size = int(c.get("lookahead_size", 255))
        p.min_match = int(c.get("min_match", 0))
        p.use_flag_encoding = bool(int(c.get("use_flag_encoding", 1)))
        return p

    def _lzss_pipeline_params_for_file_pipeline(self):
        with CompressionEngine._engine_op_lock:
            c = CompressionEngine._get_config_unlocked().get(AlgorithmType.LZSS, {})
        return self._lzss_pipeline_params_from_cfg(c)

    def _lzdp_whole_file_params_from_cfg(self, c: dict) -> object:
        """``LzdpWholeFileParams`` for C++ whole-file LZDP; mirrors ``_create_compressor`` LZDP knobs."""
        eng = self._engine
        p = eng.LzdpWholeFileParams()
        p.search_size = int(c.get("search_size", 4096))
        p.lookahead_size = int(c.get("lookahead_size", 256))
        p.min_match = int(c.get("min_match", 0))
        p.dp_top = int(c.get("dp_top", 3))
        p.use_flag_encoding = bool(int(c.get("use_flag_encoding", 0)))
        p.match_engine = int(c.get("match_engine", 0))
        return p

    def _lzdp_whole_file_params_for_file_pipeline(self):
        with CompressionEngine._engine_op_lock:
            c = CompressionEngine._get_config_unlocked().get(AlgorithmType.LZDP, {})
        return self._lzdp_whole_file_params_from_cfg(c)

    def _deflate_pipeline_params_from_cfg(self, c: dict) -> object:
        """``DeflatePipelineParams`` for C++ streaming ``algorithm::Deflate``; mirrors GUI Deflate row."""
        eng = self._engine
        p = eng.DeflatePipelineParams()
        p.search_size = int(c.get("search_size", 4096))
        p.lookahead_size = int(c.get("lookahead_size", 256))
        p.min_match = int(c.get("min_match", 0))
        p.max_chain_length = int(c.get("max_chain_length", 256))
        p.use_flag_encoding = bool(int(c.get("use_flag_encoding", 1)))
        p.use_3hfmtree = bool(int(c.get("use_3hfmtree", 0)))
        base = int(c["huffman_chunk_bits"]) if "huffman_chunk_bits" in c else 8
        p.huffman_offset_chunk_bits = int(c.get("huffman_offset_chunk_bits", base))
        p.huffman_length_chunk_bits = int(c.get("huffman_length_chunk_bits", base))
        return p

    def _deflate_pipeline_params_for_file_pipeline(self):
        with CompressionEngine._engine_op_lock:
            c = CompressionEngine._get_config_unlocked().get(AlgorithmType.DEFLATE, {})
        return self._deflate_pipeline_params_from_cfg(c)

    def _dpflate_pipeline_params_from_cfg(self, c: dict) -> object:
        """``DpflatePipelineParams`` for C++ streaming DPFlate; mirrors ``_create_compressor`` knobs."""
        eng = self._engine
        p = eng.DpflatePipelineParams()
        p.search_size = int(c.get("search_size", 4096))
        p.lookahead_size = int(c.get("lookahead_size", 256))
        p.min_match = int(c.get("min_match", 0))
        p.max_chain_length = int(c.get("max_chain_length", 256))
        p.dp_sub_match_max = int(c.get("dp_sub_match_max", 6))
        p.use_flag_encoding = bool(int(c.get("use_flag_encoding", 0)))
        p.match_engine = int(c.get("match_engine", 1))
        p.use_3hfmtree = bool(int(c.get("use_3hfmtree", 0)))
        base = int(c["huffman_chunk_bits"]) if "huffman_chunk_bits" in c else 8
        p.huffman_offset_chunk_bits = int(c.get("huffman_offset_chunk_bits", base))
        p.huffman_length_chunk_bits = int(c.get("huffman_length_chunk_bits", base))
        return p

    def _dpflate_pipeline_params_for_file_pipeline(self):
        with CompressionEngine._engine_op_lock:
            c = CompressionEngine._get_config_unlocked().get(AlgorithmType.DPFLATE, {})
        return self._dpflate_pipeline_params_from_cfg(c)

    def pipeline_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        with CompressionEngine._engine_op_lock:
            cfg_snap = CompressionEngine._deep_copy_config(
                CompressionEngine._get_config_unlocked()
            )
            algo_id = self._get_pipeline_id(algorithm)
            if algo_id is None:
                raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

            file_cfg = _load_app_config()
            chunk_bytes = int(_get_effective_chunk_kb(algorithm, file_cfg)) * 1024
            lzdp_wf = None
            dpflate_p = None
            deflate_p = None
            lzss_p = None
            if algorithm == AlgorithmType.LZDP:
                lzdp_wf = self._lzdp_whole_file_params_from_cfg(
                    cfg_snap.get(AlgorithmType.LZDP, {})
                )
            elif algorithm == AlgorithmType.DPFLATE:
                dpflate_p = self._dpflate_pipeline_params_from_cfg(
                    cfg_snap.get(AlgorithmType.DPFLATE, {})
                )
            elif algorithm == AlgorithmType.DEFLATE:
                deflate_p = self._deflate_pipeline_params_from_cfg(
                    cfg_snap.get(AlgorithmType.DEFLATE, {})
                )
            elif algorithm == AlgorithmType.LZSS:
                lzss_p = self._lzss_pipeline_params_from_cfg(
                    cfg_snap.get(AlgorithmType.LZSS, {})
                )

        return self._engine.pipeline_compress(
            data,
            [algo_id],
            lzdp_wf,
            dpflate_p,
            deflate_p,
            lzss_p,
            chunk_bytes,
        )

    def pipeline_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")

        with CompressionEngine._engine_op_lock:
            cfg_snap = CompressionEngine._deep_copy_config(
                CompressionEngine._get_config_unlocked()
            )
        file_cfg = _load_app_config()
        chunk_bytes = int(_get_effective_chunk_kb(algorithm, file_cfg)) * 1024
        lzdp_wf = None
        dpflate_p = None
        deflate_p = None
        lzss_p = None
        if algorithm == AlgorithmType.LZDP:
            lzdp_wf = self._lzdp_whole_file_params_from_cfg(cfg_snap.get(AlgorithmType.LZDP, {}))
        elif algorithm == AlgorithmType.DPFLATE:
            dpflate_p = self._dpflate_pipeline_params_from_cfg(
                cfg_snap.get(AlgorithmType.DPFLATE, {})
            )
        elif algorithm == AlgorithmType.DEFLATE:
            deflate_p = self._deflate_pipeline_params_from_cfg(
                cfg_snap.get(AlgorithmType.DEFLATE, {})
            )
        elif algorithm == AlgorithmType.LZSS:
            lzss_p = self._lzss_pipeline_params_from_cfg(cfg_snap.get(AlgorithmType.LZSS, {}))

        from gui.engine.decompress_log import log_decompress, summarize_result

        log_decompress(
            "pipeline_decompress_begin",
            algo=algorithm.value,
            payload_bytes=len(data),
            chunk_bytes=chunk_bytes,
        )
        result = self._engine.pipeline_decompress(
            data,
            [decomp_id],
            lzdp_wf,
            dpflate_p,
            deflate_p,
            lzss_p,
            chunk_bytes,
        )
        log_decompress("pipeline_decompress_end", **summarize_result(result))
        return result

    def pipeline_compress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
        stream_chunk_bytes: int = 0,
        file_compress_opts: int | None = None,
        lzdp_whole_file=None,
        dpflate_pipeline=None,
        deflate_pipeline=None,
        lzss_pipeline=None,
    ):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        from gui.utils.workspace import ensure_workspace_layout

        ensure_workspace_layout()

        if algorithm == AlgorithmType.GZIP:
            return self._gzip_smart_compress_file(input_path, output_path)

        with CompressionEngine._engine_op_lock:
            cfg_snap = CompressionEngine._deep_copy_config(
                CompressionEngine._get_config_unlocked()
            )
            algo_id = self._get_pipeline_id(algorithm)
            if algo_id is None:
                raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

            opts = 0 if file_compress_opts is None else int(file_compress_opts)
            chunk = int(_get_effective_chunk_kb(algorithm, _load_app_config())) * 1024
            if stream_chunk_bytes and stream_chunk_bytes > 0:
                chunk = int(stream_chunk_bytes)

            lzdp_wf = None
            dpflate_p = None
            deflate_p = None
            lzss_p = None
            if algorithm == AlgorithmType.LZDP:
                opts |= _FILE_COMPRESS_LZDP_WHOLE_FILE
                lzdp_wf = (
                    lzdp_whole_file
                    if lzdp_whole_file is not None
                    else self._lzdp_whole_file_params_from_cfg(
                        cfg_snap.get(AlgorithmType.LZDP, {})
                    )
                )
            elif algorithm == AlgorithmType.DPFLATE:
                dpflate_p = (
                    dpflate_pipeline
                    if dpflate_pipeline is not None
                    else self._dpflate_pipeline_params_from_cfg(
                        cfg_snap.get(AlgorithmType.DPFLATE, {})
                    )
                )
            elif algorithm == AlgorithmType.DEFLATE:
                deflate_p = (
                    deflate_pipeline
                    if deflate_pipeline is not None
                    else self._deflate_pipeline_params_from_cfg(
                        cfg_snap.get(AlgorithmType.DEFLATE, {})
                    )
                )
            elif algorithm == AlgorithmType.LZSS:
                lzss_p = (
                    lzss_pipeline
                    if lzss_pipeline is not None
                    else self._lzss_pipeline_params_from_cfg(
                        cfg_snap.get(AlgorithmType.LZSS, {})
                    )
                )

        return self._engine.pipeline_compress_file(
            input_path,
            output_path,
            [algo_id],
            chunk,
            int(opts),
            lzdp_wf,
            dpflate_p,
            deflate_p,
            lzss_p,
        )

    def pipeline_decompress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
    ):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        if algorithm == AlgorithmType.GZIP:
            return self._gzip_smart_decompress_file(input_path, output_path)

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")

        chunk = int(_get_effective_chunk_kb(algorithm, _load_app_config())) * 1024
        lzdp_wf = None
        lzss_p = None
        dpflate_p = None
        deflate_p = None
        with CompressionEngine._engine_op_lock:
            cfg_snap = CompressionEngine._deep_copy_config(
                CompressionEngine._get_config_unlocked()
            )
        if algorithm == AlgorithmType.LZDP:
            lzdp_wf = self._lzdp_whole_file_params_from_cfg(
                cfg_snap.get(AlgorithmType.LZDP, {})
            )
        elif algorithm == AlgorithmType.LZSS:
            lzss_p = self._lzss_pipeline_params_from_cfg(
                cfg_snap.get(AlgorithmType.LZSS, {})
            )
        elif algorithm == AlgorithmType.DPFLATE:
            dpflate_p = self._dpflate_pipeline_params_from_cfg(
                cfg_snap.get(AlgorithmType.DPFLATE, {})
            )
        elif algorithm == AlgorithmType.DEFLATE:
            deflate_p = self._deflate_pipeline_params_from_cfg(
                cfg_snap.get(AlgorithmType.DEFLATE, {})
            )

        from gui.engine.decompress_log import log_decompress, summarize_result

        in_sz = os.path.getsize(input_path) if os.path.isfile(input_path) else -1
        native = os.environ.get("WEBCOMPRESS_NATIVE_DECOMPRESS_FILE", "").strip().lower() in (
            "1",
            "true",
            "yes",
        )
        if native:
            log_decompress(
                "pipeline_decompress_file_begin",
                mode="native_cpp",
                algo=algorithm.value,
                input=input_path,
                output=output_path,
                chunk_bytes=chunk,
                input_file_bytes=in_sz,
            )
            result = self._engine.pipeline_decompress_file(
                input_path,
                output_path,
                [decomp_id],
                chunk,
                lzdp_wf,
                lzss_p,
                dpflate_p,
                deflate_p,
            )
            out_sz = os.path.getsize(output_path) if os.path.isfile(output_path) else -1
            fields = summarize_result(result)
            fields["output_file_bytes"] = out_sz
            log_decompress("pipeline_decompress_file_end", **fields)
            return result

        return self._python_decompress_wcx_file_to_disk(
            input_path, output_path, algorithm, log_event_prefix="pipeline_decompress_file"
        )
