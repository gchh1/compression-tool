from __future__ import annotations

import logging
import os
import math
import sys
import time
import traceback
import uuid
from pathlib import Path

from PyQt6.QtCore import QThread, pyqtSignal

from gui.ade.engine import DecisionEngine
from gui.models import (
    AlgorithmType,
    CompressionStatus,
    FileRecord,
    FolderRecord,
    Record,
    validate_media_algorithm,
    is_media_algorithm,
)

logger = logging.getLogger('gui.worker')

def _crash_safe_flush() -> None:
    """Force flush all log handlers so crash-logs survive process termination."""
    for handler in logging.root.handlers:
        try:
            handler.flush()
        except Exception:
            pass
    try:
        sys.stderr.flush()
        sys.stdout.flush()
    except Exception:
        pass

IMAGE_ALGORITHMS = frozenset({AlgorithmType.JPEG, AlgorithmType.PNG})
AUDIO_ALGORITHMS = frozenset({AlgorithmType.FLAC, AlgorithmType.AAC_LC})
VIDEO_ALGORITHMS = frozenset({AlgorithmType.H264, AlgorithmType.OPENH264, AlgorithmType.FFMPEG_H264, AlgorithmType.FFMPEG_H265})

MEDIA_ALGORITHMS = IMAGE_ALGORITHMS | AUDIO_ALGORITHMS | VIDEO_ALGORITHMS

_MEDIA_EXT_MAP = {
    ".jpg": AlgorithmType.JPEG, ".jpeg": AlgorithmType.JPEG, ".jpe": AlgorithmType.JPEG,
    ".webp": AlgorithmType.JPEG, ".ico": AlgorithmType.PNG,
    ".png": AlgorithmType.PNG, ".gif": AlgorithmType.PNG, ".bmp": AlgorithmType.PNG,
    ".tiff": AlgorithmType.PNG, ".tif": AlgorithmType.PNG,
    ".wav": AlgorithmType.FLAC, ".flac": AlgorithmType.FLAC,
    ".ogg": AlgorithmType.FLAC, ".opus": AlgorithmType.FLAC,
    ".mid": AlgorithmType.FLAC, ".midi": AlgorithmType.FLAC,
    ".mp3": AlgorithmType.AAC_LC, ".aac": AlgorithmType.AAC_LC,
    ".wma": AlgorithmType.AAC_LC, ".m4a": AlgorithmType.AAC_LC,
    ".mp4": AlgorithmType.H264, ".avi": AlgorithmType.H264,
    ".mkv": AlgorithmType.H264, ".mov": AlgorithmType.H264,
    ".wmv": AlgorithmType.H264, ".flv": AlgorithmType.H264,
    ".webm": AlgorithmType.H264, ".m4v": AlgorithmType.H264,
    ".mpg": AlgorithmType.H264, ".mpeg": AlgorithmType.H264,
}


def _warn_media_mismatch(filerecord) -> None:
    if not hasattr(filerecord, "path") or not filerecord.path:
        return
    fext = Path(filerecord.path).suffix.lower()
    expected_media = _MEDIA_EXT_MAP.get(fext)
    if expected_media is None:
        return
    if hasattr(filerecord, "_original_algo"):
        return
    actual_algo = getattr(filerecord, "algorithm", None)
    if actual_algo not in MEDIA_ALGORITHMS:
        logger.warning(
            "[compress] media-extension file %s (ext=%s) compressed with general algo=%s; "
            "enable media compression for optimal results (expected=%s)",
            getattr(filerecord, "name", "?"),
            fext,
            actual_algo.value if actual_algo else "?",
            expected_media.value if expected_media else "?",
        )


COMPARISON_ALGORITHMS = (
    AlgorithmType.LZSS,
    AlgorithmType.LZDP,
    AlgorithmType.DPFLATE,
    AlgorithmType.DEFLATE,
    AlgorithmType.GZIP,
    AlgorithmType.BROTLI,
    AlgorithmType.ZSTD,
    AlgorithmType.FLAC,
    AlgorithmType.AAC_LC,
    AlgorithmType.H264,
    AlgorithmType.OPENH264,
    AlgorithmType.FFMPEG_H264,
    AlgorithmType.FFMPEG_H265,
)

_GENERAL_COMPARISON = (
    AlgorithmType.LZSS,
    AlgorithmType.LZDP,
    AlgorithmType.DPFLATE,
    AlgorithmType.DEFLATE,
    AlgorithmType.GZIP,
    AlgorithmType.BROTLI,
    AlgorithmType.ZSTD,
)


def get_comparison_algorithms_for_file(file_path: str) -> tuple[AlgorithmType, ...]:
    from gui.models import is_media_algorithm_suitable_for_file, IMAGE_EXTENSIONS, AUDIO_EXTENSIONS, VIDEO_EXTENSIONS
    ext = Path(file_path).suffix.lower()
    algos = list(_GENERAL_COMPARISON)
    if ext in IMAGE_EXTENSIONS:
        algos.append(AlgorithmType.JPEG)
        algos.append(AlgorithmType.PNG)
    if ext in AUDIO_EXTENSIONS:
        algos.append(AlgorithmType.FLAC)
        algos.append(AlgorithmType.AAC_LC)
    if ext in VIDEO_EXTENSIONS:
        algos.append(AlgorithmType.H264)
        algos.append(AlgorithmType.OPENH264)
        algos.append(AlgorithmType.FFMPEG_H264)
        algos.append(AlgorithmType.FFMPEG_H265)
    return tuple(algos)

