"""
Component-level semantic token mappings.

Each key is a semantic token that widget code references.
Each value is a primitive Theme dataclass field name.

Users can override these mappings in config JSON under ``theme_mappings``
to scope different components to different colours.
"""

DEFAULT_COMPONENT_MAPPINGS: dict[str, str] = {
    # ── Button ──
    "button_bg": "accent",
    "button_hover_bg": "accent_hover",
    "button_text": "accent_text",
    "button_disabled_bg": "bg_surface",
    "button_disabled_text": "text_muted",

    # ── ProgressBar ──
    "progressbar_bg": "bg_surface",
    "progressbar_chunk": "accent",

    # ── Link ──
    "link_color": "accent",
    "link_hover": "accent_hover",

    # ── Scrollbar ──
    "scrollbar_bg": "bg_surface",
    "scrollbar_handle": "border_dark",
    "scrollbar_handle_hover": "text_muted",

    # ── Canvas (LZ77 window) ──
    "canvas_search_bg": "bg_surface",
    "canvas_lookahead_bg": "bg_primary",
    "canvas_match_bg": "success",
    "canvas_literal_bg": "text_muted",
    "canvas_cursor_line": "error",
    "canvas_highlight": "warning",
    "canvas_text": "text_primary",
    "canvas_dim_text": "text_muted",
    "canvas_header_bg": "bg_elevated",
    "canvas_border": "border_dark",

    # ── Huffman chart ──
    "huffman_type0": "accent",
    "huffman_type1": "success",
    "huffman_type2": "warning",
    "huffman_type3": "error",
    "huffman_bg": "bg_primary",
    "huffman_placeholder": "text_muted",

    # ── Network sim ──
    "net_badge_good_bg": "success",
    "net_badge_good_fg": "success",
    "net_badge_bad_bg": "error",
    "net_badge_bad_fg": "error",
    "net_bar_raw": "error",
    "net_bar_compressed": "success",
    "net_saving_value": "success",

    # ── Heatmap ──
    "heatmap_low": "success",
    "heatmap_mid": "warning",
    "heatmap_high": "error",

    # ── Tier-3 slider ──
    "tier3_slider_fill": "warning",
    "tier3_slider_stroke": "warning",
    "tier3_slider_pin": "warning",
    "tier3_pin_shadow": "border",

    # ── Semantic status colours ──
    "status_error": "error",
    "status_success": "success",
    "status_warning": "warning",
    "status_folder": "text_muted",
}
