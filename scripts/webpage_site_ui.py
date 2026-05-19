"""Shared HTML/CSS for webpage_basic and webpage_large demo sites (file:// friendly)."""

from __future__ import annotations

import random

# --- Presentation layer (applied to real markup) ---------------------------------

SITE_CSS = """\
/* WebCompress coursework demo — presentation (not compression filler) */
:root {
  --wc-bg: #eef2f7;
  --wc-surface: #ffffff;
  --wc-surface-2: #f8fafc;
  --wc-text: #1a2332;
  --wc-muted: #5c6b7a;
  --wc-primary: #1e4d8c;
  --wc-primary-dark: #163a6b;
  --wc-accent: #0d9488;
  --wc-border: #d8e0ea;
  --wc-shadow: 0 8px 28px rgba(22, 45, 80, 0.08);
  --wc-radius: 12px;
  --wc-font: "Segoe UI", "PingFang SC", "Microsoft YaHei", system-ui, sans-serif;
}

*, *::before, *::after { box-sizing: border-box; }

html { scroll-behavior: smooth; }

body.site {
  margin: 0;
  min-height: 100vh;
  font-family: var(--wc-font);
  font-size: 16px;
  line-height: 1.65;
  color: var(--wc-text);
  background: linear-gradient(165deg, #e8eef5 0%, var(--wc-bg) 40%, #dfe8f2 100%);
}

a { color: var(--wc-primary); text-decoration: none; }
a:hover { text-decoration: underline; color: var(--wc-primary-dark); }

.site-header {
  background: linear-gradient(120deg, var(--wc-primary-dark), var(--wc-primary) 55%, #2563a8);
  color: #fff;
  padding: 0;
  box-shadow: var(--wc-shadow);
}

.site-header .bar {
  max-width: 1120px;
  margin: 0 auto;
  padding: 1rem 1.5rem;
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 1rem 1.5rem;
}

.site-header h1 {
  margin: 0;
  font-size: 1.35rem;
  font-weight: 700;
  letter-spacing: 0.02em;
}

.site-nav {
  display: flex;
  flex-wrap: wrap;
  gap: 0.35rem 1rem;
  margin-left: auto;
}

.site-nav a {
  color: rgba(255, 255, 255, 0.92);
  font-size: 0.92rem;
  padding: 0.35rem 0.6rem;
  border-radius: 6px;
}

.site-nav a:hover,
.site-nav a.active {
  background: rgba(255, 255, 255, 0.15);
  text-decoration: none;
  color: #fff;
}

.site-main {
  max-width: 1120px;
  margin: 0 auto;
  padding: 1.75rem 1.5rem 3rem;
}

.site-footer {
  margin-top: auto;
  background: var(--wc-surface);
  border-top: 1px solid var(--wc-border);
  padding: 1.25rem 1.5rem;
  text-align: center;
  color: var(--wc-muted);
  font-size: 0.88rem;
}

.hero {
  position: relative;
  border-radius: var(--wc-radius);
  overflow: hidden;
  margin-bottom: 2rem;
  box-shadow: var(--wc-shadow);
  background: var(--wc-primary-dark);
}

.hero img {
  display: block;
  width: 100%;
  height: auto;
  max-height: 420px;
  object-fit: cover;
  opacity: 0.92;
}

.hero-caption {
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  padding: 1.5rem 1.75rem;
  background: linear-gradient(transparent, rgba(15, 35, 60, 0.85));
  color: #fff;
}

.hero-caption h2 { margin: 0 0 0.35rem; font-size: 1.5rem; }
.hero-caption p { margin: 0; opacity: 0.9; font-size: 0.95rem; }

.content-card {
  background: var(--wc-surface);
  border: 1px solid var(--wc-border);
  border-radius: var(--wc-radius);
  padding: 1.5rem 1.75rem;
  margin-bottom: 1.5rem;
  box-shadow: var(--wc-shadow);
}

.content-card h2 {
  margin: 0 0 1rem;
  font-size: 1.15rem;
  color: var(--wc-primary-dark);
  border-bottom: 2px solid var(--wc-accent);
  padding-bottom: 0.5rem;
  display: inline-block;
}

.lorem-block p {
  margin: 0 0 0.85rem;
  color: var(--wc-muted);
  font-size: 0.9rem;
}

.lorem-block p:last-child { margin-bottom: 0; }

.feature-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 1rem;
  margin: 1.25rem 0 0;
}

.feature-tile {
  background: var(--wc-surface-2);
  border: 1px solid var(--wc-border);
  border-radius: 10px;
  padding: 1rem 1.1rem;
}

.feature-tile strong {
  display: block;
  color: var(--wc-primary);
  margin-bottom: 0.35rem;
}

.wc-media-teaser {
  background: linear-gradient(135deg, #ecfdf5, #f0f9ff);
  border: 1px solid #a7f3d0;
  border-radius: var(--wc-radius);
  padding: 1.25rem 1.5rem;
  margin-top: 1.5rem;
}

.wc-media-teaser h2 { margin-top: 0; color: #0f766e; }

.gallery-page .gallery-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 1.25rem;
}

.gallery-page figure {
  margin: 0;
  background: var(--wc-surface);
  border: 1px solid var(--wc-border);
  border-radius: 10px;
  overflow: hidden;
  box-shadow: var(--wc-shadow);
}

.gallery-page figure img {
  width: 100%;
  height: 160px;
  object-fit: cover;
  display: block;
  background: #cbd5e1;
}

.gallery-page figcaption {
  padding: 0.5rem 0.65rem;
  font-size: 0.72rem;
  color: var(--wc-muted);
  word-break: break-all;
}

.gallery-page audio,
.gallery-page video {
  max-width: 100%;
  border-radius: 8px;
}

.gallery-page section { margin-bottom: 2.5rem; }

.gallery-page ul { list-style: none; padding: 0; }
.gallery-page li {
  background: var(--wc-surface);
  border: 1px solid var(--wc-border);
  border-radius: 10px;
  padding: 0.75rem 1rem;
  margin-bottom: 0.75rem;
}

@media (max-width: 640px) {
  .site-header .bar { flex-direction: column; align-items: flex-start; }
  .site-nav { margin-left: 0; }
}

@media print {
  body.site { background: #fff; }
  .site-header { background: #333; }
  .lorem-block { display: none; }
}
"""

