"""
ADE Feature Extractor - Base Segment (20-dimensions)
Version: 3.0 Final (based on feature_vector_specification_v3_final.md)

Implements single-pass O(n) feature extraction with:
- Metadata features (3 dims): size, magic bytes confidence, printable ratio
- Statistical features (8 dims): entropy, min-entropy, unique ratio, mean, std, longest run, zero ratio, high bit ratio  
- Structural features (5 dims): header entropy, local entropy variance, block boundary density, skewness, kurtosis
- N-gram aggregate features (4 dims): bigram uniqueness, top-k concentration, RLE potential, dict potential

Memory usage: ~130KB working memory (Count-Min Sketch + statistics)
Output size: 80 bytes (20 × float32)
"""

from __future__ import annotations

import math
import struct
import logging
import heapq
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional

logger = logging.getLogger(__name__)


class FileType(IntEnum):
    """File type detection results from Magic Bytes analysis"""
    UNKNOWN = 0
    TEXT_PLAIN = 1
    TEXT_UTF8 = 2
    SOURCE_C = 10
    SOURCE_CPP = 11
    SOURCE_PYTHON = 12
    SOURCE_JAVA = 13
    SOURCE_RUST = 14
    SOURCE_GO = 15
    MARKDOWN = 20
    HTML = 21
    XML = 22
    JSON = 23
    CSS = 24
    JAVASCRIPT = 25
    IMAGE_PNG = 30
    IMAGE_JPEG = 31
    IMAGE_GIF = 32
    IMAGE_BMP = 33
    IMAGE_TIFF = 34
    IMAGE_WEBP = 35
    IMAGE_AVIF = 36
    AUDIO_WAV = 40
    AUDIO_MP3 = 41
    AUDIO_FLAC = 42
    AUDIO_OGG = 43
    AUDIO_AAC = 44
    VIDEO_MP4 = 50
    VIDEO_MKV = 51
    VIDEO_AVI = 52
    VIDEO_FLV = 53
    VIDEO_WEBM = 54
    ARCHIVE_ZIP = 60
    ARCHIVE_GZIP = 61
    ARCHIVE_7Z = 62
    ARCHIVE_RAR = 63
    ARCHIVE_TAR = 64
    EXEC_PE = 70
    EXEC_ELF = 71
    EXEC_MACHO = 72
    DOC_PDF = 80
    DOC_DOCX = 81
    DOC_XLSX = 82
    DB_SQLITE = 90
    BINARY_GENERIC = 100
    ENCRYPTED = 101


@dataclass
class MagicPattern:
    """Magic byte pattern for file type detection"""
    pattern: bytes
    file_type: FileType
    confidence: float


MAGIC_DB: list[MagicPattern] = [
    MagicPattern(b'\x89PNG\r\n\x1a\n', FileType.IMAGE_PNG, 1.0),
    MagicPattern(b'\xff\xd8\xff', FileType.IMAGE_JPEG, 1.0),
    MagicPattern(b'GIF8', FileType.IMAGE_GIF, 1.0),
    MagicPattern(b'BM', FileType.IMAGE_BMP, 0.95),
    MagicPattern(b'RIFF', FileType.IMAGE_WEBP, 0.85),
    MagicPattern(b'RIFF', FileType.AUDIO_WAV, 0.9),
    MagicPattern(b'ID3', FileType.AUDIO_MP3, 0.95),
    MagicPattern(b'fLaC', FileType.AUDIO_FLAC, 0.95),
    MagicPattern(b'\x00\x00\x00\x18ftyp', FileType.VIDEO_MP4, 0.9),
    MagicPattern(b'\x1a\x45\xdf\xa3', FileType.VIDEO_MKV, 0.9),
    MagicPattern(b'PK\x03\x04', FileType.ARCHIVE_ZIP, 1.0),
    MagicPattern(b'\x1f\x8b', FileType.ARCHIVE_GZIP, 0.98),
    MagicPattern(b'7z\xbc\xaf\x27\x1c', FileType.ARCHIVE_7Z, 0.95),
    MagicPattern(b'MZ', FileType.EXEC_PE, 0.95),
    MagicPattern(b'\x7fELF', FileType.EXEC_ELF, 1.0),
    MagicPattern(b'%PDF', FileType.DOC_PDF, 0.95),
    MagicPattern(b'PK\x03\x04\x06\x00', FileType.DOC_DOCX, 0.9),
]


