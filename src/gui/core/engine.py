from __future__ import annotations

import logging
from pathlib import Path

from gui.core.models import (
    AlgorithmType,
    CompressionStatus,
    FileRecord,
)

logger = logging.getLogger(__name__)
_core_engine = None


def _get_engine():
    global _core_engine
    if _core_engine is not None:
        return _core_engine

    import sys
    from pathlib import Path

    # Search for the pybind .so relative to the project root
    # engine.py → core/ → gui/ → src/ → project root
    _proj_root = Path(__file__).resolve().parent.parent.parent.parent
    _candidates = [
        _proj_root / "build" / "src" / "bindings" / "pybind",
        _proj_root / "build" / "src" / "bindings",
        _proj_root / "build_pybind" / "src" / "bindings" / "pybind",
        _proj_root / "build_pybind" / "src" / "bindings",
    ]
    for _p in _candidates:
        if _p.is_dir():
            sys.path.insert(0, str(_p))

    try:
        import core_engine
        _core_engine = core_engine
        logger.info("C++ core_engine loaded from %s", core_engine.__file__)
        return _core_engine
    except ImportError:
        logger.warning("core_engine not available, using fallback")
        return None


def _algo_chain(algorithm: AlgorithmType) -> list:
    """Map AlgorithmType to compression AlgorithmID chain."""
    if algorithm == AlgorithmType.DEFLATE:
        return [_get_engine().AlgorithmID.Deflate]
    elif algorithm == AlgorithmType.NONE:
        return []
    elif algorithm == AlgorithmType.AUTO:
        return [_get_engine().AlgorithmID.Deflate]
    return []


def _decomp_chain(algorithm: AlgorithmType) -> list:
    """Map AlgorithmType to decompression AlgorithmID chain."""
    if algorithm == AlgorithmType.DEFLATE:
        return [_get_engine().AlgorithmID.Inflate]
    elif algorithm == AlgorithmType.NONE:
        return []
    elif algorithm == AlgorithmType.AUTO:
        return [_get_engine().AlgorithmID.Inflate]
    return []


def _result_to_dict(result) -> dict:
    d = {
        'original_size': result.original_size,
        'compressed_size': result.compressed_size,
        'compression_ratio': result.compression_ratio,
        'time_ms': result.time_ms,
        'success': result.success,
        'error_message': result.error_message,
    }
    bp = result.block_profile
    if bp is not None:
        d['block_profile'] = {
            'blocks': [
                {
                    'block_index': b.block_index,
                    'literal_count': b.literal_count,
                    'match_count': b.match_count,
                    'll_tree_bits': b.ll_tree_bits,
                    'dist_tree_bits': b.dist_tree_bits,
                    'output_bytes': b.output_bytes,
                    'll_code_lengths': list(b.ll_code_lengths),
                    'dist_code_lengths': list(b.dist_code_lengths),
                }
                for b in bp.blocks
            ]
        }
    else:
        d['block_profile'] = None
    return d


class CompressionEngine:
    def __init__(self):
        self._engine = _get_engine()

    @property
    def available(self) -> bool:
        return self._engine is not None

    def compress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        chain = _algo_chain(algorithm)
        return self._engine.compress(list(data), chain)

    def decompress(self, data: bytes, algorithm: AlgorithmType = AlgorithmType.DEFLATE):
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        chain = _decomp_chain(algorithm)
        return self._engine.decompress(list(data), chain)

    def pack_files(self, records: list[FileRecord]) -> bytes:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        files = []
        for rec in records:
            f = self._engine.WebFile()
            f.name = rec.path
            f.content = list(rec.raw_data)
            files.append(f)

        chain = [_get_engine().AlgorithmID.Deflate]
        packed = self._engine.pack_and_compress(files, chain)
        return bytes(packed)

    def unpack_archive(self, data: bytes) -> list[FileRecord]:
        if not self.available:
            raise RuntimeError("C++ core_engine not available")

        raw_files = self._engine.decompress_and_unpack(list(data))
        records = []
        for wf in raw_files:
            records.append(FileRecord(
                path=wf.name,
                name=Path(wf.name).name,
                extension=Path(wf.name).suffix.lower(),
                size=len(wf.content),
                type=_identify_from_ext(Path(wf.name).suffix.lower()),
                raw_data=bytes(wf.content),
            ))
        return records

    # ---- streaming file API ----

    def compress_file(self, input_path: str, output_path: str,
                      algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> dict:
        """Stream-compress a single file (any size) to disk."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        chain = _algo_chain(algorithm)
        result = self._engine.compress_file(input_path, output_path, chain)
        return _result_to_dict(result)

    def decompress_file(self, input_path: str, output_path: str,
                        algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> dict:
        """Stream-decompress a single file to disk."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        chain = _decomp_chain(algorithm)
        result = self._engine.decompress_file(input_path, output_path, chain)
        return _result_to_dict(result)

    def compress_directory(self, dir_path: str, output_path: str,
                           algorithm: AlgorithmType = AlgorithmType.DEFLATE) -> dict:
        """Recursively pack and compress a directory into an archive file."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        chain = _algo_chain(algorithm)
        result = self._engine.compress_directory(dir_path, output_path, chain)
        return _result_to_dict(result)

    def decompress_and_unpack_to_disk(self, input_path: str,
                                       output_dir: str) -> dict:
        """Unpack an archive to disk, preserving directory structure."""
        if not self.available:
            raise RuntimeError("C++ core_engine not available")
        result = self._engine.decompress_and_unpack_to_disk(
            input_path, output_dir)
        return _result_to_dict(result)


def _identify_from_ext(ext: str):
    from gui.core.models import SCRIPT_EXTENSIONS, TEXT_EXTENSIONS, IMAGE_EXTENSIONS, ResourceType
    if ext in TEXT_EXTENSIONS:
        return ResourceType.TEXT
    if ext in IMAGE_EXTENSIONS:
        return ResourceType.IMAGE
    if ext in SCRIPT_EXTENSIONS:
        return ResourceType.SCRIPT
    return ResourceType.UNKNOWN
