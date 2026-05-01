from __future__ import annotations

import logging
from abc import ABC, abstractmethod

from gui.core.engine import CompressionEngine
from gui.core.models import (
    AlgorithmType,
    CompressionStatus,
    FileRecord,
)

logger = logging.getLogger(__name__)


class DecisionEngine(ABC):
    @abstractmethod
    def decide(self, record: FileRecord) -> AlgorithmType:
        pass
    @abstractmethod
    def train(self, records: list[FileRecord]) -> None:
        pass


    
class RamdomForest(DecisionEngine):
    def __init__(self):
        pass
    def decide(self, record: Record) -> AlgorithmType:
        pass
    def train(self, records: list[Record]) -> None:
        pass

class NeuralNetwork(DecisionEngine):
    def __init__(self):
        try:
            from torch import nn
            self.linear = nn.Linear(100, 100)
            self.relu = nn.ReLU()
            self.dropout = nn.Dropout(0.5)
            self.linear2 = nn.Linear(100, 2)
            self.softmax = nn.Softmax(dim=1)
        except ImportError:
            raise RuntimeError("torch not installed, NeuralNetwork unavailable")
    def decide(self, record: FileRecord) -> AlgorithmType:
        pass
    def train(self, records: list[FileRecord]) -> None:
        pass

# class StrategyDispatcher:
#     def __init__(
#         self,
#         engine: CompressionEngine | None = None,
#         decision_engine: DecisionEngine | None = None,
#     ):
#         self._engine = engine or CompressionEngine()
#         self._decision_engine = decision_engine or DefaultEngine()

#     def dispatch(self, record: Record, quality: int = 100) -> Record:
#         record.extract_features()
#         algorithm = self._decision_engine.decide(record)
#         record.algorithm = algorithm
#         record.status = CompressionStatus.COMPRESSING
#         try:
#             result = self._engine.compress(record.raw_data, algorithm)
#             record.compressed_data = result.data
#             record.compression_ratio = result.compression_ratio
#             record.compression_time_ms = result.time_ms
#             record.status = CompressionStatus.DONE
#         except Exception as e:
#             record.status = CompressionStatus.FAILED
#             record.error_message = str(e)
#             logger.error("compress failed %s: %s", record.filepath, e)
#         return record

#     def dispatch_batch(self, records: list[FileRecord], quality: int = 100) -> list[FileRecord]:
#         return [self.dispatch(rec, quality) for rec in records]