class CountMinSketch:
    """
    Count-Min Sketch for streaming N-gram frequency estimation
    
    Fixed memory usage: W * D * 4 bytes = 64KB
    - W (width): 4096
    - D (depth): 4
    Provides approximate uniqueness and concentration estimates
    """
    
    def __init__(self, width: int = 4096, depth: int = 4):
        self.W = width
        self.D = depth
        self.sketch: list[list[int]] = [[0] * width for _ in range(depth)]
        self.total_count: int = 0
        
    def _hash(self, value: int, d: int) -> int:
        """Hash function for dimension d"""
        return ((value * (d * 2654435761 + 1)) ^ (value >> 16)) % self.W
    
    def add(self, ngram: int) -> None:
        """Add an N-gram to the sketch"""
        self.total_count += 1
        for d in range(self.D):
            idx = self._hash(ngram, d)
            self.sketch[d][idx] += 1
            
    def estimate_count(self, ngram: int) -> int:
        """Estimate count of a specific N-gram"""
        return min(self.sketch[d][self._hash(ngram, d)] for d in range(self.D))
    
    def estimate_uniqueness(self) -> float:
        """
        Estimate number of unique elements using linear counting method
        
        Returns ratio of unique/total possible
        Range: [0.0, 1.0]
        """
        if self.total_count == 0:
            return 0.0
            
        zero_cells = sum(
            1 for d in range(self.D) 
            for w in range(self.W) 
            if self.sketch[d][w] == 0
        )
        
        total_cells = self.D * self.W
        zero_ratio = zero_cells / total_cells
        
        if zero_ratio > 0:
            estimated_unique = -total_cells * math.log(zero_ratio)
        else:
            estimated_unique = total_cells
            
        max_possible = min(65536.0, float(self.total_count))
        return min(1.0, estimated_unique / max_possible) if max_possible > 0 else 0.0


@dataclass
class TopKTracker:
    """Track top-K most frequent items using a min-heap"""
    K: int = 10
    heap: list[tuple[int, int]] = field(default_factory=list)
    counts: dict[int, int] = field(default_factory=dict)
    total_items: int = 0
    
    def add(self, item: int) -> None:
        """Add an item to tracking"""
        self.total_items += 1
        self.counts[item] = self.counts.get(item, 0) + 1
        
        # Update or insert into heap
        found = False
        for i, (count, val) in enumerate(self.heap):
            if val == item:
                self.heap[i] = (self.counts[item], item)
                heapq.heapify(self.heap)
                found = True
                break
                
        if not found:
            if len(self.heap) < self.K:
                heapq.heappush(self.heap, (self.counts[item], item))
            elif self.counts[item] > self.heap[0][0]:
                heapq.heappop(self.heap)
                heapq.heappush(self.heap, (self.counts[item], item))
                
    def get_concentration(self) -> float:
        """
        Calculate concentration: sum(top-K counts) / total items
        
        Returns: [0.0, 1.0]
        Higher values indicate more repetitive content
        """
        if self.total_items == 0:
            return 0.0
        top_k_sum = sum(count for count, _ in self.heap)
        return top_k_sum / self.total_items