def _heuristic_params_for_record(record: FileRecord, algo) -> dict[str, int] | None:
    from gui.models import ALGORITHM_PARAMS
    bf = getattr(record, 'base_features', None)
    if bf is None:
        return None
    param_defs = ALGORITHM_PARAMS.get(algo)
    if not param_defs:
        return None
    entropy = getattr(bf, 'shannon_entropy', 7.5)
    repetition = 1.0 - getattr(bf, 'unique_byte_ratio', 0.9)
    rle_potential = getattr(bf, 'rle_potential', 0.0)
    file_size = record.size
    params = {}
    for p in param_defs:
        if p.key == 'search_size' or p.key == 'window_size':
            if repetition > 0.3 and entropy < 6.5:
                val = min(p.max_val, int(p.default * 2))
            elif repetition > 0.1 or rle_potential > 0.05:
                val = min(p.max_val, int(p.default * 1.4))
            elif entropy > 7.8 or file_size < 4096:
                val = max(p.min_val, int(p.default * 0.6))
            else:
                val = p.default
            params[p.key] = max(p.min_val, min(p.max_val, val))
        elif p.key == 'lookahead_size':
            if repetition > 0.3 and entropy < 6.0:
                val = min(p.max_val, int(p.default * 1.5))
            elif entropy > 7.8:
                val = max(p.min_val, int(p.default * 0.7))
            else:
                val = p.default
            params[p.key] = max(p.min_val, min(p.max_val, val))
        elif p.key == 'max_chain_length':
            if repetition > 0.25 and entropy < 6.5:
                val = min(p.max_val, int(p.default * 2))
            elif entropy > 7.8 or file_size < 8192:
                val = max(p.min_val, int(p.default * 0.5))
            else:
                val = p.default
            params[p.key] = max(p.min_val, min(p.max_val, val))
        elif p.key == 'dp_top' or p.key == 'dp_depth' or p.key == 'dp_range' or p.key == 'dp_sub_match_max':
            if repetition > 0.35 and entropy < 5.8 and file_size > 100000:
                val = min(12, max(p.min_val, int(p.default * 1.5)))
            else:
                val = max(p.min_val, min(6, p.default))
            params[p.key] = val
        elif p.key == 'min_match':
            params[p.key] = p.default
        elif p.key == 'compression_level':
            if entropy < 5.0:
                params[p.key] = min(p.max_val, 7)
            elif entropy > 7.5:
                params[p.key] = max(p.min_val, 3)
            else:
                params[p.key] = p.default
        else:
            params[p.key] = p.default
    return params


def _core_set_streaming_compress_cancel(requested: bool) -> None:
    """Toggle C++ compressFile cooperative cancel (checked between read chunks)."""
    import threading
    logger.info("[CANCEL_TRACE] _core_set_streaming_compress_cancel(%s) thread=%s tid=%s",
                requested, threading.current_thread().name, threading.get_ident())
    try:
        from gui.engine.bridge import get_core_engine

        eng = get_core_engine()
        if eng is None:
            logger.warning("[cancel] get_core_engine() returned None")
            return
        fn = getattr(eng, "set_streaming_compress_cancel_requested", None)
        if callable(fn):
            logger.info("[cancel] calling C++ set_streaming_compress_cancel_requested(%s)", requested)
            fn(bool(requested))
        else:
            logger.warning("[cancel] set_streaming_compress_cancel_requested not found on engine")
    except Exception as e:
        logger.exception("[cancel] set_streaming_compress_cancel_requested failed: %s", e)


