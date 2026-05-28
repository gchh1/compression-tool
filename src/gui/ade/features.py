"""ADE Base + Extension Segment feature extraction.

Pure-Python implementation:
- BaseSegment (20-dim, mirrors C++ ``BaseFeatureExtractor``)
- ExtensionSegments (5-8 dims, type-specific)

Extension segments per file type:
  TEXT/HTML/CSS/JS/JSON/XML → TextCode (5 dims)
  IMAGE                      → Image     (8 dims)
  AUDIO                      → Audio     (6 dims)
  VIDEO                      → Video     (7 dims)
  COMPRESSED                 → Archive   (4 dims)
  BINARY                     → Binary    (5 dims)
"""

from __future__ import annotations

import logging
import math
import struct
from collections import Counter
from dataclasses import dataclass, field
from enum import Enum, auto
from pathlib import Path

logger = logging.getLogger(__name__)

FEATURE_DIM = 20
LOCAL_ENTROPY_WINDOW = 1024
BLOCK_SIZE = 4096


class FileType(Enum):
    UNKNOWN = auto()
    TEXT = auto()
    HTML = auto()
    CSS = auto()
    JAVASCRIPT = auto()
    JSON = auto()
    XML = auto()
    IMAGE = auto()
    AUDIO = auto()
    VIDEO = auto()
    BINARY = auto()
    COMPRESSED = auto()
    PDF = auto()
    OFFICE = auto()


EXTENSION_DIMS: dict[FileType, int] = {
    FileType.TEXT: 5,
    FileType.HTML: 5,
    FileType.CSS: 5,
    FileType.JAVASCRIPT: 5,
    FileType.JSON: 5,
    FileType.XML: 5,
    FileType.IMAGE: 8,
    FileType.AUDIO: 6,
    FileType.VIDEO: 7,
    FileType.COMPRESSED: 4,
    FileType.BINARY: 5,
    FileType.PDF: 4,
    FileType.OFFICE: 4,
    FileType.UNKNOWN: 0,
}


def get_extension_dim(file_type: FileType) -> int:
    return EXTENSION_DIMS.get(file_type, 0)


TEXT_LIKE_TYPES = frozenset({
    FileType.TEXT, FileType.HTML, FileType.CSS,
    FileType.JAVASCRIPT, FileType.JSON, FileType.XML,
})


def is_extension_text(file_type: FileType) -> bool:
    return file_type in TEXT_LIKE_TYPES


MAGIC_SIGNATURES: list[tuple[bytes, FileType, float]] = [
    (b"\xff\xd8\xff", FileType.IMAGE, 0.95),
    (b"\x89PNG\r\n\x1a\n", FileType.IMAGE, 0.95),
    (b"GIF8", FileType.IMAGE, 0.90),
    (b"BM", FileType.IMAGE, 0.80),
    (b"RIFF", FileType.AUDIO, 0.80),
    (b"fLaC", FileType.AUDIO, 0.95),
    (b"\xff\xfb", FileType.AUDIO, 0.85),
    (b"\xff\xf3", FileType.AUDIO, 0.85),
    (b"\xff\xf2", FileType.AUDIO, 0.85),
    (b"ID3", FileType.AUDIO, 0.90),
    (b"\x00\x00\x01", FileType.VIDEO, 0.70),
    (b"\x00\x00\x00\x18ftyp", FileType.VIDEO, 0.90),
    (b"PK\x03\x04", FileType.COMPRESSED, 0.95),
    (b"\x1f\x8b\x08", FileType.COMPRESSED, 0.95),
    (b"%PDF-", FileType.PDF, 0.95),
    (b"\xd0\xcf\x11\xe0", FileType.OFFICE, 0.90),
    (b"<!DOCTYPE html", FileType.HTML, 0.90),
    (b"<html", FileType.HTML, 0.85),
    (b"<?xml", FileType.XML, 0.85),
    (b"\x7b", FileType.JSON, 0.60),
]

