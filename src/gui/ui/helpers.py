"""Small UI helpers: styled labels and encoding preview text for algorithm config."""

from __future__ import annotations

from PyQt6.QtWidgets import QLabel

from gui.config.theme import ThemeManager


def create_bold_label(text: str) -> QLabel:
    label = QLabel(text)
    label.setStyleSheet(f"font-weight: bold; color: {ThemeManager.hex('text_primary')}")
    return label


def create_italic_label(text: str) -> QLabel:
    label = QLabel(text)
    label.setStyleSheet(f"font-style: italic; color: {ThemeManager.hex('text_primary')}")
    return label


def create_styled_label(
    text: str,
    color_token: str | None = None,
    font_size: int | None = None,
    bold: bool = False,
) -> QLabel:
    label = QLabel(text)
    style_parts = []
    if color_token:
        style_parts.append(f"color: {ThemeManager.hex(color_token)}")
    if font_size:
        style_parts.append(f"font-size: {font_size}px")
    if bold:
        style_parts.append("font-weight: bold")
    if style_parts:
        label.setStyleSheet("; ".join(style_parts))
    return label


def set_label_html_free(
    label: QLabel,
    text: str,
    color_token: str | None = None,
    font_size: int | None = None,
) -> None:
    label.setText(text)
    style_parts = []
    if color_token:
        style_parts.append(f"color: {ThemeManager.hex(color_token)}")
    if font_size:
        style_parts.append(f"font-size: {font_size}px")
    if style_parts:
        current = label.styleSheet() or ""
        label.setStyleSheet(current + ("; " if current else "") + "; ".join(style_parts))


def create_color_dot(color_token: str, text: str = "●") -> QLabel:
    label = QLabel(text)
    label.setStyleSheet(f"color: {ThemeManager.hex(color_token)}; font-size: 14px;")
    return label


def calc_bit_width(max_val: int) -> int:
    if max_val <= 0:
        return 1
    bits = 0
    v = max_val
    while v > 0:
        bits += 1
        v >>= 1
    return bits


def lzdp_effective_min_match(offset_bits: int, length_bits: int, min_match_param: int) -> int:
    if min_match_param == 0:
        match_bits = offset_bits + length_bits
        return match_bits // 8 + 1
    return min_match_param


def format_lzdp_preview(
    search_size: int,
    lookahead_size: int,
    min_match_param: int,
    use_flag_encoding: bool,
) -> str:
    ob = calc_bit_width(search_size)
    lb = calc_bit_width(lookahead_size)
    eff_mm = lzdp_effective_min_match(ob, lb, min_match_param)
    max_chunk = (1 << lb) - 1
    lines = [
        f"offset 字段 = {ob} bit（由搜索窗口 {search_size} 推导）",
        f"length 字段 = {lb} bit（由前瞻窗口 {lookahead_size} 推导）",
        f"有效最小匹配 min_match = {eff_mm}"
        + (
            "（参数为 0 时按 (offset_bits+length_bits)/8+1）"
            if min_match_param == 0
            else f"（参数 = {min_match_param}）"
        ),
        "帧头固定 2 字节：offset_bits、length_bits 及编码标志位。",
        "",
    ]
    if use_flag_encoding:
        lit_bits = 1 + 8
        mat_bits = 1 + ob + lb
        lines += [
            "当前：1-Bit Flag 模式（与 core LZDP 一致）",
            f"  · 字面量: 1 + 8 = {lit_bits} bit",
            f"  · 引用匹配: 1 + {ob} + {lb} = {mat_bits} bit",
        ]
    else:
        lines += [
            "当前：Offset=0 兜底模式（与 core LZDP 一致）",
            f"  · 引用匹配（offset>0）: {ob} + {lb} = {ob + lb} bit",
            f"  · 连续字面量: 每段 {ob}+{lb}+8×chunk bit，单段最多 {max_chunk} 字节字面量",
        ]
    return "\n".join(lines)


def format_lzss_preview(search_size: int, lookahead_size: int, min_match_param: int, use_flag_encoding: bool) -> str:
    ob = calc_bit_width(search_size)
    lb = calc_bit_width(lookahead_size)
    eff_mm = 3 if min_match_param == 0 else min_match_param
    max_run = eff_mm + 15
    max_chunk = (1 << lb) - 1
    lines = [
        f"offset 字段 ≈ {ob} bit（由搜索窗口 {search_size} 推导）",
        f"length 字段 ≈ {lb} bit（由前瞻窗口 {lookahead_size} 推导）",
        f"有效最小匹配 min_match = {eff_mm}"
        + (
            "（参数为 0 时引擎内固定为 3）"
            if min_match_param == 0
            else f"（参数 = {min_match_param}）"
        ),
        "说明：当前 LZSS 的压缩格式已真正接入并与 LZDP 类似，可通过编码方案控制。",
        "",
    ]
    if use_flag_encoding:
        lit_bits = 1 + 8
        mat_bits = 1 + ob + lb
        lines += [
            "当前：1-Bit Flag 模式（与 core LZDP 类似设计）",
            f"  · 字面量: 1 bit（flag=1）+ 8 bit 数据 = 9 bit",
            f"  · 匹配: 1 bit（flag=0）+ 16 bit（offset 12 bit | length- min_match 4 bit）= 17 bit",
        ]
    else:
        lines += [
            "当前：Offset=0 兜底模式（与 core LZDP 类似设计）",
            f"  · 引用匹配（offset>0）: 16 bit（offset 12 bit | length- min_match 4 bit）= 16 bit",
            f"  · 连续字面量: 每段 16 bit 头 + 8×chunk bit，单段最多 15 字节字面量",
        ]
    return "\n".join(lines)


