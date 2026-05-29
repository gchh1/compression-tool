from pathlib import Path

html_path = Path(r"d:\AAA_C\compression-tool\src\gui\ui\views\html\deflate_dashboard.html")
raw = html_path.read_bytes()

# Build a substitution map by decoding the garbled bytes to a string
# and replacing with correct Chinese. All garbled text is "correct Chinese
# GBK bytes decoded as Latin-1 then re-encoded as UTF-8" mojibake.

replacements = {
    # Title & header
    "DEFLATE σÄïτ╝⌐σ¢₧µö╛": "DEFLATE 过程回放",
    "DEFLATE σÄïτ╝⌐σ¢₧µö╛Σ╗¬": "DEFLATE 过程回放仪",

    # Section names
    "LZ77 µ╗æσè¿τ¬ùσÅú": "LZ77 滑动窗口",
    "σôêσñ½µ¢╝µáæ": "哈夫曼树",

    # Status texts
    "τ¡ëσ╛àµò░µì«...": "等待数据...",
    "τ¡ëσ╛à BUILD_TREE Θÿ╢µ«╡...": "等待 BUILD_TREE 阶段...",
    "τ¡ëσ╛àΣ╕ïΣ╕Çσ¥ù...": "等待下一块...",
    "σà¿Θâ¿σ¥ùσ¢₧µö╛σ«îµ»ò": "全部块回放完毕",
    "µ£¬µ₧äσ╗║": "未构建",

    # Token display
    "(τ⌐║)": "(空)",

    # Huffman tree texts
    "Φèéτé╣": "节点",
    "σÅ╢σ¡É": "叶子",
    "σÅ╢": "叶",
    "σåà": "内",
    "∩╝î": "，",
    "ΘÖÉσê╢": "限制",
    "σåàΘâ¿Φèéτé╣": "内部节点",

    # Merge text fragments
    "σÉêσ╣╢": "合并",
    "σ░▒τ╗¬": "就绪",
    "Θ£Ç": "需",
    "µ¼íσÉêσ╣╢": "次合并",
    "σ╗║µáæσ«îµêÉ": "建树完成",
    "τ╗ºτ╗¡σ░åΦ┐¢σàÑΣ╕ïΣ╕Çσ¥ù": "继续将进入下一块",

    # Tree stats
    "µëÇ": "所",  # if needed
    "µùá Literal/Length τ╝ûτáü": "无 Literal/Length 编码",
    "µùá Distance τ╝ûτáü": "无 Distance 编码",

    # Tooltip
    "µáçΦ»å": "标识",
    "µ¥âΘçì": "权重",
    "τáüΦ╖»σ╛ä": "码路径",
    "Distance σÅ╢σ¡É": "Distance 叶子",
    "Lit/Len σÅ╢σ¡É": "Lit/Len 叶子",

    # Dot character for hex display
    "┬╖": "·",
}

text = raw.decode("utf-8")

for old, new in replacements.items():
    if old in text:
        text = text.replace(old, new)
        print(f"  Replaced: {old[:40]} -> {new[:40]}")
    else:
        print(f"  NOT FOUND: {old[:40]}")

html_path.write_text(text, encoding="utf-8")
print(f"\nDone. File size: {html_path.stat().st_size} bytes")