LARGE_NAV = [
    ("index.html", "首页"),
    ("products.html", "产品"),
    ("gallery.html", "媒体画廊"),
    ("blog.html", "博客"),
    ("docs.html", "文档"),
    ("contact.html", "联系"),
]

BASIC_NAV = [
    ("index.html", "首页"),
    ("about.html", "关于"),
    ("contact.html", "联系"),
]

LARGE_TITLES = {
    "index.html": "首页",
    "about.html": "关于我们",
    "products.html": "产品中心",
    "blog.html": "博客",
    "contact.html": "联系我们",
    "faq.html": "常见问题",
    "privacy.html": "隐私政策",
    "terms.html": "服务条款",
    "dashboard.html": "控制台",
    "login.html": "登录",
    "register.html": "注册",
    "error404.html": "页面未找到",
    "search.html": "搜索",
    "profile.html": "个人资料",
    "settings.html": "设置",
    "product_detail.html": "产品详情",
    "pricing.html": "价格",
    "docs.html": "技术文档",
    "gallery.html": "媒体资源画廊",
}


def page_title(filename: str, *, large: bool) -> str:
    if large:
        base = LARGE_TITLES.get(filename, filename.replace(".html", "").replace("_", " "))
    else:
        base = filename.replace(".html", "").replace("_", " ")
    return f"{base} — WebCompress 演示站"


def lorem_paragraphs(n: int, seed: int = 0) -> str:
    rng = random.Random(seed)
    words = (
        "网页 压缩 算法 Deflate LZSS Huffman 字典 编码 滑动窗口 "
        "无损 传输 存储 优化 HTML CSS JavaScript 资源 加载 性能".split()
    )
    paras = []
    for _ in range(n):
        sents = []
        for _ in range(rng.randint(2, 4)):
            sents.append(
                "".join(rng.choice(words) for _ in range(rng.randint(12, 20))) + "。"
            )
        paras.append("<p>" + "".join(sents) + "</p>")
    return "\n".join(paras)