def format_dpflate_lz_reference_preview(
    search_size: int,
    lookahead_size: int,
    min_match_param: int,
    use_flag_encoding: bool,
) -> str:
    ob = calc_bit_width(search_size)
    lb = calc_bit_width(lookahead_size)
    eff_mm = 4 if min_match_param == 0 else min_match_param
    max_chunk = (1 << lb) - 1
    lines = [
        f"offset 字段代价 = {ob} bit（由搜索窗口 {search_size} 推导）",
        f"length 字段代价 = {lb} bit（由前瞻窗口 {lookahead_size} 推导）",
        f"有效最小匹配 min_match = {eff_mm}"
        + (
            "（参数为 0 时引擎内固定为 4）"
            if min_match_param == 0
            else f"（参数 = {min_match_param}）"
        ),
        "说明：DPFlate 输出为 Deflate 风格（Huffman变长），此选项真正控制其 DP 阶段的代价评估策略。",
        "",
    ]
    if use_flag_encoding:
        lit_bits = 1 + 8
        mat_bits = 1 + ob + lb
        lines += [
            "当前：1-Bit Flag 模式代价评估（与 core LZDP 一致）",
            f"  · 字面量代价: 1 + 8 = {lit_bits} bit",
            f"  · 引用匹配代价: 1 + {ob} + {lb} = {mat_bits} bit",
        ]
    else:
        lines += [
            "当前：Offset=0 兜底模式代价评估（与 core LZDP 一致）",
            f"  · 引用匹配代价: {ob} + {lb} = {ob + lb} bit",
            f"  · 连续字面量代价: 基础 {ob}+{lb} bit + 8×chunk bit，单段最多 {max_chunk} 字节",
        ]
    return "\n".join(lines)


def format_deflate_preview(
    search_size: int,
    lookahead_size: int,
    min_match_param: int,
    use_flag_encoding: bool,
    use_3hfmtree: bool,
    huffman_offset_chunk_bits: int,
    huffman_length_chunk_bits: int,
) -> str:
    """LZ 阶段位宽与编码方案（与 greedy LZ77 代价一致）；Huffman 侧区分标准两树 / 3HfM。"""
    ob = calc_bit_width(search_size)
    lb = calc_bit_width(lookahead_size)
    eff_mm = 3 if min_match_param == 0 else min_match_param
    max_chunk = (1 << lb) - 1
    lines = [
        f"offset 字段 ≈ {ob} bit（搜索窗口 {search_size}）",
        f"length 字段 ≈ {lb} bit（前瞻窗口 {lookahead_size}）",
        f"有效最小匹配 min_match = {eff_mm}"
        + (
            "（参数为 0 时引擎内默认 3）"
            if min_match_param == 0
            else f"（参数 = {min_match_param}）"
        ),
        "",
    ]
    if use_flag_encoding:
        lit_bits = 1 + 8
        mat_bits = 1 + ob + lb
        lines += [
            "当前：1-Bit Flag 模式（LZ 符号层，与 core Deflate 一致）",
            f"  · 字面量: 1 + 8 = {lit_bits} bit",
            f"  · 匹配: 1 + {ob} + {lb} = {mat_bits} bit",
        ]
    else:
        lines += [
            "当前：Offset=0 兜底模式（LZ 符号层）",
            f"  · 匹配（offset>0）: {ob} + {lb} = {ob + lb} bit",
            f"  · 连续字面量: 每段 {ob}+{lb}+8×chunk bit，单段最多 {max_chunk} 字节字面量",
        ]
    lines += ["", "Huffman 输出层："]
    if use_3hfmtree:
        lines += [
            "  · 策略：3HfMTree（整块内存压缩走 DPFlate 内核；见「Huffman 树策略」说明）",
            f"  · 多级槽：offset 槽宽 {huffman_offset_chunk_bits} bit，length 槽宽 {huffman_length_chunk_bits} bit",
            "  · 解压路径需与编码一致（非标准两树 Inflate 时勿用标准 Inflate 解压）",
        ]
    else:
        lines += [
            "  · 策略：标准 FLATE（字面量/长度一树 + 距离一树，与 Inflate 解压兼容）",
            "  · 变长码长由块内频率自适应，预览不估算具体码字长度",
        ]
    return "\n".join(lines)
