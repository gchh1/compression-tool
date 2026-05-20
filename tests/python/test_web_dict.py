"""Web dictionary encode/decode and trie vs linear greedy parity."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))

from gui.engine.web_dict import (  # noqa: E402
    WebDictKind,
    _PhraseTrie,
    decode,
    encode,
    load_phrases,
    sort_phrases_for_matching,
)


def _encode_linear(data: bytes, kind: WebDictKind) -> bytes:
    phrases = load_phrases(kind)
    esc = 0xFF
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        if data[i] == esc:
            out.extend((esc, 0, 0))
            i += 1
            continue
        matched = False
        for idx, phrase in enumerate(phrases):
            plen = len(phrase)
            if plen and i + plen <= n and data[i : i + plen] == phrase:
                code = idx + 1
                out.extend((esc, code & 0xFF, (code >> 8) & 0xFF))
                i += plen
                matched = True
                break
        if not matched:
            out.append(data[i])
            i += 1
    return bytes(out)


def test_roundtrip_html_sample():
    plain = b'<!DOCTYPE html><html><head><meta charset="utf-8"></head><body class="x">\xff</body></html>'
    enc = encode(plain, WebDictKind.HTML)
    assert decode(enc, WebDictKind.HTML) == plain


def test_trie_matches_linear_greedy():
    samples = [
        b"<motion.div className=\"foo\" />",
        b"const x = document.getElementById('app');",
        b'{"data": true, "items": []}',
        b"body { display: flex; margin: 0; }",
    ]
    for kind, data in zip(
        (WebDictKind.HTML, WebDictKind.JS, WebDictKind.JSON, WebDictKind.CSS),
        samples,
    ):
        assert encode(data, kind) == _encode_linear(data, kind)


def test_trie_longest_among_prefixes():
    phrases = tuple(s.encode() for s in sort_phrases_for_matching(["class", "class="]))
    trie = _PhraseTrie(phrases)
    data = b'class="a"'
    hit = trie.longest_match(data, 0, len(data))
    assert hit == (0, 6)  # "class=" (longer) before "class" in table order


if __name__ == "__main__":
    test_roundtrip_html_sample()
    test_trie_matches_linear_greedy()
    test_trie_longest_among_prefixes()
    print("ok")