def html_page(
    filename: str,
    body_extra: str,
    css_hrefs: list[str],
    js_srcs: list[str],
    *,
    large: bool = False,
    active: str | None = None,
) -> str:
    active = active or filename
    nav = LARGE_NAV if large else BASIC_NAV
    title = page_title(filename, large=large)
    site_label = "WebCompress 大型网页测试站" if large else "WebCompress 演示站（基础版）"

    css_links = "\n".join(f'  <link rel="stylesheet" href="{h}">' for h in css_hrefs)
    js_tags = "\n".join(f'  <script src="{s}" defer></script>' for s in js_srcs)
    nav_html = "\n".join(
        f'      <a href="{href}" class="{"active" if href == active else ""}">{label}</a>'
        for href, label in nav
    )
    lorem = lorem_paragraphs(6 if large else 8, hash(filename) % 10000)

    features = ""
    if filename == "index.html":
        features = """
    <div class="feature-grid" aria-label="功能概览">
      <div class="feature-tile"><strong>多算法压缩</strong> LZSS / LZDP / Deflate / Brotli 等</div>
      <div class="feature-tile"><strong>ADE 决策</strong> 按内容特征自动选算法与参数</div>
      <div class="feature-tile"><strong>流式大文件</strong> 分块读写，控制内存占用</div>
      <div class="feature-tile"><strong>课程测试包</strong> 本目录为固定基准网页资源</div>
    </div>
"""

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="description" content="WebCompress 网页压缩课程设计测试站点">
  <title>{title}</title>
{css_links}
</head>
<body class="site">
  <header class="site-header">
    <div class="bar">
      <h1>{site_label}</h1>
      <nav class="site-nav" aria-label="主导航">
{nav_html}
      </nav>
    </div>
  </header>
  <main class="site-main">
{body_extra}
    <article class="content-card lorem-block">
      <h2>说明</h2>
{lorem}
    </article>
{features}
  </main>
  <footer class="site-footer">
    <p>WebCompress 网页测试资源 · {title}</p>
  </footer>
{js_tags}
</body>
</html>
"""


def index_hero_large() -> str:
    return """\
    <section class="hero">
      <img src="images/hero_banner.png" alt="站点横幅" width="1200" height="450">
      <div class="hero-caption">
        <h2>大型网页压缩基准</h2>
        <p>HTML / CSS / JS / 图片 / 音视频 — 用于 WebCompress 批量与流式测试</p>
      </div>
    </section>
    <section class="content-card wc-media-teaser">
      <h2>多媒体资源</h2>
      <p>真实图片、音频、视频见 <a href="gallery.html">媒体画廊</a> 与 <code>media/</code> 目录。</p>
      <p>自备样本可放入 <code>media/_user_drop/</code> 后运行 <code>scripts/setup_webpage_large_media.py</code>。</p>
    </section>
"""


def index_hero_basic() -> str:
    return """\
    <section class="hero">
      <img src="images/hero.png" alt="站点横幅" width="960" height="400">
      <div class="hero-caption">
        <h2>基础版演示站</h2>
        <p>中小型网页 · 文本 + 图片 · 适合快速压缩验证</p>
      </div>
    </section>
"""


def presentation_css_files() -> dict[str, str]:
    """main/layout/theme get real UI; other CSS files keep filler for size diversity."""
    return {
        "main.css": SITE_CSS,
        "layout.css": "/* layout hooks */\n.site-main { display: block; }\n",
        "theme.css": (
            "/* accent overrides */\n"
            ":root { --wc-accent: #7c3aed; }\n"
            ".site-header { background: linear-gradient(120deg, #312e81, #4f46e5 50%, #6366f1); }\n"
        ),
        "print.css": "@media print { .site-nav { display: none; } }\n",
    }


def css_filler(name: str, rules: int = 40) -> str:
    lines = [f"/* compression filler — {name} */"]
    for i in range(rules):
        lines.append(
            f".wc-{name.replace('.','')}-{i} {{ margin:{i % 12}px; padding:{i % 8}px; }}"
        )
    return "\n".join(lines) + "\n"


def gallery_html(
    *,
    img_tags: list[str],
    aud_tags: list[str],
    vid_tags: list[str],
    n_imgs: int,
    n_auds: int,
    n_vids: int,
) -> str:
    imgs = "\n".join(img_tags)
    auds = "\n".join(aud_tags) if aud_tags else (
        "    <li>（无音频 — 请放入 media/audio 或 media/_user_drop/audio）</li>"
    )
    vids = "\n".join(vid_tags) if vid_tags else (
        "    <p>（无视频 — 请放入 media/video 或 media/_user_drop/video）</p>"
    )
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>媒体资源画廊 — WebCompress 演示站</title>
  <link rel="stylesheet" href="css/main.css">
  <link rel="stylesheet" href="css/layout.css">
</head>
<body class="site gallery-page">
  <header class="site-header">
    <div class="bar">
      <h1>多媒体资源画廊</h1>
      <nav class="site-nav">
        <a href="index.html">返回首页</a>
        <a href="products.html">产品</a>
      </nav>
    </div>
  </header>
  <main class="site-main">
    <p class="content-card" style="margin-top:0">本页引用 <code>media/</code> 下真实图片、音频、视频，用于多类型压缩测试。</p>
    <section><h2>图片 ({n_imgs})</h2><div class="gallery-grid">
{imgs}
    </div></section>
    <section><h2>音频 ({n_auds})</h2><ul>
{auds}
    </ul></section>
    <section><h2>视频 ({n_vids})</h2>
{vids}
    </section>
  </main>
  <footer class="site-footer"><p>WebCompress · 媒体画廊</p></footer>
</body>
</html>
"""
