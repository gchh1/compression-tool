"""Lazy mmap-based reader for .heat entropy / compression-ratio files.



Format v2 (entropy only):

    Header (20 bytes):

      magic:         u32 = 0x54414548 ("HEAT" LE)

      version:       u32 = 2

      total_chunks:  u32

      chunk_bytes:   u32

      avg_entropy:   f32

    Chunk Data: [total_chunks ├ù f32 entropy]



Format v3 (per-chunk compression ratio):

    Header (28 bytes):

      magic:         u32 = 0x54414548 ("HEAT" LE)

      version:       u32 = 3

      total_chunks:  u32

      chunk_bytes:   u32

      total_input:   u64

      total_output:  u64

    Chunk Data: [total_chunks ├ù f32 compression_ratio]

"""



from __future__ import annotations



import logging

import math

import mmap

import struct

from pathlib import Path

from typing import BinaryIO



logger = logging.getLogger(__name__)



HEAT_MAGIC = 0x54414548  # "HEAT" LE

HEAT_VERSION_V2 = 2

HEAT_VERSION_V3 = 3



_V2_HEADER_FMT = struct.Struct("<I I I I f")   # magic, version, total_chunks, chunk_bytes, avg_entropy

_V3_HEADER_FMT = struct.Struct("<I I I I Q Q")  # magic, version, total_chunks, chunk_bytes, total_input, total_output

_CHUNK_FMT = struct.Struct("<f")                 # single f32 (entropy in v2, ratio in v3)

_V2_HEADER_SIZE = _V2_HEADER_FMT.size            # 20 bytes

_V3_HEADER_SIZE = _V3_HEADER_FMT.size            # 28 bytes





