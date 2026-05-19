#!/usr/bin/env python3
"""
Generate webpage test fixtures for WebCompress coursework.

  resources/webpage_basic/  — 8 text, 4 images, ~3–8 MB (≤10 MB)
  resources/webpage_large/    — 30+ text, 12 images, ~15–45 MB (≤50 MB)

Run: python scripts/gen_webpage_fixtures.py
"""

from __future__ import annotations

import json
import random
import struct
import zlib
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

random.seed(20260519)

REPO = Path(__file__).resolve().parent.parent
RES = REPO / "resources"


def make_png(width: int, height: int, tile: int = 8) -> bytes:
    """Fast PNG: solid tiles (still valid RGB image)."""
    def chunk(ctype: bytes, data: bytes) -> bytes:
        c = ctype + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            tx, ty = x // tile, y // tile
            r = (tx * 37 + ty * 91) % 256
            g = (tx * 53 + ty * 17) % 256
            b = (tx * 19 + ty * 73) % 256
            raw.extend((r, g, b))
    idat = chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    return sig + ihdr + idat + chunk(b"IEND", b"")


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


from webpage_site_ui import (  # noqa: E402
    css_filler,
    html_page,
    index_hero_basic,
    index_hero_large,
    presentation_css_files,
)


def js_file(name: str, lines: int = 80) -> str:
    parts = [f"// {name}", "(function(g){'use strict';"]
    for i in range(lines):
        parts.append(f"  function f{i}(x){{return (x*{i+1})%997;}}")
    parts.append(f"  g.WC_{name.replace('.','_')}={{v:1}};}})(globalThis);")
    return "\n".join(parts) + "\n"


def bulk_text_file(path: Path, target_bytes: int, seed: int) -> None:
    """Repetitive UTF-8 text (compressible, fast to generate)."""
    rng = random.Random(seed)
    unit = (
        "WebCompress 大型网页基准测试：HTML CSS JS 图片资源批量压缩与解压验证。\n"
        "要求压缩耗时与文件大小近似线性，内存占用合理。\n"
    )
    parts = []
    n = 0
    while n < target_bytes:
        line = unit + "".join(rng.choice("abcdef0123456789") for _ in range(64)) + "\n"
        parts.append(line)
        n += len(line.encode("utf-8"))
    write_text(path, "".join(parts))


def write_image(path: Path, width: int, height: int) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = make_png(width, height, tile=12 if width > 600 else 6)
    path.write_bytes(data)
    return len(data)


def build_basic(base: Path) -> dict:
    base.mkdir(parents=True, exist_ok=True)
    (base / "css").mkdir(exist_ok=True)
    (base / "js").mkdir(exist_ok=True)
    (base / "images").mkdir(exist_ok=True)
    (base / "data").mkdir(exist_ok=True)

    pres = presentation_css_files()
    files = {
        "index.html": html_page(
            "index.html",
            index_hero_basic(),
            ["css/style.css", "css/layout.css"],
            ["js/main.js", "js/utils.js"],
            large=False,
        ),
        "about.html": html_page(
            "about.html", "", ["css/style.css"], ["js/main.js"], large=False, active="about.html"
        ),
        "contact.html": html_page(
            "contact.html",
            '    <section class="content-card"><h2>联系</h2><p>邮箱: test@example.com</p></section>\n',
            ["css/style.css"],
            [],
            large=False,
            active="contact.html",
        ),
        "css/style.css": pres["main.css"],
        "css/layout.css": pres["layout.css"],
        "js/main.js": js_file("main.js", 60),
        "js/utils.js": js_file("utils.js", 40),
        "data/config.json": json.dumps(
            {"site": "webpage_basic", "spec": "5-10 text, 3-5 images, <=10MB"},
            indent=2,
            ensure_ascii=False,
        ),
        "readme.txt": (
            "WebCompress 基础网页测试包 (webpage_basic)\n"
            "规格: 8 个文本类文件, 4 张图片, 总大小 ≤ 10 MB\n"
            "用途: 课程设计「中小型网页」压缩/解压与 ADE 测试。\n"
        ),
    }
    for rel, content in files.items():
        write_text(base / rel, content)

    bulk_text_file(base / "data" / "article_corpus.txt", 4_000_000, seed=1)

    for rel, w, h in [
        ("images/logo.png", 128, 128),
        ("images/hero.png", 960, 400),
        ("images/banner.png", 800, 200),
        ("images/gallery.png", 640, 480),
    ]:
        write_image(base / rel, w, h)

    return summarize(base, "webpage_basic")


