#!/usr/bin/env python3
"""
Populate resources/webpage_large/media/ with real images, audio, and video.

Sources (in order):
  1. Copy from repo: level4_media, level3_large
  2. Download CC0 / public-domain samples (optional, needs network)
  3. User drop-in: resources/webpage_large/media/_user_drop/

Run: python scripts/setup_webpage_large_media.py
      python scripts/setup_webpage_large_media.py --download
"""

from __future__ import annotations

import argparse
import json
import shutil
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LARGE = REPO / "resources" / "webpage_large"
MEDIA = LARGE / "media"

# Official layout for coursework / GUI tests
DIRS = {
    "images": MEDIA / "images",
    "audio": MEDIA / "audio",
    "video": MEDIA / "video",
    "fonts": MEDIA / "fonts",
    "_user_drop": MEDIA / "_user_drop",
}

# (url, relative path under media/) — small, stable test URLs
DOWNLOADS = [
    (
        "https://upload.wikimedia.org/wikipedia/commons/thumb/3/3f/Fronalpstock_rocks.jpg/1280px-Fronalpstock_rocks.jpg",
        "images/sample_landscape_wikimedia.jpg",
    ),
    (
        "https://upload.wikimedia.org/wikipedia/commons/thumb/4/47/PNG_transparency_demonstration_1.png/320px-PNG_transparency_demonstration_1.png",
        "images/sample_transparency_wikimedia.png",
    ),
    (
        "https://filesamples.com/samples/audio/wav/sample1.wav",
        "audio/sample_filesamples.wav",
    ),
    (
        "https://filesamples.com/samples/audio/mp3/sample1.mp3",
        "audio/sample_filesamples.mp3",
    ),
    (
        "https://filesamples.com/samples/video/mp4/sample_640x360.mp4",
        "video/sample_640x360_filesamples.mp4",
    ),
    (
        "https://filesamples.com/samples/video/webm/sample_640x360.webm",
        "video/sample_640x360_filesamples.webm",
    ),
]


def copy_repo_assets() -> list[str]:
    copied: list[str] = []
    pairs = [
        (REPO / "resources" / "level4_media" / "images", DIRS["images"], ("*.png", "*.svg", "*.bmp")),
        (REPO / "resources" / "level3_large" / "images", DIRS["images"], ("*.jpg", "*.png")),
        (REPO / "resources" / "level4_media" / "audio", DIRS["audio"], ("*.wav",)),
        (REPO / "resources" / "level2_medium" / "images", DIRS["images"], ("*.jpg", "*.png")),
    ]
    for src_dir, dst_dir, patterns in pairs:
        if not src_dir.is_dir():
            continue
        dst_dir.mkdir(parents=True, exist_ok=True)
        for pat in patterns:
            for f in src_dir.glob(pat):
                target = dst_dir / f.name
                if not target.exists() or f.stat().st_mtime > target.stat().st_mtime:
                    shutil.copy2(f, target)
                copied.append(str(target.relative_to(MEDIA)))
    return copied


def download_samples(timeout: int = 60) -> list[dict]:
    results: list[dict] = []
    for url, rel in DOWNLOADS:
        dest = MEDIA / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        entry = {"url": url, "path": rel, "ok": False, "bytes": 0, "error": ""}
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "WebCompress-fixture-setup/1.0"})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                data = resp.read()
            if len(data) < 100:
                entry["error"] = "response too small"
            else:
                dest.write_bytes(data)
                entry["ok"] = True
                entry["bytes"] = len(data)
        except Exception as e:
            entry["error"] = str(e)
        results.append(entry)
    return results


def write_user_drop_readme() -> None:
    text = """# 用户自备媒体（可选）

将你自己准备的文件放入对应子目录（文件名任意，建议英文无空格）：

| 目录 | 格式示例 | 用途 |
|------|----------|------|
| `_user_drop/images/` | .png .jpg .gif .webp .svg .bmp | 网页图片 |
| `_user_drop/audio/` | .wav .mp3 .ogg .flac .aac | 背景音乐、音效 |
| `_user_drop/video/` | .mp4 .webm .avi .mkv | 嵌入视频 |
| `_user_drop/fonts/` | .woff2 .woff .ttf | 网页字体（可选） |

放好后运行：

    python scripts/setup_webpage_large_media.py --link-user

会在 `media/manifest.json` 中登记，并更新 `gallery.html` 链接。
"""
    (DIRS["_user_drop"] / "README.md").write_text(text, encoding="utf-8")