class HeatmapController:

    """Lazy mmap-based reader for .heat v2 / v3 files.



    - Opens .heat via mmap on first access (``_ensure_mmap``).

    - Header parsed immediately ΓÇö O(1).

    - Chunk values accessed via O(1) mmap slice.

    - Byte-level heatmap slices from the original file via transient mmap.

    """



    def __init__(self, heat_path: str) -> None:

        self._heat_path = heat_path

        self._mm: mmap.mmap | None = None

        self._f: BinaryIO | None = None

        self._version: int = 0

        self._total_chunks: int = 0

        self._chunk_bytes: int = 0

        self._avg_entropy: float = 0.0

        self._total_input: int = 0

        self._total_output: int = 0

        self._data_offset: int = 0

        self._parse_header()



    # ΓöÇΓöÇ internal ΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇ



    def _ensure_mmap(self) -> mmap.mmap:

        if self._mm is not None:

            return self._mm

        self._f = open(self._heat_path, "rb")

        self._mm = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)

        return self._mm



    def _parse_header(self) -> None:

        mm = self._ensure_mmap()

        if len(mm) < _V2_HEADER_SIZE:

            raise ValueError(

                f"{self._heat_path}: file too small for .heat header "

                f"({len(mm)} < {_V2_HEADER_SIZE})"

            )



        # Read common prefix (magic + version + total_chunks + chunk_bytes)

        magic, version, self._total_chunks, self._chunk_bytes = \
            struct.unpack("<I I I I", mm[0:16])



        if magic != HEAT_MAGIC:

            raise ValueError(

                f"{self._heat_path}: bad magic 0x{magic:08X}, "

                f"expected 0x{HEAT_MAGIC:08X}"

            )



        self._version = version



        if version == HEAT_VERSION_V3:

            if len(mm) < _V3_HEADER_SIZE:

                raise ValueError(

                    f"{self._heat_path}: file too small for .heat v3 header"

                )

            _, _, _, _, self._total_input, self._total_output = \
                _V3_HEADER_FMT.unpack(mm[0:_V3_HEADER_SIZE])

            self._data_offset = _V3_HEADER_SIZE

            self._avg_entropy = 0.0

        elif version == HEAT_VERSION_V2:

            self._avg_entropy = struct.unpack("<f", mm[16:_V2_HEADER_SIZE])[0]

            self._total_input = self._chunk_bytes * self._total_chunks

            self._total_output = 0

            self._data_offset = _V2_HEADER_SIZE

        else:

            raise ValueError(

                f"{self._heat_path}: unsupported version {version} "

                f"(expected {HEAT_VERSION_V2} or {HEAT_VERSION_V3})"

            )



    def close(self) -> None:

        if self._mm is not None:

            self._mm.close()

            self._mm = None

        if self._f is not None:

            self._f.close()

            self._f = None



    # ΓöÇΓöÇ Properties ΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇ



    @property

    def version(self) -> int:

        return self._version



    @property

    def total_chunks(self) -> int:

        return self._total_chunks



    @property

    def chunk_bytes(self) -> int:

        return self._chunk_bytes



    @property

    def avg_entropy(self) -> float:

        return self._avg_entropy



    @property

    def total_input(self) -> int:

        """Actual total input bytes (v3: from header; v2: estimated)."""

        return self._total_input



    @property

    def total_output(self) -> int:

        """Total compressed output bytes (v3 only; 0 for v2)."""

        return self._total_output



    @property

    def estimated_input_bytes(self) -> int:

        """Nominal input size (chunk_bytes ├ù total_chunks)."""

        return self._chunk_bytes * self._total_chunks



    @property

    def is_v3(self) -> bool:

        return self._version >= HEAT_VERSION_V3



    @property

    def chunk_array(self) -> list[float]:

        """Lazy mmap read of all per-chunk values.



        v3: compression_ratio per chunk (output/input bytes).

        v2: Shannon entropy per chunk.

        """

        if self._total_chunks == 0:

            return []

        mm = self._ensure_mmap()

        count = self._total_chunks

        data = mm[self._data_offset:self._data_offset + count * 4]

        return [

            _CHUNK_FMT.unpack(data[i:i + 4])[0]

            for i in range(0, len(data), 4)

        ]



    @property

    def ratio_array(self) -> list[float]:

        """Per-chunk compression ratios.



        v3: directly from file.  v2: approximated from entropy (entropy/8).

        """

        if self.is_v3:

            return self.chunk_array

        # Approximate: lower entropy ΓåÆ better compression

        return [min(1.0, max(0.01, e / 8.0)) for e in self.chunk_array]



    @property

    def entropy_array(self) -> list[float]:

        """Per-chunk Shannon entropy values.



        v3: not stored (returns empty).  v2: directly from file.

        """

        if self.is_v3:

            return []

        return self.chunk_array



    @property

    def macro_data(self) -> dict:

        """Return macro data payload compatible with ThreeTierHeatmapDialog."""

        name = Path(self._heat_path).stem

        ratios = self.ratio_array

        input_bytes = self._total_input if self.is_v3 else self.estimated_input_bytes

        return {

            "total_input_bytes": input_bytes,

            "chunk_bytes": self._chunk_bytes,

            "total_chunks": self._total_chunks,

            "version": self._version,

            "is_v3": self.is_v3,

            "files": [{

                "name": name,

                "global_start": 0,

                "global_end": input_bytes,

                "input_bytes": input_bytes,

                "chunk_count": self._total_chunks,

                "entropy_array": self.entropy_array,

                "ratio_array": ratios,

                "is_v3": self.is_v3,

            }],

        }



    # ΓöÇΓöÇ Micro API (mmap original file) ΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇ



    @staticmethod

    def slice_byte_heatmap(filepath: str, offset: int, size: int) -> dict:

        """mmap *filepath* at [offset, offset+size), return per-byte heat array.



        Returns a dict with:

          - ``bytes``: list of int (raw byte values 0-255)

          - ``offset``: actual start offset

          - ``size``: actual byte count returned

          - ``entropy_64k``: Shannon entropy of this slice (or 0.0 on empty)

        """

        path = Path(filepath)

        if not path.is_file():

            raise FileNotFoundError(f"File not found: {filepath}")



        file_size = path.stat().st_size

        actual_offset = max(0, min(offset, file_size))

        actual_size = max(0, min(size, file_size - actual_offset))



        if actual_size == 0:

            return {"bytes": [], "offset": actual_offset, "size": 0, "entropy_64k": 0.0}



        f: BinaryIO | None = None

        mm: mmap.mmap | None = None

        try:

            f = open(filepath, "rb")

            mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)



            raw = mm[actual_offset:actual_offset + actual_size]

            byte_values = list(raw)



            counts = [0] * 256

            for b in byte_values:

                counts[b] += 1

            entropy = 0.0

            inv = 1.0 / actual_size

            for c in counts:

                if c > 0:

                    p = c * inv

                    entropy -= p * math.log2(p)



            return {

                "bytes": byte_values,

                "offset": actual_offset,

                "size": actual_size,

                "entropy_64k": round(entropy, 4),

            }

        finally:

            if mm is not None:

                mm.close()

            if f is not None:

                f.close()

