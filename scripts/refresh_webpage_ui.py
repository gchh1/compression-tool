#!/usr/bin/env python3
"""Refresh HTML/CSS presentation for webpage fixtures (keeps images, data/, media/)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))

from gen_webpage_fixtures import js_file, write_text  # noqa: E402
from webpage_site_ui import (  # noqa: E402
    css_filler,
    html_page,
    index_hero_basic,
    index_hero_large,
    presentation_css_files,
)

RES = REPO / "resources"
LARGE = RES / "webpage_large"


def refresh_css(base: Path, *, large: bool) -> None:
    pres = presentation_css_files()
    if large:
        write_text(base / "css" / "main.css", pres["main.css"])
        write_text(base / "css" / "layout.css", pres["layout.css"])
        write_text(base / "css" / "theme.css", pres["theme.css"])
        write_text(base / "css" / "print.css", pres["print.css"])
        write_text(base / "css" / "components.css", css_filler("components", 90))
    else:
        write_text(base / "css" / "style.css", pres["main.css"])
        write_text(base / "css" / "layout.css", pres["layout.css"])


def refresh_large_html(base: Path) -> None:
    css = ["css/main.css", "css/layout.css", "css/theme.css"]
    js = ["js/app.js", "js/utils.js"]
    pages = [
        "index.html", "about.html", "products.html", "blog.html", "contact.html",
        "faq.html", "privacy.html", "terms.html", "dashboard.html", "login.html",
        "register.html", "error404.html", "search.html", "profile.html",
        "settings.html", "product_detail.html", "pricing.html", "docs.html",
    ]
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


def refresh_basic_html(base: Path) -> None:
    css = ["css/style.css", "css/layout.css"]
    js = ["js/main.js", "js/utils.js"]
    write_text(
        base / "index.html",
        html_page("index.html", index_hero_basic(), css, js, large=False),
    )
    write_text(
        base / "about.html",
        html_page("about.html", "", css, js[:1], large=False, active="about.html"),
    )
    write_text(
        base / "contact.html",
        html_page(
            "contact.html",
            '    <section class="content-card"><h2>联系</h2><p>邮箱: test@example.com</p></section>\n',
            css,
            [],
            large=False,
            active="contact.html",
        ),
    )


def refresh_gallery_if_manifest() -> None:
    manifest_path = LARGE / "media" / "manifest.json"
    if not manifest_path.is_file():
        return
    from webpage_site_ui import gallery_html

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = manifest.get("files", [])
    imgs = [f for f in files if f.get("kind") == "image"]
    auds = [f for f in files if f.get("kind") == "audio"]
    vids = [f for f in files if f.get("kind") == "video"]

    def img_tags():
        for f in imgs[:24]:
            yield (
                f'    <figure><img src="media/{f["path"]}" alt="{f["path"]}" '
                f'loading="lazy" width="320"><figcaption>{f["path"]} '
                f'({f.get("bytes", 0) // 1024} KB)</figcaption></figure>'
            )

    def aud_tags():
        for f in auds:
            yield (
                f'    <li><audio controls preload="none" src="media/{f["path"]}"></audio> '
                f'{f["path"]}</li>'
            )

    def vid_tags():
        for f in vids:
            yield (
                f'    <div><video controls width="480" preload="metadata" '
                f'src="media/{f["path"]}"></video>'
                f'<p>{f["path"]} ({f.get("bytes", 0) // 1024} KB)</p></div>'
            )

    html = gallery_html(
        img_tags=list(img_tags()),
        aud_tags=list(aud_tags()),
        vid_tags=list(vid_tags()),
        n_imgs=len(imgs),
        n_auds=len(auds),
        n_vids=len(vids),
    )
    write_text(LARGE / "gallery.html", html)


def main() -> None:
    basic = RES / "webpage_basic"
    large = RES / "webpage_large"
    if basic.is_dir():
        refresh_css(basic, large=False)
        refresh_basic_html(basic)
        print("refreshed", basic.relative_to(REPO))
    if large.is_dir():
        refresh_css(large, large=True)
        refresh_large_html(large)
        refresh_gallery_if_manifest()
        print("refreshed", large.relative_to(REPO))
    print("Done. Open index.html in a browser (file://) to preview.")


if __name__ == "__main__":
    main()
