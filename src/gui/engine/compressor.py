from __future__ import annotations

import logging
import shutil
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
    _instance = None
    _config: dict[AlgorithmType, dict[str, int]] = None
    _streaming_threshold_mb: float = STREAMING_THRESHOLD_MB

    def __init__(self):
        self._engine = get_core_engine()
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
    def set_config(cls, config: dict[AlgorithmType, dict[str, int]], save: bool = True):
        cls._config = config
        if save:
            cls._save_to_file()

    @classmethod
    def get_config(cls) -> dict[AlgorithmType, dict[str, int]]:
        if cls._config is None:
            cls._config = get_default_config()
        return cls._config

    @classmethod
    def snapshot_for_algorithm(cls, algorithm: AlgorithmType) -> dict[str, int]:
        """Copy of engine config for ``algorithm`` at call time (for per-record demos)."""
        cfg = cls.get_config().get(algorithm, {})
        return {str(k): int(v) for k, v in cfg.items()}

    def create_compressor_for_visualization(
        self,
        algorithm: AlgorithmType,
        params: dict[str, int] | None = None,
    ):
        """Build compressor with global config, then overlay ``params`` if provided."""
        compressor = self._create_compressor(algorithm)
        if params:
            for key, val in params.items():
                setter = getattr(compressor, f"set_{key}", None)
                if setter:
                    setter(int(val))
        return compressor

    @classmethod
    def set_streaming_threshold(cls, mb: float, save: bool = True):
        cls._streaming_threshold_mb = max(0.0, float(mb))
        if save:
            cls._save_to_file()

    @classmethod
    def get_streaming_threshold(cls) -> float:
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
        cls._config = get_default_config()
        cls._streaming_threshold_mb = STREAMING_THRESHOLD_MB
        cls._save_to_file()

    def _create_compressor(self, algorithm: AlgorithmType):
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
        else:
            raise ValueError(f"Unsupported algorithm: {algorithm.value}")

        cfg = self._config.get(algorithm, {})
        for key, val in cfg.items():
            if key in _HUFFMAN_CFG_KEYS:
                continue
            setter = getattr(comp, f"set_{key}", None)
            if setter:
                setter(val)
        _apply_huffman_slot_config(comp, algorithm, cfg)
        return comp

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
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

        compressor = self._create_compressor(algorithm)
        result = compressor.compress(list(data))

        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.compression_ratio = result.compression_ratio
        cr.time_ms = result.time_ms
        cr.data = list(result.data)
        cr.success = result.success
        cr.error_message = result.error_message
        return cr

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            cr = self._engine.CompressorResult()
            cr.success = False
            cr.error_message = "Transformer (beta) 暂不支持解压"
            return cr

        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        compressor = self._create_compressor(algorithm)
        result = compressor.decompress(list(data))

        cr = self._engine.CompressorResult()
        cr.original_size = result.original_size
        cr.compressed_size = result.compressed_size
        cr.time_ms = result.time_ms
        cr.data = list(result.data)
        cr.success = result.success
        cr.error_message = result.error_message
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
        r.data = list(data) if data is not None else []
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

        if self.should_use_streaming(len(data), algorithm):
            from gui.config.settings import get_effective_streaming_threshold_mb

            eff_mb = get_effective_streaming_threshold_mb(algorithm, _load_app_config())
            logger.info(
                "[smart_compress] using streaming mode for %d bytes (threshold=%.1f MB)",
                len(data),
                eff_mb,
            )
            if algorithm == AlgorithmType.GZIP:
                r = self._gzip_bytes_compress(data)
                if r.success:
                    return r
                logger.warning(
                    "[smart_compress] gzip streaming failed: %s, fallback to normal",
                    r.error_message,
                )
            else:
                try:
                    return self.pipeline_compress(data, algorithm)
                except Exception as e:
                    logger.warning("[smart_compress] streaming failed, fallback to normal: %s", e)

        return self.compress(data, algorithm)

    def smart_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if algorithm == AlgorithmType.TRANSFORMER:
            return self.decompress(data, algorithm)

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
            if algorithm == AlgorithmType.GZIP:
                r = self._gzip_bytes_decompress(data)
                if r.success:
                    return r
                logger.warning(
                    "[smart_decompress] gzip streaming failed: %s, fallback to normal",
                    r.error_message,
                )
            else:
                try:
                    return self.pipeline_decompress(data, algorithm)
                except Exception as e:
                    logger.warning("[smart_decompress] streaming failed, fallback to normal: %s", e)

        return self.decompress(data, algorithm)

    def smart_compress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
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
        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")
        cfg = _load_app_config()
        chunk_bytes = int(_get_effective_chunk_kb(algorithm, cfg)) * 1024
        file_opts = 0
        lzdp_wf = None
        dpflate_p = None
        if algorithm == AlgorithmType.LZDP:
            file_opts |= _FILE_COMPRESS_LZDP_WHOLE_FILE
            lzdp_wf = self._lzdp_whole_file_params_for_file_pipeline()
        elif algorithm == AlgorithmType.DPFLATE:
            dpflate_p = self._dpflate_pipeline_params_for_file_pipeline()
        logger.info(
            "[smart_compress_file] %s -> %s via %s (chunk_bytes=%d, file_opts=%d)",
            input_path,
            output_path,
            algorithm.value,
            chunk_bytes,
            file_opts,
        )
        return self._engine.pipeline_compress_file(
            input_path, output_path, [algo_id], chunk_bytes, file_opts, lzdp_wf, dpflate_p
        )

    def smart_decompress_file(
        self,
        input_path: str,
        output_path: str,
        algorithm: AlgorithmType = AlgorithmType.DEFLATE,
    ):
        """Streaming decompress file-to-file without loading into Python memory."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        if algorithm == AlgorithmType.GZIP:
            logger.info(
                "[smart_decompress_file] %s -> %s via gzip (stdlib, chunked copy)",
                input_path,
                output_path,
            )
            return self._gzip_smart_decompress_file(input_path, output_path)
        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")
        cfg = _load_app_config()
        chunk_bytes = int(_get_effective_chunk_kb(algorithm, cfg)) * 1024
        logger.info(
            "[smart_decompress_file] %s -> %s via %s (chunk_bytes=%d)",
            input_path,
            output_path,
            algorithm.value,
            chunk_bytes,
        )
        return self._engine.pipeline_decompress_file(
            input_path, output_path, [decomp_id], chunk_bytes
        )

    def pack_files(self, records: list[FileRecord]) -> bytes:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        files = []
        for rec in records:
            f = self._engine.File()
            f.filepath = rec.path
            f.context = list(rec.raw_data)
            files.append(f)

        packed = self._engine.Archiver.pack(files)
        return bytes(packed)

    def unpack_archive(self, data: bytes) -> list[FileRecord]:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        raw_files = self._engine.Archiver.unpack(list(data))
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

    def _lzdp_whole_file_params_for_file_pipeline(self):
        """``LzdpWholeFileParams`` for C++ whole-file LZDP; mirrors ``_create_compressor`` LZDP knobs."""
        eng = self._engine
        p = eng.LzdpWholeFileParams()
        c = self.get_config().get(AlgorithmType.LZDP, {})
        p.search_size = int(c.get("search_size", 4096))
        p.lookahead_size = int(c.get("lookahead_size", 256))
        p.min_match = int(c.get("min_match", 0))
        p.dp_top = int(c.get("dp_top", 3))
        p.use_flag_encoding = bool(int(c.get("use_flag_encoding", 0)))
        p.match_engine = int(c.get("match_engine", 0))
        return p

    def _dpflate_pipeline_params_for_file_pipeline(self):
        """``DpflatePipelineParams`` for C++ streaming DPFlate; mirrors ``_create_compressor`` knobs."""
        eng = self._engine
        p = eng.DpflatePipelineParams()
        c = self.get_config().get(AlgorithmType.DPFLATE, {})
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

    def pipeline_compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

        result = self._engine.pipeline_compress(list(data), [algo_id])
        return result

    def pipeline_decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        decomp_id = self._get_decompress_pipeline_id(algorithm)
        if decomp_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline decompress mode")

        result = self._engine.pipeline_decompress(list(data), [decomp_id])
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
    ):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        from gui.utils.workspace import ensure_workspace_layout

        ensure_workspace_layout()

        if algorithm == AlgorithmType.GZIP:
            return self._gzip_smart_compress_file(input_path, output_path)

        algo_id = self._get_pipeline_id(algorithm)
        if algo_id is None:
            raise ValueError(f"Algorithm {algorithm.value} not supported in pipeline mode")

        opts = 0 if file_compress_opts is None else int(file_compress_opts)
        chunk = int(_get_effective_chunk_kb(algorithm, _load_app_config())) * 1024
        if stream_chunk_bytes and stream_chunk_bytes > 0:
            chunk = int(stream_chunk_bytes)

        lzdp_wf = None
        dpflate_p = None
        if algorithm == AlgorithmType.LZDP:
            opts |= _FILE_COMPRESS_LZDP_WHOLE_FILE
            lzdp_wf = (
                lzdp_whole_file
                if lzdp_whole_file is not None
                else self._lzdp_whole_file_params_for_file_pipeline()
            )
        elif algorithm == AlgorithmType.DPFLATE:
            dpflate_p = (
                dpflate_pipeline
                if dpflate_pipeline is not None
                else self._dpflate_pipeline_params_for_file_pipeline()
            )

        result = self._engine.pipeline_compress_file(
            input_path, output_path, [algo_id], chunk, int(opts), lzdp_wf, dpflate_p
        )
        return result

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

        result = self._engine.pipeline_decompress_file(
            input_path, output_path, [decomp_id], chunk
        )
        return result