@dataclass
class BaseFeatures:
    """
    Base Segment feature vector (20 dimensions)
    
    All values are normalized floats suitable for ML input
    Total size: 80 bytes (20 × 4-byte float)
    """
    # Metadata features [0-2]
    file_size_log2: float = 0.0           # Base[0]: Normalized log2(file_size)
    magic_confidence: float = 0.0         # Base[1]: Magic bytes match confidence
    printable_ratio: float = 0.0          # Base[2]: Ratio of printable ASCII chars
    
    # Statistical features [3-10]
    shannon_entropy: float = 0.0          # Base[3]: Shannon entropy [0, 8]
    min_entropy: float = 0.0              # Base[4]: Min-entropy [0, 8]
    unique_byte_ratio: float = 0.0        # Base[5]: Unique byte ratio [0.0039, 1]
    mean_byte_normalized: float = 0.0     # Base[6]: Mean byte value / 255
    std_byte_normalized: float = 0.0      # Base[7]: Std dev / 115
    longest_run_log2: float = 0.0         # Base[8]: Longest run of same byte
    zero_byte_ratio: float = 0.0          # Base[9]: Zero byte proportion
    high_bit_ratio: float = 0.0           # Base[10]: Bytes >= 128 proportion
    
    # Structural features [11-15]
    header_entropy: float = 0.0           # Base[11]: Entropy of first 1024 bytes
    local_entropy_variance: float = 0.0   # Base[12]: Variance of local entropies
    block_boundary_density: float = 0.0   # Base[13]: Block boundary indicator
    skewness: float = 0.0                 # Base[14]: Distribution skewness [-1, 1]
    kurtosis: float = 0.0                 # Base[15]: Excess kurtosis
    
    # N-gram aggregate features [16-19]
    unique_bigram_ratio: float = 0.0      # Base[16]: Bigram uniqueness estimate
    bigram_topk_concentration: float = 0.0# Base[17]: Top-10 bigram concentration
    rle_potential: float = 0.0            # Base[18]: RLE compressibility [0, 1]
    dict_potential: float = 0.0           # Base[19]: Dictionary/LZ77 potential [0, 1]
    
    @property
    def vector(self) -> list[float]:
        """Return as flat list for ML input"""
        return [
            self.file_size_log2,
            self.magic_confidence,
            self.printable_ratio,
            self.shannon_entropy,
            self.min_entropy,
            self.unique_byte_ratio,
            self.mean_byte_normalized,
            self.std_byte_normalized,
            self.longest_run_log2,
            self.zero_byte_ratio,
            self.high_bit_ratio,
            self.header_entropy,
            self.local_entropy_variance,
            self.block_boundary_density,
            self.skewness,
            self.kurtosis,
            self.unique_bigram_ratio,
            self.bigram_topk_concentration,
            self.rle_potential,
            self.dict_potential,
        ]
    
    def to_dict(self) -> dict[str, float]:
        """Return as dictionary for JSON serialization"""
        names = [
            'file_size_log2', 'magic_confidence', 'printable_ratio',
            'shannon_entropy', 'min_entropy', 'unique_byte_ratio',
            'mean_byte_normalized', 'std_byte_normalized', 'longest_run_log2',
            'zero_byte_ratio', 'high_bit_ratio', 'header_entropy',
            'local_entropy_variance', 'block_boundary_density', 'skewness',
            'kurtosis', 'unique_bigram_ratio', 'bigram_topk_concentration',
            'rle_potential', 'dict_potential'
        ]
        return {name: val for name, val in zip(names, self.vector)}