# File extension → FileType mappings for supplementary detection
EXTENSION_FILE_TYPES: dict[str, FileType] = {
    ".txt": FileType.TEXT, ".md": FileType.TEXT, ".rst": FileType.TEXT,
    ".py": FileType.TEXT, ".js": FileType.JAVASCRIPT, ".ts": FileType.JAVASCRIPT,
    ".jsx": FileType.JAVASCRIPT, ".tsx": FileType.JAVASCRIPT,
    ".html": FileType.HTML, ".htm": FileType.HTML,
    ".css": FileType.CSS, ".scss": FileType.CSS, ".less": FileType.CSS,
    ".json": FileType.JSON, ".xml": FileType.XML, ".svg": FileType.XML,
    ".jpg": FileType.IMAGE, ".jpeg": FileType.IMAGE, ".png": FileType.IMAGE,
    ".gif": FileType.IMAGE, ".bmp": FileType.IMAGE, ".webp": FileType.IMAGE,
    ".tiff": FileType.IMAGE, ".ico": FileType.IMAGE,
    ".wav": FileType.AUDIO, ".flac": FileType.AUDIO, ".mp3": FileType.AUDIO,
    ".aac": FileType.AUDIO, ".ogg": FileType.AUDIO, ".opus": FileType.AUDIO,
    ".wma": FileType.AUDIO, ".m4a": FileType.AUDIO,
    ".mp4": FileType.VIDEO, ".avi": FileType.VIDEO, ".mkv": FileType.VIDEO,
    ".mov": FileType.VIDEO, ".wmv": FileType.VIDEO, ".webm": FileType.VIDEO,
    ".flv": FileType.VIDEO,
    ".zip": FileType.COMPRESSED, ".rar": FileType.COMPRESSED,
    ".7z": FileType.COMPRESSED, ".gz": FileType.COMPRESSED,
    ".bz2": FileType.COMPRESSED, ".xz": FileType.COMPRESSED,
    ".tar": FileType.COMPRESSED,
    ".pdf": FileType.PDF,
    ".doc": FileType.OFFICE, ".docx": FileType.OFFICE,
    ".xls": FileType.OFFICE, ".xlsx": FileType.OFFICE,
    ".ppt": FileType.OFFICE, ".pptx": FileType.OFFICE,
    ".exe": FileType.BINARY, ".dll": FileType.BINARY, ".so": FileType.BINARY,
    ".dylib": FileType.BINARY, ".bin": FileType.BINARY, ".dat": FileType.BINARY,
}


@dataclass
class BaseFeatures:
    vector: list[float] = field(default_factory=lambda: [0.0] * FEATURE_DIM)

    @property
    def shannon_entropy(self) -> float:
        return self.vector[3] * 8.0

    @property
    def unique_byte_ratio(self) -> float:
        return self.vector[5]

    @property
    def printable_ratio(self) -> float:
        return self.vector[2]

    @property
    def magic_confidence(self) -> float:
        return self.vector[1]

    def to_dict(self) -> dict[str, float]:
        keys = [
            "file_size_log2", "magic_confidence", "printable_ratio",
            "shannon_entropy", "min_entropy", "unique_byte_ratio",
            "mean_byte_norm", "std_byte_norm", "longest_run_log2",
            "zero_byte_ratio", "high_bit_ratio", "header_entropy",
            "local_entropy_var", "block_boundary_density",
            "skewness", "kurtosis",
            "unique_bigram_ratio", "bigram_topk_conc",
            "rle_potential", "dict_potential",
        ]
        return dict(zip(keys, self.vector))


def _detect_file_type(data: bytes) -> tuple[FileType, float]:
    if not data:
        return FileType.UNKNOWN, 0.0

    best_type = FileType.UNKNOWN
    best_conf = 0.0
    head = data[:64]

    for sig, ftype, conf in MAGIC_SIGNATURES:
        if head.startswith(sig):
            if conf > best_conf:
                best_type = ftype
                best_conf = conf

    if best_type == FileType.UNKNOWN and len(data) > 0:
        printable = sum(1 for b in data[:4096] if 32 <= b < 127 or b in (9, 10, 13))
        ratio = printable / min(len(data), 4096)
        if ratio > 0.85:
            best_type = FileType.TEXT
            best_conf = min(ratio, 0.90)
        else:
            best_type = FileType.BINARY
            best_conf = 0.50

    return best_type, best_conf


