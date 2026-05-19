"""Preset phrase dictionary preprocess for HTML/CSS/JS-like text (WCX flag bit)."""

from __future__ import annotations

import logging
import sys
from functools import lru_cache
from pathlib import Path

logger = logging.getLogger(__name__)

_ESC = 0xFF
_LITERAL_ESC = 0x0000  # after _ESC: u16 0 => literal 0xFF
_MIN_PHRASE_LEN = 3  # shorter phrases expand 1 byte -> 3 and hurt ratio


def _dict_file_paths() -> list[Path]:
    """Resolve ``web_phrases.txt`` for dev tree and PyInstaller ``WebCompress.exe``."""
    candidates: list[Path] = []
    if getattr(sys, "frozen", False):
        exe_dir = Path(sys.executable).resolve().parent
        meipass = getattr(sys, "_MEIPASS", None)
        if meipass:
            candidates.append(Path(meipass) / "resources" / "dict" / "web_phrases.txt")
        candidates.extend(
            [
                exe_dir / "resources" / "dict" / "web_phrases.txt",
                exe_dir.parent / "resources" / "dict" / "web_phrases.txt",
            ]
        )
    else:
        repo = Path(__file__).resolve().parent.parent.parent.parent
        candidates.extend(
            [
                repo / "resources" / "dict" / "web_phrases.txt",
                repo / "Package" / "resources" / "dict" / "web_phrases.txt",
            ]
        )
    return candidates


@lru_cache(maxsize=1)
def load_phrases() -> tuple[bytes, ...]:
    lines: list[str] = []
    loaded_from: Path | None = None
    for path in _dict_file_paths():
        if path.is_file():
            for raw in path.read_text(encoding="utf-8").splitlines():
                s = raw.strip()
                if not s or s.startswith("#"):
                    continue
                if len(s.encode("utf-8")) < _MIN_PHRASE_LEN:
                    logger.debug("[web_dict] skip phrase too short: %r", s)
                    continue
                lines.append(s)
            loaded_from = path
            break
    if not lines:
        logger.warning(
            "[web_dict] web_phrases.txt not found (checked %s); using minimal fallback",
            ", ".join(str(p) for p in _dict_file_paths()),
        )
        lines = ["<div>", "class=", "px;", "function"]
    else:
        logger.info("[web_dict] loaded %d phrases from %s", len(lines), loaded_from)
    lines.sort(key=len, reverse=True)
    return tuple(s.encode("utf-8") for s in lines)


def encode(data: bytes) -> bytes:
    phrases = load_phrases()
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        if data[i] == _ESC:
            out.append(_ESC)
            out.append(0)
            out.append(0)
            i += 1
            continue
        matched = False
        for idx, phrase in enumerate(phrases):
            plen = len(phrase)
            if plen > 0 and i + plen <= n and data[i : i + plen] == phrase:
                out.append(_ESC)
                code = idx + 1
                out.append(code & 0xFF)
                out.append((code >> 8) & 0xFF)
                i += plen
                matched = True
                break
        if not matched:
            out.append(data[i])
            i += 1
    return bytes(out)


def decode(data: bytes) -> bytes:
    phrases = load_phrases()
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        if data[i] != _ESC:
            out.append(data[i])
            i += 1
            continue
        if i + 3 > n:
            raise ValueError("truncated web-dict escape sequence")
        code = data[i + 1] | (data[i + 2] << 8)
        i += 3
        if code == _LITERAL_ESC:
            out.append(_ESC)
        elif 1 <= code <= len(phrases):
            out.extend(phrases[code - 1])
        else:
            raise ValueError(f"unknown web-dict phrase code {code}")
    return bytes(out)


def prepare_file_record_for_compression(record) -> bool:
    """Load plaintext, apply dictionary encode into ``record.raw_data``. Returns True if applied."""
    from gui.config.settings import get_use_web_resource_dict

    if not get_use_web_resource_dict():
        return False
    if not getattr(record, "path", None):
        return False
    record.load_raw_data()
    record.plaintext_snapshot = bytes(record.raw_data)
    encoded = encode(record.plaintext_snapshot)
    record.raw_data = encoded
    record.web_dict_preprocess = True
    logger.info(
        "[web_dict] preprocessed %s: %d -> %d bytes",
        getattr(record, "name", "?"),
        len(record.plaintext_snapshot),
        len(record.raw_data),
    )
    return True


def postprocess_after_codec(payload: bytes, *, web_dict_preprocess: bool) -> bytes:
    if not web_dict_preprocess:
        return payload
    return decode(payload)