class CompressionWorker(QThread):
    progress = pyqtSignal(int, str)
    row_started = pyqtSignal(object)
    finished_row = pyqtSignal(object)
    error = pyqtSignal(object)

    def __init__(self, tasks: list[tuple[int, Record]], algorithm: AlgorithmType = AlgorithmType.LZDP):
        super().__init__()
        self.tasks = tasks
        self.algorithm = algorithm
        self._is_cancelled = False
        self._save_counter = 0

    def cancel(self):
        self._is_cancelled = True
        _core_set_streaming_compress_cancel(True)

    def _compress_phase_notify(self, record: Record, message: str) -> None:
        """Queued ``progress`` so the UI can refresh status during long non-streaming phases."""
        if self._is_cancelled:
            return
        name = getattr(record, "name", "?")
        self.progress.emit(0, f"{name}: {message}")

    def _flush_training_data(self, *, retrain: bool = True) -> None:
        try:
            from gui.ade.retrain import maybe_supplemental_retrain
            from gui.ade.training import get_training_store

            store = get_training_store()
            if store._dirty:
                store.save(incremental=True)
            if retrain:
                maybe_supplemental_retrain()
        except Exception:
            pass

    def _run_streaming_compress_branch(
        self,
        engine,
        record: FileRecord,
        folder_ref: FolderRecord | None,
        snap,
    ) -> None:
        """Write WCX via ``smart_compress_file``; finish record like the primary streaming path."""
        from gui.utils.workspace import allocate_streaming_wcx_path

        self._compress_phase_notify(record, "流式压缩（分块写盘）…")
        logger.info("[compress] STREAMING mode for %d bytes (file-to-file)", record.size)
        # === DEBUG_BLOCK_BEGIN (可删除) ===
        if record.algorithm == AlgorithmType.LZSS:
            try:
                with open("lzss_gui_debug.log", "a") as f:
                    f.write(f"[Python::streaming] LZSS file={getattr(record, 'path', 'N/A')} size={record.size}\n")
            except Exception:
                pass
        # === DEBUG_BLOCK_END ===
        out_path = str(allocate_streaming_wcx_path(record.path))
        _core_set_streaming_compress_cancel(False)
        from gui.ade.explorer import SilentExplorer

        _is_media2 = record.algorithm in MEDIA_ALGORITHMS
        logger.info("[compress] DEBUG streaming: calling smart_compress_file algo=%s in=%s out=%s size=%d media=%s",
                   record.algorithm.value, record.path, out_path, record.size, _is_media2)
        _crash_safe_flush()
        try:
            with SilentExplorer.user_compression_priority():
                result = engine.smart_compress_file(record.path, out_path, record.algorithm)
        except Exception as e:
            logger.exception("[compress] smart_compress_file CRASHED: %s", e)
            _crash_safe_flush()
            raise
        logger.info(
            "[compress] streaming compress returned success=%s compressed_size=%s",
            getattr(result, "success", None),
            getattr(result, "compressed_size", None),
        )

        em = (getattr(result, "error_message", None) or "").strip()
        succ = getattr(result, "success", True)
        if succ is False:
            record.status = CompressionStatus.FAILED
            low = em.lower()
            if self._is_cancelled or "cancel" in low or "取消" in em:
                record.error_message = em or "已取消"
            else:
                record.error_message = em or "压缩失败"
            record.compression_config_snapshot = None
            self.error.emit(record)
            return

        original_size = record.size
        compressed_size = result.compressed_size

        if compressed_size >= original_size:
            logger.info("[compress] EXPANSION detected (streaming), copying raw")
            import shutil

            shutil.copy2(record.path, out_path)
            record.compressed_path = out_path
            record.compressed_data = None
            record.algorithm = AlgorithmType.NONE
            record.compression_time_ms = result.time_ms
            record.compression_ratio = 1.0
            record.is_stored = True
            record.compression_config_snapshot = None
        else:
            record.compressed_path = out_path
            record.compressed_data = None
            record.compression_time_ms = result.time_ms
            record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
            record.is_stored = False
            record.compression_config_snapshot = snap

        record.status = CompressionStatus.DONE
        try:
            from gui.ade.training import get_training_store

            store = get_training_store()
            store.add_from_record(record, record.decision_result)
        except Exception:
            pass
        self._save_counter += 1
        if self._save_counter % 10 == 0:
            self._flush_training_data(retrain=False)
        try:
            from gui.ade.explore_log import log_explore
            from gui.ade.explorer import SilentExplorer

            ex = SilentExplorer.get()
            triggered = ex.maybe_explore(
                record, record.algorithm, compress_time_ms=record.compression_time_ms
            )
            logger.info(
                "[compress] maybe_explore enabled=%s triggered=%s file=%s",
                ex.enabled,
                triggered,
                getattr(record, "name", "") or "",
            )
            if triggered:
                log_explore(
                    "hook_after_compress",
                    file=getattr(record, "name", "") or "",
                    greedy=getattr(record.algorithm, "value", record.algorithm),
                    compress_time_ms=round(float(record.compression_time_ms or 0), 2),
                )
        except Exception as e:
            logger.warning("[compress] maybe_explore failed: %s", e)
        if folder_ref is not None:
            folder_ref.total_original += original_size
            folder_ref.total_compressed += compressed_size
            folder_ref.total_time_ms += record.compression_time_ms
            folder_ref.compression_ratio = (
                folder_ref.total_compressed / folder_ref.total_original
                if folder_ref.total_original > 0
                else 1.0
            )
            folder_ref.compression_time_ms = folder_ref.total_time_ms
            done_count = sum(1 for f in folder_ref.files if f.status == CompressionStatus.DONE)
            if done_count == len(folder_ref.files):
                folder_ref.status = CompressionStatus.DONE
            else:
                folder_ref.status = CompressionStatus.COMPRESSING
        self.finished_row.emit(record)

    def single_compress(self, row_idx: int, record: Record, folder_ref: FolderRecord | None = None) -> None:
        import traceback
        if self._is_cancelled:
            record.status = CompressionStatus.FAILED
            record.error_message = "已取消"
            self.error.emit(record)
            return
        self.row_started.emit(record)
        logger.info("[compress] START record=%s file=%s algo=%s", id(record), getattr(record, 'path', '?'), self.algorithm.value)
        _fname = str(getattr(record, 'name', '') or '')
        _fsize = int(getattr(record, 'size', 0) or 0)
        _fext = Path(record.path).suffix.lower() if hasattr(record, 'path') and record.path else ''
        logger.info("[compress] DEBUG meta: name=%s size=%d ext=%s auto=%s", _fname, _fsize, _fext, self.algorithm == AlgorithmType.AUTO)
        _crash_safe_flush()
        try:
            from gui.engine.compressor import CompressionEngine
            engine = CompressionEngine()
            if not engine.available:
                raise RuntimeError("C++ core_engine not available")

            use_streaming = False
            forced_no_stream = False

            if isinstance(record, FileRecord):
                # Always reload from disk: stale ``raw_data`` (e.g. prior web-dict encode)
                # must not be reused after the user turns off the dictionary option.
                record.web_dict_preprocess = False
                record.plaintext_snapshot = None
                record.base_features = None
                record.detected_file_type = None
                record.load_raw_data()

                from gui.config.settings import get_use_web_resource_dict

                _use_web_dict_cfg = get_use_web_resource_dict()

                if not hasattr(record, '_original_algo'):
                    record.algorithm = self.algorithm
                _auto_params = None
                if self.algorithm == AlgorithmType.AUTO:
                    logger.info("[compress] DEBUG auto_decision: entering ADE decide for %s (%d bytes)", _fname, _fsize)
                    _crash_safe_flush()
                    try:
                        record.extract_features()
                        feat_dims = len(record.base_features.vector) if record.base_features else 0
                        logger.info("[compress] DEBUG auto_decision: features extracted, feat_dims=%d", feat_dims)
                        _crash_safe_flush()
                        
                        decision_engine = DecisionEngine.get()
                        logger.info("[compress] DEBUG auto_decision: calling decide()...")
                        _crash_safe_flush()
                        decision = decision_engine.decide(record)
                        logger.info("[compress] DEBUG auto_decision: decide() returned algo=%s conf=%.2f mode=%s",
                                   decision.algorithm.value, decision.confidence, decision.mode_used)
                        _crash_safe_flush()
                        
                        if record.base_features is not None:
                            from gui.ade.features import get_compression_decision
                            py_decision = get_compression_decision(record.base_features)
                            if py_decision == "SKIP" and decision.confidence < 0.7:
                                record.algorithm = AlgorithmType.NONE
                                record.decision_result = decision
                                logger.info("[compress] ADE SKIP (Python: %s, C++ conf=%.2f)",
                                           py_decision, decision.confidence)
                            else:
                                record.algorithm = decision.algorithm
                                record.decision_result = decision
                                _auto_params = decision.params
                                logger.info("[compress] ADE decided: %s (conf=%.2f, reason=%s, py=%s)",
                                           decision.algorithm.value, decision.confidence,
                                           decision.reason, py_decision)
                        else:
                            record.algorithm = decision.algorithm
                            record.decision_result = decision
                            _auto_params = decision.params
                            logger.info("[compress] ADE decided: %s (conf=%.2f, reason=%s)",
                                        decision.algorithm.value, decision.confidence, decision.reason)
                    except Exception as e:
                        logger.warning("[compress] ADE failed, fallback to LZDP: %s", e)
                        record.algorithm = AlgorithmType.LZDP

                web_dict_active = False
                if _use_web_dict_cfg and record.algorithm != AlgorithmType.NONE:
                    from gui.engine.web_dict import prepare_file_record_for_compression

                    if prepare_file_record_for_compression(record):
                        web_dict_active = True
                        forced_no_stream = True
                        logger.info(
                            "[compress] web resource dict preprocess enabled for %s",
                            getattr(record, "name", "?"),
                        )

                _warn_media_mismatch(record)

                if _auto_params or (self.algorithm == AlgorithmType.AUTO and record.base_features is not None):
                    from gui.engine.compressor import CompressionEngine
                    from gui.models import merge_decision_overrides_into_algo_config, sanitize_stage2_params

                    current_cfg = CompressionEngine.get_config()
                    algo = record.algorithm
                    if not _auto_params:
                        _auto_params = _heuristic_params_for_record(record, algo)
                    if _auto_params:
                        file_sz = int(getattr(record, "size", 0) or 0)
                        _auto_params = sanitize_stage2_params(
                            algo, _auto_params, file_size=file_sz
                        )
                        algo_cfg = merge_decision_overrides_into_algo_config(
                            algo, current_cfg.get(algo, {}), _auto_params
                        )
                        full_cfg = dict(current_cfg)
                        full_cfg[algo] = algo_cfg
                        CompressionEngine.set_config(full_cfg, save=False)
                        logger.info("[compress] AUTO applied params for %s: %s",
                                   algo.value, _auto_params)
                        forced_no_stream = not engine.should_use_streaming(
                            file_sz, algo
                        )

                if is_media_algorithm(record.algorithm):
                    fext = Path(record.path).suffix.lower() if hasattr(record, "path") and record.path else ""
                    logger.info("[compress] DEBUG media_algo: algo=%s ext=%s file=%s size=%d",
                               record.algorithm.value, fext, _fname, _fsize)
                    _crash_safe_flush()
                    ok, msg = validate_media_algorithm(fext, record.algorithm)
                    if not ok:
                        record.status = CompressionStatus.FAILED
                        record.error_message = msg
                        record.compression_config_snapshot = None
                        logger.warning("[compress] media algo mismatch: %s (algo=%s ext=%s)", msg, record.algorithm.value, fext)
                        self.error.emit(record)
                        return

                use_streaming = (
                    not forced_no_stream
                    and not web_dict_active
                    and hasattr(record, "size")
                    and bool(getattr(record, "path", None))
                    and record.algorithm != AlgorithmType.NONE
                    and (folder_ref is not None or record.algorithm not in MEDIA_ALGORITHMS)
                    and engine.should_use_streaming(record.size, record.algorithm)
                )
            else:
                use_streaming = hasattr(record, "size") and engine.should_use_streaming(
                    record.size, None
                )

            if use_streaming and isinstance(record, FileRecord) and getattr(record, "path", None):
                logger.info("[compress] DEBUG branch: use_streaming=True (primary), algo=%s size=%d", record.algorithm.value, record.size)
                snap = CompressionEngine.snapshot_for_algorithm(record.algorithm)
                self._run_streaming_compress_branch(engine, record, folder_ref, snap)

            else:
                # Same threshold as above: avoid whole-file RAM + single pipeline.push when
                # file-to-file streaming is available (chunked push must match compressFile).
                if (
                    not web_dict_active
                    and isinstance(record, FileRecord)
                    and bool(getattr(record, "path", None))
                    and record.algorithm != AlgorithmType.NONE
                    and (folder_ref is not None or record.algorithm not in MEDIA_ALGORITHMS)
                    and engine.should_use_streaming(record.size, record.algorithm)
                ):
                    logger.info(
                        "[compress] redirect to STREAMING (path present, size=%d)",
                        record.size,
                    )
                    snap = CompressionEngine.snapshot_for_algorithm(record.algorithm)
                    self._run_streaming_compress_branch(engine, record, folder_ref, snap)
                    return

                if getattr(record, "base_features", None) is None:
                    self._compress_phase_notify(record, "正在提取特征…")
                    record.extract_features()
                logger.info("[compress] loaded raw data: %d bytes", len(record.raw_data))

                # === DEBUG_BLOCK_BEGIN (可删除) ===
                if record.algorithm == AlgorithmType.LZSS:
                    import logging as _lzss_log
                    _lzss_log.getLogger('gui.worker.lzss').info(
                        "[LZSS_DEBUG] single_compress LZSS path: raw_data=%d bytes, use_streaming=%s, path=%s",
                        len(record.raw_data), use_streaming,
                        getattr(record, 'path', 'N/A')
                    )
                    # Also write to file for C++ side correlation
                    try:
                        with open("lzss_gui_debug.log", "a") as f:
                            f.write(f"[Python::single_compress] LZSS raw_data={len(record.raw_data)} "
                                    f"use_streaming={use_streaming} path={getattr(record, 'path', 'N/A')}\n")
                    except Exception:
                        pass
                # === DEBUG_BLOCK_END ===

                logger.info(
                    "[compress] compressing with %s (web_dict_cfg=%s web_dict_active=%s bytes=%d) ...",
                    record.algorithm.value,
                    _use_web_dict_cfg,
                    web_dict_active,
                    len(record.raw_data),
                )
                snap = CompressionEngine.snapshot_for_algorithm(record.algorithm)
                can_stream_fallback = (
                    isinstance(record, FileRecord)
                    and bool(getattr(record, "path", None))
                    and record.algorithm != AlgorithmType.NONE
                    and (folder_ref is not None or record.algorithm not in MEDIA_ALGORITHMS)
                )
                self._compress_phase_notify(
                    record,
                    "正在内存压缩（耗时随体积与算法变化；可点「取消」中止）…",
                )
                result = None
                from gui.ade.explorer import SilentExplorer

                _is_media = record.algorithm in MEDIA_ALGORITHMS
                logger.info("[compress] DEBUG compress: calling smart_compress algo=%s bytes=%d media=%s",
                           record.algorithm.value, len(record.raw_data), _is_media)
                _crash_safe_flush()
                try:
                    with SilentExplorer.user_compression_priority():
                        result = engine.smart_compress(
                            record.raw_data,
                            record.algorithm,
                            force_memory_codec=web_dict_active,
                        )
                    logger.info(
                        "[compress] memory smart_compress returned success=%s compressed_size=%s",
                        getattr(result, "success", None),
                        getattr(result, "compressed_size", None),
                    )
                except Exception as e:
                    logger.exception("[compress] smart_compress raised: %s", e)
                    _crash_safe_flush()
                    em_ex = str(e).lower()
                    if self._is_cancelled or "cancel" in em_ex:
                        record.status = CompressionStatus.FAILED
                        record.error_message = "已取消"
                        record.compression_config_snapshot = None
                        self.error.emit(record)
                        return

                if result is None or getattr(result, "success", True) is False:
                    em_fail = (getattr(result, "error_message", None) or "").strip() if result else ""
                    if self._is_cancelled or "cancel" in em_fail.lower() or "取消" in em_fail:
                        record.status = CompressionStatus.FAILED
                        record.error_message = em_fail or "已取消"
                        record.compression_config_snapshot = None
                        self.error.emit(record)
                        return
                    if can_stream_fallback:
                        logger.warning("[compress] memory path failed; falling back to file-to-file")
                        self._run_streaming_compress_branch(engine, record, folder_ref, snap)
                        return
                    record.status = CompressionStatus.FAILED
                    if result is not None:
                        em_fail = (getattr(result, "error_message", None) or "").strip()
                        record.error_message = em_fail or "压缩失败"
                    else:
                        record.error_message = "压缩失败"
                    record.compression_config_snapshot = None
                    self.error.emit(record)
                    return

                em = (getattr(result, "error_message", None) or "").strip()
                if em:
                    record.status = CompressionStatus.FAILED
                    record.error_message = em
                    record.compression_config_snapshot = None
                    self.error.emit(record)
                    return

                payload = result.data
                if isinstance(payload, list) and can_stream_fallback:
                    logger.warning(
                        "[compress] engine returned list-shaped payload (len=%d); "
                        "using file-to-file to avoid huge Python allocation",
                        len(payload),
                    )
                    self._run_streaming_compress_branch(engine, record, folder_ref, snap)
                    return

                if isinstance(payload, bytes):
                    coerced = payload
                elif isinstance(payload, (bytearray, memoryview)):
                    coerced = bytes(payload)
                else:
                    coerced = bytes(payload)

                logger.info(
                    "[compress] compress done: %d -> %d bytes",
                    len(record.raw_data),
                    result.compressed_size,
                )

                original_size = int(getattr(record, "size", 0) or len(record.raw_data))
                compressed_size = result.compressed_size

                if compressed_size == 0 and original_size > 0:
                    logger.warning(
                        "[compress] EMPTY result: %d -> 0 bytes for algo=%s on file=%s (ext=%s); falling back to stored",
                        original_size,
                        record.algorithm.value,
                        getattr(record, "name", "?"),
                        Path(record.path).suffix.lower() if hasattr(record, "path") else "?",
                    )
                    compressed_size = original_size

                stored_plain = (
                    record.plaintext_snapshot
                    if getattr(record, "web_dict_preprocess", False) and record.plaintext_snapshot
                    else record.raw_data
                )

                is_img_algo = record.algorithm in IMAGE_ALGORITHMS and folder_ref is None
                is_audio_algo = record.algorithm in AUDIO_ALGORITHMS and folder_ref is None
                is_video_algo = record.algorithm in VIDEO_ALGORITHMS and folder_ref is None
                is_media_algo = is_img_algo or is_audio_algo or is_video_algo
                if is_img_algo:
                    img_ext = ".jpg" if record.algorithm == AlgorithmType.JPEG else ".png"
                    from gui.utils.workspace import compressed_dir
                    uid = uuid.uuid4().hex[:12]
                    stem = Path(record.path).stem if hasattr(record, "path") and record.path else "image"
                    img_path = str(compressed_dir() / f"{uid}_{stem}{img_ext}")
                    if compressed_size >= original_size:
                        Path(img_path).write_bytes(stored_plain)
                        record.compression_ratio = 1.0
                        record.is_stored = True
                        logger.info("[compress] %s EXPANSION, stored as %s (no WCX)", record.name, img_path)
                    else:
                        Path(img_path).write_bytes(coerced)
                        record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
                        record.is_stored = False
                        logger.info("[compress] %s compressed saved as %s (no WCX)", record.name, img_path)
                    record.compressed_path = img_path
                    record.compressed_data = None
                    record.compression_time_ms = result.time_ms
                    record.compression_config_snapshot = snap
                elif is_audio_algo:
                    audio_ext = ".flac" if record.algorithm == AlgorithmType.FLAC else ".aac"
                    from gui.utils.workspace import compressed_dir
                    uid = uuid.uuid4().hex[:12]
                    stem = Path(record.path).stem if hasattr(record, "path") and record.path else "audio"
                    audio_path = str(compressed_dir() / f"{uid}_{stem}{audio_ext}")
                    if compressed_size >= original_size:
                        Path(audio_path).write_bytes(stored_plain)
                        record.compression_ratio = 1.0
                        record.is_stored = True
                        logger.info("[compress] %s EXPANSION, stored as %s (no WCX)", record.name, audio_path)
                    else:
                        Path(audio_path).write_bytes(coerced)
                        record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
                        record.is_stored = False
                        logger.info("[compress] %s compressed saved as %s (no WCX)", record.name, audio_path)
                    record.compressed_path = audio_path
                    record.compressed_data = None
                    record.compression_time_ms = result.time_ms
                    record.compression_config_snapshot = snap
                elif is_video_algo:
                    from gui.utils.workspace import compressed_dir
                    uid = uuid.uuid4().hex[:12]
                    stem = Path(record.path).stem if hasattr(record, "path") and record.path else "video"
                    if record.algorithm == AlgorithmType.FFMPEG_H264:
                        video_ext = ".mp4"
                    elif record.algorithm == AlgorithmType.FFMPEG_H265:
                        video_ext = ".mp4"
                    else:
                        video_ext = ".h264"
                    video_path = str(compressed_dir() / f"{uid}_{stem}{video_ext}")
                    if compressed_size >= original_size:
                        Path(video_path).write_bytes(stored_plain)
                        record.compression_ratio = 1.0
                        record.is_stored = True
                        logger.info("[compress] %s EXPANSION, stored as %s (no WCX)", record.name, video_path)
                    else:
                        Path(video_path).write_bytes(coerced)
                        record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
                        record.is_stored = False
                        logger.info("[compress] %s compressed saved as %s (no WCX)", record.name, video_path)
                    record.compressed_path = video_path
                    record.compressed_data = None
                    record.compression_time_ms = result.time_ms
                    record.compression_config_snapshot = snap
                elif compressed_size >= original_size:
                    logger.info("[compress] EXPANSION detected: %d >= %d, falling back to stored (raw)",
                                 compressed_size, original_size)
                    record.compressed_data = stored_plain
                    record.compressed_path = None
                    record.algorithm = AlgorithmType.NONE
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = 1.0
                    record.is_stored = True
                    record.compression_config_snapshot = None
                else:
                    record.compressed_data = coerced
                    record.compressed_path = None
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
                    record.is_stored = False
                    record.compression_config_snapshot = snap

                record.status = CompressionStatus.DONE
                try:
                    from gui.ade.training import get_training_store
                    store = get_training_store()
                    store.add_from_record(record, record.decision_result)
                except Exception:
                    pass
                self._save_counter += 1
                if self._save_counter % 10 == 0:
                    self._flush_training_data(retrain=False)
                try:
                    from gui.ade.explore_log import log_explore
                    from gui.ade.explorer import SilentExplorer

                    ex = SilentExplorer.get()
                    triggered = ex.maybe_explore(
                        record, record.algorithm,
                        compress_time_ms=record.compression_time_ms,
                    )
                    logger.info(
                        "[compress] maybe_explore enabled=%s triggered=%s file=%s",
                        ex.enabled,
                        triggered,
                        getattr(record, "name", "") or "",
                    )
                    if triggered:
                        log_explore(
                            "hook_after_compress",
                            file=getattr(record, "name", "") or "",
                            greedy=getattr(record.algorithm, "value", record.algorithm),
                            compress_time_ms=round(float(record.compression_time_ms or 0), 2),
                        )
                except Exception as e:
                    logger.warning("[compress] maybe_explore failed: %s", e)
                if folder_ref is not None:
                    folder_ref.total_original += original_size
                    folder_ref.total_compressed += compressed_size
                    folder_ref.total_time_ms += record.compression_time_ms
                    folder_ref.compression_ratio = (
                        folder_ref.total_compressed / folder_ref.total_original
                        if folder_ref.total_original > 0 else 1.0
                    )
                    folder_ref.compression_time_ms = folder_ref.total_time_ms
                    done_count = sum(1 for f in folder_ref.files if f.status == CompressionStatus.DONE)
                    if done_count == len(folder_ref.files):
                        folder_ref.status = CompressionStatus.DONE
                    else:
                        folder_ref.status = CompressionStatus.COMPRESSING
                self.finished_row.emit(record)

        except Exception as e:
            logger.error("[compress] CRASH row=%d file=%s: %s\n%s", row_idx, getattr(record, 'path', '?'), e, traceback.format_exc())
            record.status = CompressionStatus.FAILED
            record.error_message = str(e)
            if folder_ref is not None:
                folder_ref.error_messages.append(f"{record.name}: {e}")
            self.error.emit(record)

    def run(self) -> None:
        from gui.ade.explorer import SilentExplorer

        try:
            with SilentExplorer.user_compression_priority():
                self._run_compress_tasks()
        finally:
            self._flush_training_data()
            _core_set_streaming_compress_cancel(False)

    def _run_compress_tasks(self) -> None:
        for row_idx, record in self.tasks:
            if self._is_cancelled:
                break
            if isinstance(record, FolderRecord):
                record.ensure_files_loaded()
                record.total_original = 0
                record.total_compressed = 0
                record.total_time_ms = 0.0
                record.compression_ratio = 1.0
                record.error_messages.clear()
                for f in record.files:
                    f.status = CompressionStatus.PENDING
                    f.compressed_data = None
                    f.is_stored = False
                    f.web_dict_preprocess = False
                    f.plaintext_snapshot = None
                record.status = CompressionStatus.PENDING
                for filerecord in record.files:
                    if self._is_cancelled:
                        break
                    if self.algorithm != AlgorithmType.AUTO:
                        from gui.config.settings import get_use_media_compression
                        if get_use_media_compression():
                            fext = Path(filerecord.path).suffix.lower() if hasattr(filerecord, "path") else ""
                            media_algo = _MEDIA_EXT_MAP.get(fext)
                            if media_algo is not None:
                                filerecord._original_algo = filerecord.algorithm
                                filerecord.algorithm = media_algo
                    self.single_compress(row_idx, filerecord, folder_ref=record)
                if not self._is_cancelled:
                    record_final_done = sum(1 for f in record.files if f.status == CompressionStatus.DONE)
                    if record_final_done > 0:
                        record.status = CompressionStatus.DONE
                        logger.info("[compress] FolderRecord DONE: %d/%d files succeeded", record_final_done, len(record.files))
                    elif record_final_done == 0 and len(record.files) > 0:
                        record.status = CompressionStatus.FAILED
                        record.error_messages.append("所有文件压缩失败")
            elif isinstance(record, FileRecord):
                record.status = CompressionStatus.PENDING
                record.compressed_data = None
                record.is_stored = False
                record.web_dict_preprocess = False
                record.plaintext_snapshot = None
                self.single_compress(row_idx, record)