def _compute_histogram_entropy(histogram: list[int], total: int) -> float:
    if total == 0:
        return 0.0
    entropy = 0.0
    for count in histogram:
        if count > 0:
            p = count / total
            entropy -= p * math.log2(p)
    return entropy


def _compute_bigrams(raw: bytes) -> Counter:
    bigrams: Counter = Counter()
    for i in range(len(raw) - 1):
        bigram = (raw[i] << 8) | raw[i + 1]
        bigrams[bigram] += 1
    return bigrams


def extract_base_features_fast(raw_data: bytes) -> tuple[BaseFeatures, FileType]:
    size = len(raw_data)
    if size == 0:
        return BaseFeatures(), FileType.UNKNOWN

    result = BaseFeatures()
    v = result.vector

    file_type, magic_conf = _detect_file_type(raw_data)

    v[0] = math.log2(size + 1.0) / 32.0
    v[1] = magic_conf

    histogram = [0] * 256
    printable = 0
    zero_count = 0
    high_bit_count = 0
    longest_run = 0
    current_byte = raw_data[0] if size > 0 else 0
    current_run = 0

    sum_val = 0.0
    sum_sq = 0.0
    sum_cu = 0.0
    sum_qu = 0.0

    window_hist = [0] * 256
    window_pos = 0
    local_entropies: list[float] = []

    for i, b in enumerate(raw_data):
        val = float(b)
        sum_val += val
        sum_sq += val * val
        sum_cu += val * val * val
        sum_qu += val * val * val * val

        histogram[b] += 1
        window_hist[b] += 1
        window_pos += 1

        if 32 <= b < 127 or b in (9, 10, 13):
            printable += 1
        if b == 0:
            zero_count += 1
        if b >= 128:
            high_bit_count += 1

        if b == current_byte:
            current_run += 1
        else:
            if current_run > longest_run:
                longest_run = current_run
            current_byte = b
            current_run = 1

        if window_pos >= LOCAL_ENTROPY_WINDOW:
            ent = _compute_histogram_entropy(window_hist, window_pos)
            local_entropies.append(ent)
            window_hist = [0] * 256
            window_pos = 0

    if current_run > longest_run:
        longest_run = current_run

    if window_pos > 0:
        ent = _compute_histogram_entropy(window_hist, window_pos)
        local_entropies.append(ent)

    n = float(size)
    v[2] = printable / n
    v[3] = _compute_histogram_entropy(histogram, size) / 8.0

    max_count = max(histogram) if histogram else 1
    v[4] = -math.log2(max_count / n) / 8.0 if max_count > 0 else 0.0
    v[5] = sum(1 for c in histogram if c > 0) / 256.0

    mean = sum_val / n
    v[6] = mean / 255.0

    variance = (sum_sq / n) - (mean * mean)
    std = math.sqrt(max(variance, 0.0))
    v[7] = std / 115.0

    v[8] = math.log2(longest_run + 1.0) / 20.0
    v[9] = zero_count / n
    v[10] = high_bit_count / n

    header_end = min(size, 1024)
    header_hist = [0] * 256
    for i in range(header_end):
        header_hist[raw_data[i]] += 1
    v[11] = _compute_histogram_entropy(header_hist, header_end) / 8.0

    if len(local_entropies) > 1:
        le_mean = sum(local_entropies) / len(local_entropies)
        le_var = sum((x - le_mean) ** 2 for x in local_entropies) / len(local_entropies)
        v[12] = min(le_var / 8.0, 1.0)
    else:
        v[12] = 0.0

    block_count = max(1, (size + BLOCK_SIZE - 1) // BLOCK_SIZE)
    zero_run_boundaries = 0
    for i in range(1, size):
        if raw_data[i] == 0 and raw_data[i - 1] != 0:
            zero_run_boundaries += 1
    v[13] = min(zero_run_boundaries / block_count, 1.0)

    if std > 0:
        skew = (sum_cu / n - 3.0 * mean * variance - mean ** 3) / (std ** 3) if std > 0.001 else 0.0
        v[14] = max(-1.0, min(1.0, skew / 2.0))
        kurt = ((sum_qu / n - 4.0 * mean * (sum_cu / n) + 6.0 * mean * mean * variance + 3.0 * mean ** 4)
                / (variance ** 2) - 3.0) if variance > 0.001 else 0.0
        v[15] = max(-2.0, min(2.0, kurt / 10.0))
    else:
        v[14] = 0.0
        v[15] = 0.0

    if size >= 2:
        bigrams = _compute_bigrams(raw_data)
        total_bigrams = size - 1
        unique_ratio = len(bigrams) / min(total_bigrams, 65536)
        v[16] = unique_ratio

        sorted_bigrams = bigrams.most_common(10)
        topk_sum = sum(c for _, c in sorted_bigrams)
        v[17] = topk_sum / total_bigrams if total_bigrams > 0 else 0.0
    else:
        v[16] = 0.0
        v[17] = 0.0

    avg_run = longest_run / n if n > 0 else 0.0
    v[18] = 1.0 - min(avg_run * 100.0, 1.0)
    v[19] = 1.0 - v[17]

    return result, file_type


def get_compression_decision(features: BaseFeatures) -> str:
    v = features.vector
    entropy = v[3] * 8.0
    printable = v[2]

    if entropy < 0.5:
        return "DEFLATE"
    if printable > 0.85:
        if v[12] > 0.3:
            return "DPFLATE"
        return "DEFLATE"
    if entropy > 7.5:
        return "ZSTD"
    if features.magic_confidence > 0.8:
        if v[17] > 0.3:
            return "LZDP"
        return "BROTLI"
    return "DEFLATE"


# ── Extension Segment Feature Extractors ──────────────────────────


def _parse_png_ihdr(data: bytes) -> dict:
    """Extract width, height, bit_depth, color_type from PNG IHDR."""
    info = {}
    if len(data) < 37:
        return info
    if data[1:4] != b"PNG":
        return info
    ihdr_start = data.find(b"IHDR", 8)
    if ihdr_start == -1 or ihdr_start + 16 > len(data):
        return info
    chunk = data[ihdr_start + 4:ihdr_start + 16]
    if len(chunk) < 12:
        return info
    info["width"] = struct.unpack(">I", chunk[0:4])[0]
    info["height"] = struct.unpack(">I", chunk[4:8])[0]
    info["bit_depth"] = chunk[8]
    info["color_type"] = chunk[9]
    return info


def _parse_jpeg_sof(data: bytes) -> dict:
    """Extract width, height from JPEG SOF0/SOF1 marker."""
    info = {}
    for marker in (b"\xc0", b"\xc1", b"\xc2"):
        pos = data.find(b"\xff" + marker)
        if pos != -1 and pos + 9 < len(data):
            info["height"] = (data[pos + 5] << 8) | data[pos + 6]
            info["width"] = (data[pos + 7] << 8) | data[pos + 8]
            break
    return info


def _parse_bmp_header(data: bytes) -> dict:
    """Extract width, height, bit_depth from BMP header."""
    info = {}
    if len(data) < 30:
        return info
    if data[0:2] != b"BM":
        return info
    info["width"] = struct.unpack("<I", data[18:22])[0]
    info["height"] = struct.unpack("<I", data[22:26])[0]
    info["bit_depth"] = struct.unpack("<H", data[28:30])[0]
    return info


def _parse_wav_header(data: bytes) -> dict:
    """Extract sample_rate, bit_depth, channels, duration from WAV fmt chunk."""
    info = {}
    if len(data) < 44 or data[0:4] != b"RIFF":
        return info
    fmt_pos = data.find(b"fmt ", 12)
    if fmt_pos == -1 or fmt_pos + 24 > len(data):
        return info
    fmt_data = data[fmt_pos + 8:]
    if len(fmt_data) < 16:
        return info
    channels = struct.unpack_from("<H", fmt_data, 2)[0]
    sample_rate = struct.unpack_from("<I", fmt_data, 4)[0]
    bit_depth = struct.unpack_from("<H", fmt_data, 14)[0]
    info["channels"] = channels
    info["sample_rate"] = sample_rate
    info["bit_depth"] = bit_depth
    data_size = len(data) - 44
    if sample_rate > 0 and channels > 0 and bit_depth > 0:
        seconds = data_size / (sample_rate * channels * (bit_depth // 8))
        info["duration_sec"] = seconds
    return info


def _parse_flac_metadata(data: bytes) -> dict:
    """Extract sample_rate, bit_depth, channels from FLAC STREAMINFO."""
    info = {}
    if len(data) < 42 or data[0:4] != b"fLaC":
        return info
    streaminfo = data[8:42]
    if len(streaminfo) < 34:
        return info
    info["sample_rate"] = (streaminfo[14] << 12) | (streaminfo[15] << 4) | (streaminfo[16] >> 4)
    channels = ((streaminfo[13] >> 1) & 0x07) + 1
    info["channels"] = channels
    bit_depth = ((streaminfo[13] & 0x01) << 4) | (streaminfo[14] >> 4)
    info["bit_depth"] = bit_depth + 1
    samples = (streaminfo[17] << 24) | (streaminfo[18] << 16) | (streaminfo[19] << 8) | streaminfo[20]
    if info["sample_rate"] > 0:
        info["duration_sec"] = samples / info.get("sample_rate", 44100)
    return info


def _estimate_compression_savings(entropy: float, size: int) -> float:
    """Estimate potential space saving ratio from Shannon entropy."""
    if entropy < 6.0 and size > 1000:
        return min(0.9, (8.0 - entropy) / 8.0 + 0.2)
    if entropy < 7.0:
        return max(0.0, (8.0 - entropy) / 8.0)
    return 0.0


# ── Per-type Extension Extractors ──


def _extract_textcode_extension(data: bytes) -> list[float]:
    if len(data) == 0:
        return [0.5, 0.0, 0.0, 0.0, 0.0]

    text = data[:min(len(data), 65536)]
    total = len(text)

    syntax_chars = set(b"{}();[]<>")
    syntax_count = sum(1 for b in text if b in syntax_chars)
    syntax_density = syntax_count / total if total > 0 else 0.0

    lf_count = text.count(b"\n")
    crlf_count = text.count(b"\r\n")
    if crlf_count > 0 and lf_count == crlf_count:
        line_ending = 1.0
    elif lf_count > 0 and crlf_count == 0:
        line_ending = 0.0
    elif lf_count > 0:
        line_ending = 0.5
    else:
        line_ending = 0.0

    space_indent = 0
    tab_indent = 0
    lines = text.split(b"\n")[:1000]
    for line in lines:
        stripped = line.lstrip()
        if not stripped:
            continue
        leading = line[:len(line) - len(stripped)]
        leading_str = leading.decode("ascii", errors="replace")
        if leading_str.startswith("\t"):
            tab_indent += 1
        elif leading_str.startswith(" "):
            space_indent += 1
    total_indent = space_indent + tab_indent
    if total_indent > 0:
        indentation = tab_indent / total_indent
    else:
        indentation = 0.0

    comment_symbols = [b"//", b"#", b"/*", b"<!--"]
    comment_lines = 0
    for line in lines:
        stripped = line.strip()
        for sym in comment_symbols:
            if stripped.startswith(sym):
                comment_lines += 1
                break
    comment_ratio = comment_lines / len(lines) if lines else 0.0

    total_tokens = syntax_count + sum(1 for b in text if b == 0x20 or b == 0x09)
    avg_word_chars = 0.0
    if text and total_tokens > 0:
        runs = [len(s) for s in text.split(b" ") if s]
        avg_word_chars = sum(runs) / len(runs) / 20.0 if runs else 0.0

    language_score = max(0.0, min(1.0, 1.0 - (syntax_density * 2.0 + avg_word_chars * 0.5) / 2.5))

    return [
        language_score,
        min(1.0, syntax_density * 5.0),
        line_ending,
        indentation,
        min(0.5, comment_ratio),
    ]


def _extract_image_extension(data: bytes, file_type: FileType, path_hint: str = "") -> list[float]:
    features = [0.0] * 8
    if len(data) < 16:
        return features

    width = 0.0
    height = 0.0
    bit_depth = 8.0
    has_alpha = 0.0
    color_mode = 1.0
    is_lossless = 1.0
    jpeg_quality = 0.0

    magic = data[:16]

    if magic[:3] == b"\xff\xd8\xff":
        is_lossless = 0.0
        jpeg = _parse_jpeg_sof(data)
        if jpeg:
            width = float(jpeg.get("width", 0))
            height = float(jpeg.get("height", 0))
        if width > 0 and height > 0:
            compressed_bpp = (len(data) * 8.0) / (width * height) if width * height > 0 else 24
            if compressed_bpp < 1.0:
                jpeg_quality = 10.0
            elif compressed_bpp < 2.0:
                jpeg_quality = 40.0
            elif compressed_bpp < 4.0:
                jpeg_quality = 70.0
            elif compressed_bpp < 8.0:
                jpeg_quality = 85.0
            else:
                jpeg_quality = 95.0
        else:
            jpeg_quality = 75.0
        bit_depth = 24.0
        color_mode = 1.0

    elif magic[1:4] == b"PNG":
        png = _parse_png_ihdr(data)
        if png:
            width = float(png.get("width", 0))
            height = float(png.get("height", 0))
            bit_depth = float(png.get("bit_depth", 8))
            color_type = png.get("color_type", 2)
            has_alpha = 1.0 if color_type in (4, 6) else 0.0
            color_mode_map = {0: 0.0, 2: 1.0, 4: 2.0, 6: 2.0, 3: 4.0}
            color_mode = color_mode_map.get(color_type, 1.0)
        is_lossless = 1.0

    elif magic[:2] == b"BM":
        bmp = _parse_bmp_header(data)
        if bmp:
            width = float(bmp.get("width", 0))
            height = float(bmp.get("height", 0))
            bit_depth = float(bmp.get("bit_depth", 24))
        is_lossless = 1.0
        color_mode = 1.0

    elif magic[:3] == b"GIF":
        is_lossless = 0.0
        color_mode = 4.0

    else:
        ext = Path(path_hint).suffix.lower() if path_hint else ""
        if ext in (".webp",):
            is_lossless = 0.0
        elif ext in (".ico", ".tiff"):
            is_lossless = 1.0

    features[0] = min(width / 4096.0, 1.0)
    features[1] = min(height / 4096.0, 1.0)
    features[2] = bit_depth
    features[3] = has_alpha
    features[4] = min(color_mode / 4.0, 1.0)
    features[5] = is_lossless
    features[6] = jpeg_quality / 100.0
    savings = 0.0
    if is_lossless > 0.5 and width > 0 and height > 0:
        raw_bpp = bit_depth if bit_depth > 0 else 24.0
        unpacked_bytes = (width * height * raw_bpp) / 8.0
        if unpacked_bytes > 0:
            savings = (unpacked_bytes - len(data)) / unpacked_bytes
    features[7] = max(0.0, min(1.0, savings))

    return features


def _extract_audio_extension(data: bytes) -> list[float]:
    features = [0.0] * 6
    if len(data) < 16:
        return features

    sample_rate = 44100.0
    bit_depth = 16.0
    channels = 2.0
    duration_sec = 0.0
    is_lossless = 0.0
    bitrate = 192.0

    if len(data) > 4:
        if data[0:4] == b"RIFF" and data[8:12] == b"WAVE":
            wav = _parse_wav_header(data)
            if wav:
                sample_rate = float(wav.get("sample_rate", 44100))
                bit_depth = float(wav.get("bit_depth", 16))
                channels = float(wav.get("channels", 2))
                duration_sec = float(wav.get("duration_sec", 0))
            is_lossless = 1.0

        elif data[0:4] == b"fLaC":
            flac = _parse_flac_metadata(data)
            if flac:
                sample_rate = float(flac.get("sample_rate", 44100))
                bit_depth = float(flac.get("bit_depth", 16))
                channels = float(flac.get("channels", 2))
                duration_sec = float(flac.get("duration_sec", 0))
            is_lossless = 1.0

        elif data[0:3] == b"ID3":
            is_lossless = 0.0

    if duration_sec > 0:
        bitrate = (len(data) * 8.0) / duration_sec / 1000.0

    features[0] = min(sample_rate / 96000.0, 1.0)
    features[1] = min(bit_depth / 32.0, 1.0) * 32.0
    features[2] = min(channels / 8.0, 1.0)
    features[3] = min(duration_sec / 3600.0, 1.0)
    features[4] = is_lossless
    features[5] = min(bitrate / 1000.0, 1.0)

    return features


def _extract_video_extension(data: bytes, path_hint: str = "") -> list[float]:
    features = [0.0] * 7
    if len(data) < 32:
        return features

    width = 0.0
    height = 0.0
    fps = 0.0
    duration_sec = 0.0
    codec = 0.0
    is_lossless = 0.0

    if data[4:8] == b"ftyp":
        ftyp = data[8:12]
        codec_map = {b"avc1": 0.0, b"isom": 0.0, b"mp42": 0.0}
        codec = codec_map.get(ftyp, 1.0) if ftyp else 0.0
        moov_pos = data.find(b"moov")
        if moov_pos != -1:
            tkhd_pos = data.find(b"tkhd", moov_pos)
            if tkhd_pos != -1 and tkhd_pos + 84 < len(data):
                matrix = data[tkhd_pos + 52:tkhd_pos + 68]
                if len(matrix) == 16:
                    w_raw = struct.unpack(">I", matrix[0:4])[0]
                    h_raw = struct.unpack(">I", matrix[8:12])[0]
                    width = float(w_raw >> 16) if w_raw > 0 else 0.0
                    height = float(h_raw >> 16) if h_raw > 0 else 0.0

    elif data[:3] == b"\x00\x00\x01":
        is_lossless = 0.0

    ext = Path(path_hint).suffix.lower() if path_hint else ""
    if ext in (".avi", ".mkv"):
        is_lossless = 0.0

    features[0] = min(width / 3840.0, 1.0)
    features[1] = min(height / 2160.0, 1.0)
    features[2] = min(fps / 120.0, 1.0)
    features[3] = min(duration_sec / 7200.0, 1.0)
    features[4] = min(codec / 5.0, 1.0)
    features[5] = is_lossless
    space_entropy = _compute_histogram_entropy([list(data[:4096]).count(b) for b in range(256)], min(len(data), 4096))
    savings = _estimate_compression_savings(space_entropy, len(data))
    features[6] = savings

    return features


def _extract_archive_extension(data: bytes) -> list[float]:
    features = [0.0] * 4
    if len(data) < 4:
        return features

    inner = 0.0
    if data[:2] == b"PK":
        inner = 0.0
    elif data[:2] == b"Rar":
        inner = 1.0
    elif data[:3] == b"7z\xbc":
        inner = 2.0
    elif data[:2] == b"\x1f\x9d":
        inner = 3.0
    elif len(data) >= 3 and data[0:3] in (b"\x1f\x8b\x08", b"BZh"):
        inner = 4.0
    elif data[:5] == b"\xfd7zXZ":
        inner = 5.0
    else:
        inner = 6.0

    entropy = _compute_histogram_entropy([list(data[:4096]).count(b) for b in range(256)], min(len(data), 4096))
    compactness = entropy / 8.0 if entropy > 0 else 0.0
    current_ratio = 1.0 - compactness * 0.5

    file_count_est = 0.0
    features[0] = inner / 10.0
    features[1] = min(max(current_ratio, 0.0), 1.0)
    features[2] = file_count_est
    potential = max(0.0, (compactness - 0.4) / 0.6) if compactness > 0.3 else 0.0
    features[3] = min(potential, 1.0)

    return features


def _extract_binary_extension(data: bytes) -> list[float]:
    features = [0.0] * 5
    if len(data) < 64:
        return features

    total = len(data)
    head = data[:min(len(data), 4096)]

    section_starts = 0
    zero_padding = 0
    for i in range(1, min(len(head), 4096)):
        if head[i - 1] == 0 and head[i] != 0:
            section_starts += 1
        if head[i] == 0 and head[i - 1] == 0:
            zero_padding += 1
    structure_density = min(section_starts / 50.0, 1.0)

    zero_bytes = sum(1 for b in data[:min(len(data), 65536)] if b == 0)
    padding_ratio = zero_bytes / min(len(data), 65536)

    align_hits = 0
    for offset in (512, 1024, 2048, 4096):
        if offset < len(data) and data[offset: offset + 1] == b"\x00":
            align_hits += 1
    alignment = min(align_hits / 4.0, 1.0)

    endian_samples = data[:64]
    le_score = 0
    be_score = 0
    if len(endian_samples) >= 4:
        for i in range(0, len(endian_samples) - 4, 4):
            val_le = struct.unpack_from("<I", endian_samples, i)[0]
            val_be = struct.unpack_from(">I", endian_samples, i)[0]
            if val_le < 256 and val_le > 0:
                le_score += 1
            if val_be < 256 and val_be > 0:
                be_score += 1
    if le_score + be_score > 0:
        endianness = 0.0 if le_score >= be_score else 1.0
    else:
        endianness = 0.5

    exec_score = 0.0
    if data[:2] == b"MZ":
        exec_score = 0.9
    elif data[:4] == b"\x7fELF":
        exec_score = 0.95
    elif data[:4] == b"\xcf\xfa\xed\xfe" or data[:4] == b"\xce\xfa\xed\xfe":
        exec_score = 0.85
    else:
        instr_count = sum(1 for b in head if b in {0x55, 0x89, 0x83, 0x8b, 0x48, 0xe9, 0xeb, 0x74, 0x75, 0x90})
        exec_score = min(instr_count / 50.0, 0.7)

    features[0] = structure_density
    features[1] = min(padding_ratio * 5.0, 1.0)
    features[2] = alignment
    features[3] = endianness
    features[4] = exec_score

    return features


def extract_extension_features(
    raw_data: bytes,
    file_type: FileType,
    file_path: str = "",
) -> list[float]:
    ext_dims = get_extension_dim(file_type)
    if ext_dims == 0:
        return []

    if is_extension_text(file_type):
        return _extract_textcode_extension(raw_data)

    dispatch = {
        FileType.IMAGE: lambda: _extract_image_extension(raw_data, file_type, file_path),
        FileType.AUDIO: lambda: _extract_audio_extension(raw_data),
        FileType.VIDEO: lambda: _extract_video_extension(raw_data, file_path),
        FileType.COMPRESSED: lambda: _extract_archive_extension(raw_data),
        FileType.BINARY: lambda: _extract_binary_extension(raw_data),
        FileType.PDF: lambda: _extract_archive_extension(raw_data),
        FileType.OFFICE: lambda: _extract_archive_extension(raw_data),
    }

    extractor = dispatch.get(file_type)
    if extractor:
        result = extractor()
        return result[:ext_dims]

    return []


def pad_features(base_vector: list[float], extension_vector: list[float], target_dim: int = 33) -> list[float]:
    result = list(base_vector[:20])
    result.extend(extension_vector[:8])
    while len(result) < target_dim:
        result.append(0.0)
    return result[:target_dim]