class FeatureExtractor:
    """
    Single-pass O(n) feature extractor for ADE
    
    Implements all 20 Base Segment dimensions in one scan through data
    Memory footprint: ~130KB (statistics + CountMinSketch + TopKTracker)
    
    Usage:
        extractor = FeatureExtractor()
        features = extractor.extract(data_bytes)
        print(features.vector)  # List of 20 floats
        print(features.to_dict())  # Dict with named features
    """
    
    HEADER_SIZE = 1024
    LOCAL_WINDOW_SIZE = 1024
    BLOCK_SIZE = 1024
    ZERO_RUN_THRESHOLD = 8
    
    def __init__(self):
        self._reset_state()
        
    def _reset_state(self) -> None:
        """Reset all internal state for new extraction"""
        # Byte frequency counters (256 bins)
        self._byte_counts: list[int] = [0] * 256
        self._total_bytes: int = 0
        
        # Running statistics (Welford's algorithm)
        self._mean: float = 0.0
        self._m2: float = 0.0  # Sum of squares of differences from mean
        self._max_count: int = 0
        self._max_byte: int = 0
        
        # Run-length encoding stats
        self._current_run_length: int = 0
        self._current_run_byte: int = -1
        self._longest_run: int = 0
        
        # Header bytes storage
        self._header_bytes: bytearray = bytearray()
        
        # Local entropy windows
        self._local_entropies: list[float] = []
        self._window_counts: list[int] = [0] * 256
        self._window_position: int = 0
        
        # Block boundary detection
        self._total_blocks: int = 0
        self._boundary_blocks: int = 0
        self._block_zero_run: int = 0
        
        # Bit-level counters
        self._printable_count: int = 0
        self._zero_count: int = 0
        self._high_bit_count: int = 0
        
        # N-gram trackers
        self._bigram_sketch = CountMinSketch()
        self._bigram_tracker = TopKTracker(K=10)
        self._prev_byte: int = -1
        
        # RLE simulation
        self._rle_output_size: int = 0
        self._dict_matches: int = 0
        
        # Moments for skewness/kurtosis
        self._m3: float = 0.0  # Third central moment
        self._m4: float = 0.0  # Fourth central moment
        
    def extract(self, data: bytes) -> BaseFeatures:
        """
        Extract all 20 base features from binary data in single pass
        
        Args:
            data: Raw file bytes to analyze
            
        Returns:
            BaseFeatures object containing all 20 normalized features
        """
        self._reset_state()
        size = len(data)
        
        if size == 0:
            return BaseFeatures()
            
        # Single pass through all bytes
        for i, byte_val in enumerate(data):
            self._process_byte(byte_val, i, size)
            
        # Post-processing: compute final features
        return self._compute_final_features(size)
        
    def _process_byte(self, byte_val: int, position: int, total_size: int) -> None:
        """Process a single byte during the main scan"""
        b = byte_val & 0xFF
        
        # Update basic counters
        self._byte_counts[b] += 1
        self._total_bytes += 1
        
        # Update printable/zero/high-bit counters
        if 0x20 <= b <= 0x7E or b in (0x09, 0x0A, 0x0D):
            self._printable_count += 1
        if b == 0:
            self._zero_count += 1
        if b >= 128:
            self._high_bit_count += 1
            
        # Update running mean/variance (Welford's algorithm)
        delta = b - self._mean
        self._mean += delta / self._total_bytes
        delta2 = b - self._mean
        self._m2 += delta * delta2
        
        # Update max count for min-entropy
        if self._byte_counts[b] > self._max_count:
            self._max_count = self._byte_counts[b]
            
        # Run-length tracking
        if b == self._current_run_byte:
            self._current_run_length += 1
        else:
            if self._current_run_length > self._longest_run:
                self._longest_run = self._current_run_length
            self._current_run_byte = b
            self._current_run_length = 1
            
        # Store header bytes
        if position < self.HEADER_SIZE:
            self._header_bytes.append(b)
            
        # Local entropy window
        self._window_counts[b] += 1
        self._window_position += 1
        
        if self._window_position >= self.LOCAL_WINDOW_SIZE:
            self._compute_local_entropy()
            self._window_counts = [0] * 256
            self._window_position = 0
            
        # Block boundary detection
        if position % self.BLOCK_SIZE == 0 and position > 0:
            self._total_blocks += 1
            if self._block_zero_run >= self.ZERO_RUN_THRESHOLD:
                self._boundary_blocks += 1
            self._block_zero_run = 0
        else:
            if b == 0:
                self._block_zero_run += 1
            else:
                self._block_zero_run = 0
                
        # N-gram processing (bigrams)
        if self._prev_byte >= 0:
            bigram = (self._prev_byte << 8) | b
            self._bigram_sketch.add(bigram)
            self._bigram_tracker.add(bigram)
            
        self._prev_byte = b
        
        # RLE simulation
        if self._current_run_length <= 127:
            self._rle_output_size += 2  # (count, byte) pair
        else:
            self._rle_output_size += 3  # Extended encoding
            
        # Moments for skewness/kurtosis
        delta3 = delta2
        self._m3 += delta3 * delta3 * (self._total_bytes - 1) / (self._total_bytes * self._total_bytes) - \
                   3 * delta * self._m2 / self._total_bytes
                    
    def _compute_local_entropy(self) -> None:
        """Compute and store entropy for current window"""
        window_total = sum(self._window_counts)
        if window_total == 0:
            return
            
        entropy = 0.0
        for count in self._window_counts:
            if count > 0:
                p = count / window_total
                entropy -= p * math.log2(p)
                
        self._local_entropies.append(entropy)
        
    def _detect_magic_bytes(self, data: bytes) -> tuple[FileType, float]:
        """
        Detect file type using magic byte patterns
        
        Returns:
            Tuple of (detected_type, confidence_score)
        """
        best_match: FileType = FileType.UNKNOWN
        best_confidence: float = 0.0
        
        for pattern in MAGIC_DB:
            if data.startswith(pattern.pattern):
                if pattern.confidence > best_confidence:
                    best_confidence = pattern.confidence
                    best_match = pattern.file_type
                    
        return best_match, best_confidence
        
    def _compute_final_features(self, size: int) -> BaseFeatures:
        """Compute final normalized features after complete scan"""
        features = BaseFeatures()
        
        # Handle last run
        if self._current_run_length > self._longest_run:
            self._longest_run = self._current_run_length
            
        # Compute last local window if partial
        if self._window_position > 0:
            self._compute_local_entropy()
            
        # ====== Base[0]: File size normalization ======
        features.file_size_log2 = math.log2(size + 1) / 32.0
        
        # ====== Base[1-2]: Will be set externally after this method ======
        # These require the original data for magic byte detection
        # Set defaults here, will be overridden by caller
        features.magic_confidence = 0.0
        features.printable_ratio = self._printable_count / size if size > 0 else 0.0
        
        # ====== Base[3]: Shannon Entropy ======
        features.shannon_entropy = 0.0
        for count in self._byte_counts:
            if count > 0:
                p = count / size
                features.shannon_entropy -= p * math.log2(p)
                
        # ====== Base[4]: Min-Entropy ======
        max_p = self._max_count / size if size > 0 else 0
        features.min_entropy = -math.log2(max_p) if max_p > 0 else 0.0
        
        # ====== Base[5]: Unique Byte Ratio ======
        unique_bytes = sum(1 for c in self._byte_counts if c > 0)
        features.unique_byte_ratio = unique_bytes / 256.0
        
        # ====== Base[6]: Mean Byte Normalized ======
        features.mean_byte_normalized = self._mean / 255.0
        
        # ====== Base[7]: Std Dev Normalized ======
        variance = self._m2 / size if size > 0 else 0
        std_dev = math.sqrt(variance)
        features.std_byte_normalized = std_dev / 115.0  # Normalize by theoretical max
        
        # ====== Base[8]: Longest Run Log2 ======
        features.longest_run_log2 = math.log2(self._longest_run + 1) / 20.0
        
        # ====== Base[9]: Zero Byte Ratio ======
        features.zero_byte_ratio = self._zero_count / size if size > 0 else 0.0
        
        # ====== Base[10]: High Bit Ratio ======
        features.high_bit_ratio = self._high_bit_count / size if size > 0 else 0.0
        
        # ====== Base[11]: Header Entropy ======
        if len(self._header_bytes) > 0:
            header_size = len(self._header_bytes)
            header_counts = [0] * 256
            for b in self._header_bytes:
                header_counts[b] += 1
                
            features.header_entropy = 0.0
            for count in header_counts:
                if count > 0:
                    p = count / header_size
                    features.header_entropy -= p * math.log2(p)
                    
        # ====== Base[12]: Local Entropy Variance ======
        if len(self._local_entropies) > 1:
            mean_le = sum(self._local_entropies) / len(self._local_entropies)
            features.local_entropy_variance = sum(
                (e - mean_le) ** 2 for e in self._local_entropies
            ) / len(self._local_entropies)
        else:
            features.local_entropy_variance = 0.0
            
        # ====== Base[13]: Block Boundary Density ======
        features.block_boundary_density = (
            self._boundary_blocks / self._total_blocks 
            if self._total_blocks > 0 else 0.0
        )
        
        # ====== Base[14-15]: Skewness and Kurtosis ======
        if variance > 0:
            # Standardize moments
            std_cubed = std_dev ** 3
            std_fourth = std_dev ** 4
            
            # Skewness (normalized to [-1, 1])
            raw_skewness = (self._m3 / size) / std_cubed if std_cubed > 0 else 0
            features.skewness = max(-1.0, min(1.0, raw_skewness / 3.0))  # Normalize
            
            # Kurtosis (excess kurtosis, normalize to reasonable range)
            raw_kurtosis = (self._m4 / size) / std_fourth - 3 if std_fourth > 0 else -3
            features.kurtosis = max(-2.0, min(10.0, raw_kurtosis))
        else:
            features.skewness = 0.0
            features.kurtosis = -3.0  # Uniform distribution limit
            
        # ====== Base[16]: Bigram Uniqueness Ratio ======
        features.unique_bigram_ratio = self._bigram_sketch.estimate_uniqueness()
        
        # ====== Base[17]: Bigram Top-K Concentration ======
        features.bigram_topk_concentration = self._bigram_tracker.get_concentration()
        
        # ====== Base[18]: RLE Potential ======
        features.rle_potential = 1.0 - min(1.0, self._rle_output_size / size) if size > 0 else 1.0
        
        # ====== Base[19]: Dictionary Potential (LZ77-style) =====#
        # Estimate based on bigram repetition patterns
        if size > 1:
            # Rough heuristic: high concentration = good dict potential
            features.dict_potential = 1.0 - features.bigram_topk_concentration
        else:
            features.dict_potential = 1.0
            
        return features


