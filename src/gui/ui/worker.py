from __future__ import annotations

import logging
import os
from pathlib import Path

from PyQt6.QtCore import QThread, pyqtSignal

from gui.ade.engine import DecisionEngine
from gui.models import (
    AlgorithmType,
    CompressionStatus,
    FileRecord,
    FolderRecord,
    Record,
)

logger = logging.getLogger('gui.worker')

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


class CompressionWorker(QThread):
    progress = pyqtSignal(int, str)
    row_started = pyqtSignal(int)
    finished_row = pyqtSignal(int)
    error = pyqtSignal(int)

    def __init__(self, tasks: list[tuple[int, Record]], algorithm: AlgorithmType = AlgorithmType.LZDP):
        super().__init__()
        self.tasks = tasks
        self.algorithm = algorithm
        self._is_cancelled = False
        self._save_counter = 0

    def cancel(self):
        self._is_cancelled = True

    def _flush_training_data(self):
        try:
            from gui.ade.training import get_training_store
            store = get_training_store()
            if store._dirty:
                store.save(incremental=True)
        except Exception:
            pass

    def single_compress(self, row_idx: int, record: Record, folder_ref: FolderRecord | None = None) -> None:
        import traceback
        if self._is_cancelled:
            record.status = CompressionStatus.FAILED
            record.error_message = "已取消"
            self.error.emit(row_idx)
            return
        self.row_started.emit(row_idx)
        logger.info("[compress] START row=%d file=%s algo=%s", row_idx, getattr(record, 'path', '?'), self.algorithm.value)
        try:
            from gui.engine.compressor import CompressionEngine
            engine = CompressionEngine()
            if not engine.available:
                raise RuntimeError("C++ core_engine not available")

            use_streaming = hasattr(record, 'size') and engine.should_use_streaming(record.size)

            if isinstance(record, FileRecord):
                record.algorithm = self.algorithm
                _auto_params = None
                if self.algorithm == AlgorithmType.AUTO:
                    try:
                        if not record.raw_data:
                            record.load_raw_data()
                        record.extract_features()
                        
                        decision_engine = DecisionEngine.get()
                        decision = decision_engine.decide(record)
                        
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

                if _auto_params or (self.algorithm == AlgorithmType.AUTO and record.base_features is not None):
                    from gui.engine.compressor import CompressionEngine
                    current_cfg = CompressionEngine.get_config()
                    algo = record.algorithm
                    if not _auto_params:
                        _auto_params = _heuristic_params_for_record(record, algo)
                    if _auto_params:
                        algo_cfg = dict(current_cfg.get(algo, {}))
                        algo_cfg.update(_auto_params)
                        full_cfg = dict(current_cfg)
                        full_cfg[algo] = algo_cfg
                        CompressionEngine.set_config(full_cfg, save=False)
                        logger.info("[compress] AUTO applied params for %s: %s",
                                   algo.value, _auto_params)
                        use_streaming = False

            if use_streaming and hasattr(record, 'path'):
                import os
                logger.info("[compress] STREAMING mode for %d bytes (file-to-file)", record.size)
                out_path = record.path + ".wcx"
                snap = CompressionEngine.snapshot_for_algorithm(record.algorithm)
                result = engine.smart_compress_file(record.path, out_path, record.algorithm)
                logger.info("[compress] streaming compress done: %d -> %d bytes", record.size, result.compressed_size)

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

                if hasattr(result, 'error_message') and result.error_message:
                    record.status = CompressionStatus.FAILED
                    record.error_message = result.error_message
                    record.compression_config_snapshot = None
                    self.error.emit(row_idx)
                else:
                    record.status = CompressionStatus.DONE
                    try:
                        from gui.ade.training import get_training_store
                        store = get_training_store()
                        store.add_from_record(record, record.decision_result)
                    except Exception:
                        pass
                    self._save_counter += 1
                    if self._save_counter % 10 == 0:
                        self._flush_training_data()
                    try:
                        from gui.ade.explorer import SilentExplorer
                        SilentExplorer.get().maybe_explore(
                            record, record.algorithm,
                            compress_time_ms=record.compression_time_ms
                        )
                    except Exception:
                        pass
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
                    self.finished_row.emit(row_idx)

            else:
                record.load_raw_data()
                record.extract_features()
                logger.info("[compress] loaded raw data: %d bytes", len(record.raw_data))

                logger.info("[compress] compressing with %s ...", record.algorithm.value)
                snap = CompressionEngine.snapshot_for_algorithm(record.algorithm)
                result = engine.smart_compress(record.raw_data, record.algorithm)
                logger.info("[compress] compress done: %d -> %d bytes", len(record.raw_data), result.compressed_size)

                original_size = len(record.raw_data)
                compressed_size = result.compressed_size

                if compressed_size >= original_size:
                    logger.info("[compress] EXPANSION detected: %d >= %d, falling back to stored (raw)",
                                 compressed_size, original_size)
                    record.compressed_data = record.raw_data
                    record.algorithm = AlgorithmType.NONE
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = 1.0
                    record.is_stored = True
                    record.compression_config_snapshot = None
                else:
                    record.compressed_data = bytes(result.data)
                    record.compression_time_ms = result.time_ms
                    record.compression_ratio = compressed_size / original_size if original_size > 0 else 0
                    record.is_stored = False
                    record.compression_config_snapshot = snap

                if hasattr(result, 'error_message') and result.error_message:
                    record.status = CompressionStatus.FAILED
                    record.error_message = result.error_message
                    record.compression_config_snapshot = None
                    self.error.emit(row_idx)
                else:
                    record.status = CompressionStatus.DONE
                    try:
                        from gui.ade.training import get_training_store
                        store = get_training_store()
                        store.add_from_record(record, record.decision_result)
                    except Exception:
                        pass
                    self._save_counter += 1
                    if self._save_counter % 10 == 0:
                        self._flush_training_data()
                    try:
                        from gui.ade.explorer import SilentExplorer
                        SilentExplorer.get().maybe_explore(
                            record, record.algorithm,
                            compress_time_ms=record.compression_time_ms
                        )
                    except Exception:
                        pass
                    if folder_ref is not None:
                        folder_ref.total_original += original_size
                        folder_ref.total_compressed += _compressed_size(record)
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
                    self.finished_row.emit(row_idx)

        except Exception as e:
            logger.error("[compress] CRASH row=%d file=%s: %s\n%s", row_idx, getattr(record, 'path', '?'), e, traceback.format_exc())
            record.status = CompressionStatus.FAILED
            record.error_message = str(e)
            if folder_ref is not None:
                folder_ref.error_messages.append(f"{record.name}: {e}")
            self.error.emit(row_idx)

    def run(self) -> None:
        for row_idx, record in self.tasks:
            if self._is_cancelled:
                break
            if isinstance(record, FolderRecord):
                record.total_original = 0
                record.total_compressed = 0
                record.total_time_ms = 0.0
                record.compression_ratio = 1.0
                record.error_messages.clear()
                for f in record.files:
                    f.status = CompressionStatus.PENDING
                    f.compressed_data = None
                    f.is_stored = False
                record.status = CompressionStatus.PENDING
                for filerecord in record.files:
                    if self._is_cancelled:
                        break
                    self.single_compress(row_idx, filerecord, folder_ref=record)
            elif isinstance(record, FileRecord):
                record.status = CompressionStatus.PENDING
                record.compressed_data = None
                record.is_stored = False
                self.single_compress(row_idx, record)

        self._flush_training_data()