def scan_media() -> dict:
    text_ext = {".html", ".css", ".js", ".json", ".xml", ".txt", ".md"}
    img_ext = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".ico"}
    aud_ext = {".wav", ".mp3", ".ogg", ".flac", ".aac", ".m4a"}
    vid_ext = {".mp4", ".webm", ".avi", ".mkv", ".mov"}

    files: list[dict] = []
    for p in sorted(MEDIA.rglob("*")):
        if not p.is_file() or p.name.startswith("."):
            continue
        rel = p.relative_to(MEDIA).as_posix()
        ext = p.suffix.lower()
        kind = "other"
        if ext in img_ext:
            kind = "image"
        elif ext in aud_ext:
            kind = "audio"
        elif ext in vid_ext:
            kind = "video"
        elif ext in {".woff", ".woff2", ".ttf", ".otf"}:
            kind = "font"
        files.append({"path": rel, "kind": kind, "bytes": p.stat().st_size})

    total = sum(f["bytes"] for f in files)
    return {
        "root": str(MEDIA.relative_to(REPO)).replace("\\", "/"),
        "counts": {
            "image": sum(1 for f in files if f["kind"] == "image"),
            "audio": sum(1 for f in files if f["kind"] == "audio"),
            "video": sum(1 for f in files if f["kind"] == "video"),
            "font": sum(1 for f in files if f["kind"] == "font"),
            "other": sum(1 for f in files if f["kind"] == "other"),
        },
        "total_bytes": total,
        "total_mb": round(total / (1024 * 1024), 2),
        "files": files,
    }


def write_gallery_html(manifest: dict) -> None:
  imgs = [f for f in manifest["files"] if f["kind"] == "image"]
  auds = [f for f in manifest["files"] if f["kind"] == "audio"]
  vids = [f for f in manifest["files"] if f["kind"] == "video"]

  def img_tags():
    for f in imgs[:24]:
      yield f'    <figure><img src="media/{f["path"]}" alt="{f["path"]}" loading="lazy" width="320"><figcaption>{f["path"]} ({f["bytes"]//1024} KB)</figcaption></figure>'

  def aud_tags():
    for f in auds:
      yield f'    <li><audio controls preload="none" src="media/{f["path"]}"></audio> {f["path"]}</li>'

  def vid_tags():
    for f in vids:
      yield (
        f'    <div><video controls width="480" preload="metadata" src="media/{f["path"]}"></video>'
        f'<p>{f["path"]} ({f["bytes"]//1024} KB)</p></div>'
      )

  html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <title>媒体资源画廊 — WebCompress 大型网页</title>
  <link rel="stylesheet" href="css/main.css">
</head>
<body>
  <header><h1>多媒体资源画廊</h1>
  <p>本页引用 <code>media/</code> 下真实图片、音频、视频，用于压缩工具多类型测试。</p>
  <nav><a href="index.html">返回首页</a></nav></header>
  <main>
    <section><h2>图片 ({len(imgs)})</h2><div class="gallery-grid">
{chr(10).join(img_tags())}
    </div></section>
    <section><h2>音频 ({len(auds)})</h2><ul>
{chr(10).join(aud_tags()) or '    <li>（无音频，请将文件放入 media/audio 或 media/_user_drop/audio）</li>'}
    </ul></section>
    <section><h2>视频 ({len(vids)})</h2>
{chr(10).join(vid_tags()) or '    <p>（无视频，请将文件放入 media/video 或 media/_user_drop/video）</p>'}
    </section>
  </main>
</body>
</html>
"""
  (LARGE / "gallery.html").write_text(html, encoding="utf-8")


def patch_index() -> None:
  index = LARGE / "index.html"
  if not index.is_file():
    return
  text = index.read_text(encoding="utf-8")
  block = (
    '    <section class="wc-media-teaser">\n'
    '      <h2>多媒体资源</h2>\n'
    '      <p>真实图片、音频、视频见 <a href="gallery.html">gallery.html</a> 与 <code>media/</code> 目录。</p>\n'
    '      <p>用户自备文件请放入 <code>media/_user_drop/</code> 后重新运行 setup 脚本。</p>\n'
    "    </section>\n"
  )
  if "wc-media-teaser" not in text:
    text = text.replace("  </main>", block + "  </main>", 1)
    index.write_text(text, encoding="utf-8")


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("--download", action="store_true", help="Try downloading public samples")
  args = parser.parse_args()

  for d in DIRS.values():
    d.mkdir(parents=True, exist_ok=True)

  write_user_drop_readme()
  copied = copy_repo_assets()
  print(f"Copied {len(copied)} files from repo fixtures")

  dl_report = []
  if args.download:
    print("Downloading public samples…")
    dl_report = download_samples()
    ok = sum(1 for x in dl_report if x["ok"])
    print(f"  downloaded {ok}/{len(dl_report)}")

  manifest = scan_media()
  manifest["download_report"] = dl_report
  manifest_path = MEDIA / "manifest.json"
  manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")

  write_gallery_html(manifest)
  patch_index()

  print(f"\nMedia root: {manifest['root']}")
  print(f"  images: {manifest['counts']['image']}")
  print(f"  audio:  {manifest['counts']['audio']}")
  print(f"  video:  {manifest['counts']['video']}")
  print(f"  total:  {manifest['total_mb']} MB")
  print(f"  manifest: {manifest_path.relative_to(REPO)}")


if __name__ == "__main__":
  main()