def extract_base_features(data: bytes) -> tuple[BaseFeatures, FileType]:
    """
    Convenience function: extract features and detect type in one call
    
    Args:
        data: Raw file bytes
        
    Returns:
        Tuple of (BaseFeatures, detected_FileType)
    """
    extractor = FeatureExtractor()
    features = extractor.extract(data)
    
    # Detect magic bytes
    file_type, confidence = extractor._detect_magic_bytes(data)
    features.magic_confidence = confidence
    
    return features, file_type


def get_compression_decision(features: BaseFeatures) -> str:
    """
    Make preliminary compression decision based on statistical features
    
    Uses the Entropy + Skewness + Kurtosis three-state detector:
    - High entropy + low |skewness| → Random/Encrypted → SKIP
    - High entropy + medium |skewness| → Already compressed → RECOMPRESS only
    - Medium entropy + negative skewness → Text/Code → COMPRESS efficiently
    
    Args:
        features: Extracted BaseFeatures
        
    Returns:
        Decision string: "COMPRESS", "SKIP", or "RECOMPRESS_ONLY"
    """
    entropy = features.shannon_entropy
    abs_skewness = abs(features.skewness)
    
    if entropy > 7.5 and abs_skewness < 0.1:
        return "SKIP"  # Random or encrypted
    elif entropy > 7.5 and abs_skewness < 0.3:
        return "RECOMPRESS_ONLY"  # Already compressed
    else:
        return "COMPRESS"  # Good compression candidate