def build_large(base: Path) -> dict:
    base.mkdir(parents=True, exist_ok=True)
    for d in ("css", "js", "images", "data", "blog"):
        (base / d).mkdir(exist_ok=True)

    pres = presentation_css_files()
    for cn, content in pres.items():
        write_text(base / "css" / cn, content)
    write_text(base / "css" / "components.css", css_filler("components", 90))
    for jn in ["app.js", "api.js", "utils.js", "charts.js", "router.js", "analytics.js"]:
        write_text(base / "js" / jn, js_file(jn, 90))

    pages = [
        "index.html", "about.html", "products.html", "blog.html", "contact.html",
        "faq.html", "privacy.html", "terms.html", "dashboard.html", "login.html",
        "register.html", "error404.html", "search.html", "profile.html",
        "settings.html", "product_detail.html", "pricing.html", "docs.html",
    ]
    css = ["css/main.css", "css/layout.css", "css/theme.css"]
    js = ["js/app.js", "js/utils.js"]
    for name in pages:
        extra = index_hero_large() if name == "index.html" else ""
        write_text(
            base / name,
            html_page(name, extra, css, js, large=True, active=name),
        )

    for i in range(1, 9):
        write_text(
            base / "blog" / f"post_{i}.html",
            html_page(f"post_{i}.html", "", css[:2], js[:1], large=True),
        )

    write_text(base / "sitemap.xml", '<?xml version="1.0"?><urlset></urlset>\n')
    write_text(base / "robots.txt", "User-agent: *\nAllow: /\n")
    write_text(base / "manifest.json", json.dumps({"name": "WC Large"}, indent=2))
    write_text(
        base / "data" / "catalog.json",
        json.dumps({"items": [{"id": i, "sku": f"P-{i:04d}"} for i in range(800)]}, indent=2),
    )
    bulk_text_file(base / "data" / "documentation.txt", 18_000_000, seed=2)
    bulk_text_file(base / "data" / "changelog.txt", 8_000_000, seed=3)
    bulk_text_file(base / "data" / "api_responses.txt", 6_000_000, seed=4)
    write_text(
        base / "readme.txt",
        "WebCompress 大型网页测试包 (webpage_large)\n"
        "规格: 30+ 文本文件, 12 张图片, 总大小 ≤ 50 MB\n",
    )

    for rel, w, h in [
        ("images/hero_banner.png", 1200, 450),
        ("images/product_1.png", 800, 600),
        ("images/product_2.png", 800, 600),
        ("images/product_3.png", 800, 600),
        ("images/product_4.png", 800, 600),
        ("images/gallery_1.png", 900, 675),
        ("images/gallery_2.png", 900, 675),
        ("images/gallery_3.png", 800, 600),
        ("images/gallery_4.png", 800, 600),
        ("images/team_photo.png", 1000, 667),
        ("images/office_wide.png", 1400, 350),
        ("images/icon_strip.png", 400, 1200),
    ]:
        write_image(base / rel, w, h)

    return summarize(base, "webpage_large")


def summarize(base: Path, label: str) -> dict:
    text_ext = {".html", ".css", ".js", ".json", ".xml", ".txt", ".md", ".svg"}
    img_ext = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp"}
    text_n = img_n = 0
    total = 0
    for p in base.rglob("*"):
        if not p.is_file():
            continue
        sz = p.stat().st_size
        total += sz
        ext = p.suffix.lower()
        if ext in text_ext:
            text_n += 1
        elif ext in img_ext:
            img_n += 1
    return {
        "label": label,
        "path": str(base.relative_to(REPO)).replace("\\", "/"),
        "text_files": text_n,
        "image_files": img_n,
        "total_bytes": total,
        "total_mb": round(total / (1024 * 1024), 2),
    }


def main() -> None:
    import shutil

    for name in ("webpage_basic", "webpage_large"):
        p = RES / name
        if p.exists():
            shutil.rmtree(p)

    results = [build_basic(RES / "webpage_basic"), build_large(RES / "webpage_large")]
    for r in results:
        print(f"\n=== {r['label']} ===")
        print(f"  path: {r['path']}")
        print(f"  text: {r['text_files']}  images: {r['image_files']}")
        print(f"  total: {r['total_mb']} MB ({r['total_bytes']:,} bytes)")

    manifest = RES / "webpage_fixtures_manifest.json"
    manifest.write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nWrote {manifest.relative_to(REPO)}")


if __name__ == "__main__":
    main()
