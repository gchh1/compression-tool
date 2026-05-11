from __future__ import annotations

import logging
from pathlib import Path

from gui.models import (
    IMAGE_EXTENSIONS,
    TEXT_EXTENSIONS,
    FileRecord,
    ResourceType,
)

logger = logging.getLogger(__name__)


def scan_directory(directory: str | Path, recursive: bool = True) -> list[FileRecord]:
    directory = Path(directory)
    if not directory.is_dir():
        raise FileNotFoundError(f"Not a directory: {directory}")

    records: list[FileRecord] = []
    pattern = "**/*" if recursive else "*"

    for p in sorted(directory.glob(pattern)):
        if not p.is_file():
            continue
        try:
            rec = FileRecord.from_path(p)
            records.append(rec)
        except OSError as e:
            logger.warning("skip %s: %s", p, e)

    logger.info("scanned %d files from %s", len(records), directory)
    return records


def load_file_data(record: FileRecord) -> FileRecord:
    p = Path(record.path)
    if not p.exists():
        raise FileNotFoundError(f"File not found: {p}")
    record.raw_data = p.read_bytes()
    record.size = len(record.raw_data)
    return record


def load_batch(records: list[FileRecord]) -> list[FileRecord]:
    for rec in records:
        try:
            load_file_data(rec)
        except OSError as e:
            logger.warning("load failed %s: %s", rec.name, e)
    return records


def filter_by_type(records: list[FileRecord], rtype: ResourceType) -> list[FileRecord]:
    return [r for r in records if r.type == rtype]


def group_by_type(records: list[FileRecord]) -> dict[ResourceType, list[FileRecord]]:
    groups: dict[ResourceType, list[FileRecord]] = {}
    for rec in records:
        groups.setdefault(rec.type, []).append(rec)
    return groups


def format_size(size_bytes: int) -> str:
    if size_bytes < 1024:
        return f"{size_bytes} B"
    if size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.1f} KB"
    if size_bytes < 1024 * 1024 * 1024:
        return f"{size_bytes / (1024 * 1024):.2f} MB"
    return f"{size_bytes / (1024 * 1024 * 1024):.2f} GB"