class ComparisonWorker(QThread):
    """Run algorithm comparison away from the UI thread.

    Large files are compared in streaming-like chunks so the progress dialog can
    refresh after each completed chunk instead of waiting for the whole file.
    """

    progress = pyqtSignal(int, str)
    comparison_finished = pyqtSignal(object, str, int)
    failed = pyqtSignal(str)

    def __init__(
        self,
        record: Record,
        algorithms: tuple[AlgorithmType, ...] = COMPARISON_ALGORITHMS,
        parent=None,
    ):
        super().__init__(parent)
        self.record = record
        self.algorithms = algorithms
        from gui.config.settings import load_config

        self._stream_cfg = load_config()
        self._is_cancelled = False
        self._total_units = 1
        self._done_units = 0

    def _chunk_bytes(self, algo: AlgorithmType) -> int:
        from gui.config.settings import get_effective_streaming_chunk_kb

        return max(64 * 1024, int(get_effective_streaming_chunk_kb(algo, self._stream_cfg)) * 1024)

    def cancel(self) -> None:
        self._is_cancelled = True

    def _check_cancelled(self) -> None:
        if self._is_cancelled:
            raise RuntimeError("已取消")

    def _is_streaming_file(
        self, engine, record: FileRecord, algo: AlgorithmType
    ) -> bool:
        """Chunked comparison only for very large files; never with web-dict input."""
        from gui.config.settings import get_use_web_resource_dict

        if get_use_web_resource_dict():
            return False
        if not getattr(record, "path", None):
            return False
        # Below main compress streaming threshold: one-shot compare (matches gzip / memory path).
        if not engine.should_use_streaming(record.size, algo):
            return False
        return record.size > self._chunk_bytes(algo)

    def _units_for_file(
        self, engine, record: FileRecord, algo: AlgorithmType
    ) -> int:
        if self._is_streaming_file(engine, record, algo):
            chunk = self._chunk_bytes(algo)
            return max(1, math.ceil(record.size / chunk))
        return 1

    def _emit_step(self, text: str, units: int = 1) -> None:
        self._done_units += units
        percent = int(min(99, self._done_units / max(1, self._total_units) * 100))
        self.progress.emit(percent, text)

    def _load_comparison_input(self, record: FileRecord) -> bytes:
        """Plaintext from disk; optional web-dict encode (same input as main compress).

        Never reuse ``record.raw_data`` — it may still hold a prior dictionary token
        stream after compression, which makes every codec look worse in comparison.
        """
        if getattr(record, "path", None):
            data = Path(record.path).read_bytes()
        elif getattr(record, "raw_data", None):
            data = bytes(record.raw_data)
        else:
            return b""
        from gui.config.settings import get_use_web_resource_dict

        if get_use_web_resource_dict():
            from gui.engine.web_dict import encode

            data = encode(data)
        return data

    @staticmethod
    def _comparison_ratio(record: FileRecord, compressed_size: int) -> float:
        original = int(getattr(record, "size", 0) or 0)
        if original <= 0:
            return 1.0
        return compressed_size / original

    def _compress_data(self, engine, data: bytes, algo: AlgorithmType):
        from gui.ade.explorer import SilentExplorer

        with SilentExplorer.user_compression_priority():
            return engine.smart_compress(data, algo)

    def _compress_streaming_file(self, engine, record: FileRecord, algo: AlgorithmType) -> dict:
        from gui.ade.explorer import SilentExplorer

        compressed_size = 4  # stream terminator
        start = time.perf_counter()
        processed = 0
        chunk_sz = self._chunk_bytes(algo)

        with SilentExplorer.user_compression_priority():
            with open(record.path, "rb") as fh:
                while True:
                    self._check_cancelled()
                    chunk = fh.read(chunk_sz)
                    if not chunk:
                        break
                    result = engine.compress(chunk, algo)
                    if getattr(result, "error_message", ""):
                        raise RuntimeError(result.error_message)
                    compressed_size += int(result.compressed_size) + 4
                    processed += len(chunk)
                    self._emit_step(
                        f"{algo.value}: {record.name} ({processed / max(1, record.size):.0%})"
                    )

        elapsed_ms = (time.perf_counter() - start) * 1000.0
        return {
            "name": algo.value,
            "compressed_size": compressed_size,
            "ratio": compressed_size / record.size if record.size > 0 else 1.0,
            "time_ms": elapsed_ms,
        }

    def _compare_file(self, engine, record: FileRecord, algo: AlgorithmType) -> dict:
        self._check_cancelled()
        if self._is_streaming_file(engine, record, algo):
            return self._compress_streaming_file(engine, record, algo)

        data = self._load_comparison_input(record)
        result = self._compress_data(engine, data, algo)
        if getattr(result, "error_message", ""):
            raise RuntimeError(result.error_message)
        self._emit_step(f"{algo.value}: {record.name}")
        comp_sz = int(result.compressed_size)
        return {
            "name": algo.value,
            "compressed_size": comp_sz,
            "ratio": self._comparison_ratio(record, comp_sz),
            "time_ms": result.time_ms,
        }

    def _run_file_comparison(self, engine, record: FileRecord) -> tuple[list[dict], str, int]:
        self._total_units = sum(
            self._units_for_file(engine, record, algo) for algo in self.algorithms
        )
        results = []
        for algo in self.algorithms:
            try:
                results.append(self._compare_file(engine, record, algo))
                logger.info("[comparison] %s/%s done", record.name, algo.value)
            except RuntimeError as e:
                if str(e) == "已取消":
                    raise
                logger.warning("[comparison] %s compress failed: %s", algo.value, e)
            except Exception as e:
                logger.warning("[comparison] %s compress failed: %s", algo.value, e)
        return results, record.name, record.size

    def _run_folder_comparison(self, engine, record: FolderRecord) -> tuple[list[dict], str, int]:
        record.ensure_files_loaded()
        files = [f for f in record.files if f.size > 0]
        if not files:
            return [], record.name, 0

        self._total_units = sum(
            self._units_for_file(engine, f, algo)
            for algo in self.algorithms
            for f in files
        )
        totals: dict[str, dict] = {
            algo.value: {"compressed": 0, "time_ms": 0.0, "count": 0}
            for algo in self.algorithms
        }

        for algo in self.algorithms:
            for file_record in files:
                try:
                    result = self._compare_file(engine, file_record, algo)
                    totals[algo.value]["compressed"] += result["compressed_size"]
                    totals[algo.value]["time_ms"] += result["time_ms"]
                    totals[algo.value]["count"] += 1
                except RuntimeError as e:
                    if str(e) == "已取消":
                        raise
                    logger.warning("[comparison] folder %s/%s failed: %s", algo.value, file_record.name, e)
                except Exception as e:
                    logger.warning("[comparison] folder %s/%s failed: %s", algo.value, file_record.name, e)

        total_original = sum(f.size for f in files)
        results = []
        for algo in self.algorithms:
            item = totals[algo.value]
            if item["count"] > 0:
                results.append({
                    "name": algo.value,
                    "compressed_size": item["compressed"],
                    "ratio": item["compressed"] / total_original if total_original > 0 else 1.0,
                    "time_ms": item["time_ms"],
                })
        return results, record.name, total_original

    def run(self) -> None:
        try:
            from gui.engine.compressor import CompressionEngine

            engine = CompressionEngine()
            if not engine.available:
                raise RuntimeError("C++ core_engine not available")

            if isinstance(self.record, FolderRecord):
                results, name, original_size = self._run_folder_comparison(engine, self.record)
            elif isinstance(self.record, FileRecord):
                results, name, original_size = self._run_file_comparison(engine, self.record)
            else:
                raise RuntimeError("不支持的记录类型")

            self._check_cancelled()
            if not results:
                raise RuntimeError("所有算法压缩均失败")

            self.progress.emit(100, "算法对比完成")
            self.comparison_finished.emit(results, name, original_size)
        except RuntimeError as e:
            if str(e) == "已取消":
                self.progress.emit(0, "算法对比已取消")
                return
            logger.error("[comparison] failed: %s", e, exc_info=True)
            self.failed.emit(str(e))
        except Exception as e:
            logger.error("[comparison] crashed: %s", e, exc_info=True)
            self.failed.emit(str(e))