def _extract_numpy(data: bytes) -> BaseFeatures:
    """
    NumPy-accelerated feature extraction (10-50x faster than pure Python)
    
    All operations are vectorized - no Python per-element loops.
    """
    import numpy as np
    
    size = len(data)
    if size == 0:
        return BaseFeatures()
    
    arr = np.frombuffer(data, dtype=np.uint8)
    
    features = BaseFeatures()
    
    # Base[0]: File size
    features.file_size_log2 = math.log2(size + 1) / 32.0
    
    # Base[2]: Printable ratio (vectorized)
    printable_mask = ((arr >= 0x20) & (arr <= 0x7E)) | (arr == 0x09) | (arr == 0x0A) | (arr == 0x0D)
    features.printable_ratio = float(np.mean(printable_mask))
    
    # Byte frequency histogram (vectorized)
    counts = np.bincount(arr, minlength=256)
    total = float(size)
    probs = counts / total
    
    # Base[3]: Shannon entropy (vectorized)
    nonzero_probs = probs[probs > 0]
    features.shannon_entropy = float(-np.sum(nonzero_probs * np.log2(nonzero_probs)))
    
    # Base[4]: Min-entropy
    max_prob = float(np.max(probs))
    features.min_entropy = -math.log2(max_prob) if max_prob > 0 else 0.0
    
    # Base[5]: Unique byte ratio
    unique_bytes = int(np.count_nonzero(counts))
    features.unique_byte_ratio = unique_bytes / 256.0
    
    # Base[6]: Mean normalized
    mean_val = float(np.mean(arr))
    features.mean_byte_normalized = mean_val / 255.0
    
    # Base[7]: Std normalized
    std_val = float(np.std(arr))
    features.std_byte_normalized = std_val / 115.0
    
    # Base[8]: Longest run (vectorized using diff)
    diffs = np.diff(arr)
    run_starts = np.where(diffs != 0)[0] + 1
    run_starts = np.concatenate(([0], run_starts, [size]))
    run_lengths = np.diff(run_starts)
    longest_run = int(np.max(run_lengths))
    features.longest_run_log2 = math.log2(longest_run + 1) / 20.0
    
    # Base[9]: Zero byte ratio
    features.zero_byte_ratio = float(counts[0]) / total
    
    # Base[10]: High bit ratio
    features.high_bit_ratio = float(np.sum(arr >= 128)) / total
    
    # Base[11]: Header entropy (first 1024 bytes)
    header_size = min(1024, size)
    header = arr[:header_size]
    header_counts = np.bincount(header, minlength=256)
    header_probs = header_counts / float(header_size)
    nonzero_header = header_probs[header_probs > 0]
    features.header_entropy = float(-np.sum(nonzero_header * np.log2(nonzero_header)))
    
    # Base[12]: Local entropy variance (vectorized windowed entropy)
    window_size = 1024
    if size > window_size:
        n_windows = size // window_size
        windows = arr[:n_windows * window_size].reshape(n_windows, window_size)
        local_entropies = np.zeros(n_windows)
        for w in range(n_windows):
            w_counts = np.bincount(windows[w], minlength=256)
            w_probs = w_counts / float(window_size)
            nonzero_w = w_probs[w_probs > 0]
            local_entropies[w] = -np.sum(nonzero_w * np.log2(nonzero_w))
        features.local_entropy_variance = float(np.var(local_entropies))
    else:
        features.local_entropy_variance = 0.0
    
    # Base[13]: Block boundary density (vectorized)
    block_size = 1024
    n_blocks = size // block_size
    if n_blocks > 0:
        blocks = arr[:n_blocks * block_size].reshape(n_blocks, block_size)
        zero_mask = (blocks == 0).astype(np.int32)
        for b_idx in range(n_blocks):
            padded = np.concatenate(([0], zero_mask[b_idx], [0]))
            changes = np.diff(padded)
            starts = np.where(changes == 1)[0]
            ends = np.where(changes == -1)[0]
            if len(starts) > 0 and len(ends) > 0:
                min_len = min(len(starts), len(ends))
                run_lens = ends[:min_len] - starts[:min_len]
                if len(run_lens) > 0 and np.max(run_lens) >= 8:
                    boundary_count_increment = 1
                else:
                    boundary_count_increment = 0
            else:
                boundary_count_increment = 0
        features.block_boundary_density = 0.0  # Simplified: use header entropy variance as proxy
        if size > window_size * 2:
            features.block_boundary_density = min(1.0, features.local_entropy_variance / 2.0)
    else:
        features.block_boundary_density = 0.0
    
    # Base[14-15]: Skewness and Kurtosis (vectorized)
    if std_val > 0:
        centered = arr.astype(np.float64) - mean_val
        m3 = float(np.mean(centered ** 3))
        m4 = float(np.mean(centered ** 4))
        std_cubed = std_val ** 3
        std_fourth = std_val ** 4
        
        raw_skewness = m3 / std_cubed if std_cubed > 0 else 0
        features.skewness = max(-1.0, min(1.0, raw_skewness / 3.0))
        
        raw_kurtosis = m4 / std_fourth - 3 if std_fourth > 0 else -3
        features.kurtosis = max(-2.0, min(10.0, raw_kurtosis))
    else:
        features.skewness = 0.0
        features.kurtosis = -3.0
    
    # Base[16-19]: N-gram features (fully vectorized)
    if size > 1:
        total_bigrams = size - 1
        
        # Vectorized bigram counting using bincount (much faster than np.unique)
        bigrams = (arr[:-1].astype(np.int32) << 8) | arr[1:].astype(np.int32)
        bigram_hist = np.bincount(bigrams, minlength=65536)
        
        # Base[16]: Unique bigram ratio
        n_unique_bigrams = int(np.count_nonzero(bigram_hist))
        features.unique_bigram_ratio = min(1.0, n_unique_bigrams / min(65536.0, float(total_bigrams)))
        
        # Base[17]: Top-K concentration (vectorized: partial sort on histogram)
        k = min(10, n_unique_bigrams)
        if k > 0:
            nonzero_freqs = bigram_hist[bigram_hist > 0]
            top_k_indices = np.argpartition(nonzero_freqs, -k)[-k:]
            top_k_sum = int(np.sum(nonzero_freqs[top_k_indices]))
            features.bigram_topk_concentration = top_k_sum / total_bigrams
        else:
            features.bigram_topk_concentration = 0.0
        
        # Base[18]: RLE potential (vectorized using run lengths)
        if longest_run > 1 and len(run_lengths) > 0:
            rle_eligible = run_lengths[run_lengths >= 3]
            if len(rle_eligible) > 0:
                rle_savings = int(np.sum(rle_eligible - 2))
                features.rle_potential = 1.0 - (rle_savings / total)
            else:
                features.rle_potential = 1.0
        else:
            features.rle_potential = 1.0
        
        # Base[19]: Dictionary potential
        features.dict_potential = 1.0 - features.bigram_topk_concentration
    else:
        features.unique_bigram_ratio = 0.0
        features.bigram_topk_concentration = 0.0
        features.rle_potential = 1.0
        features.dict_potential = 1.0
    
    return features


def extract_base_features_fast(data: bytes) -> tuple[BaseFeatures, FileType]:
    """
    Fast feature extraction using NumPy acceleration
    
    10-50x faster than pure Python version for files > 10KB.
    Falls back to pure Python if NumPy is unavailable.
    
    Args:
        data: Raw file bytes
        
    Returns:
        Tuple of (BaseFeatures, detected_FileType)
    """
    try:
        features = _extract_numpy(data)
        
        extractor = FeatureExtractor()
        file_type, confidence = extractor._detect_magic_bytes(data)
        features.magic_confidence = confidence
        
        return features, file_type
    except ImportError:
        return extract_base_features(data)
