#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate an LVGL v9 C font from a TTF, covering the full GB2312 char set
(6763 Hanzi + symbols) plus printable ASCII, using only Pillow (freetype).

This is a stand-in for `lv_font_conv` (unavailable offline). Output layout
matches the SDK's existing generated fonts (uncompressed PLAIN bitmap,
designated-initializer structs, self-enabling #if guard).

Usage:
    python3 gen_simhei_gb2312.py <font.ttf> <px> <bpp> <out.c> <symbol> <macro>
"""
import sys, math
from PIL import Image, ImageDraw, ImageFont

def gb2312_codepoints():
    """All Unicode codepoints reachable through the GB2312 encoding."""
    cps = set()
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except (UnicodeDecodeError, LookupError):
                continue
            cps.add(ord(ch))
    return cps

def build_charset():
    cps = set(range(0x20, 0x7F))        # printable ASCII
    cps |= gb2312_codepoints()          # GB2312 hanzi + symbols
    cps.discard(0x7F)
    return sorted(cps)

def quantize(alpha, bpp):
    maxv = (1 << bpp) - 1
    return (alpha * maxv + 127) // 255

def pack_glyph(mask_px, w, h, bpp):
    """Row-major, bpp bits/pixel, MSB-first, continuous bitstream,
    padded to a byte boundary at the end of the glyph."""
    out = bytearray()
    acc = 0
    nbits = 0
    for y in range(h):
        for x in range(w):
            v = quantize(mask_px[y * w + x], bpp)
            acc = (acc << bpp) | v
            nbits += bpp
            while nbits >= 8:
                nbits -= 8
                out.append((acc >> nbits) & 0xFF)
    if nbits > 0:
        out.append((acc << (8 - nbits)) & 0xFF)
    return bytes(out)

def main():
    ttf, px, bpp, out_c, symbol, macro = (
        sys.argv[1], int(sys.argv[2]), int(sys.argv[3]),
        sys.argv[4], sys.argv[5], sys.argv[6])

    font = ImageFont.truetype(ttf, px)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent

    charset = build_charset()

    glyphs = []          # (cp, adv_w, box_w, box_h, ofs_x, ofs_y, bytes)
    bitmap = bytearray()
    canvas = px * 3
    for cp in charset:
        ch = chr(cp)
        adv = font.getlength(ch)
        adv_w = int(round(adv * 16))
        bbox = font.getbbox(ch)          # (l, t, r, b), origin top-left
        if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
            glyphs.append((cp, adv_w, 0, 0, 0, 0, b""))
            continue
        l, t, r, b = bbox
        box_w, box_h = r - l, b - t
        img = Image.new("L", (canvas, canvas), 0)
        d = ImageDraw.Draw(img)
        d.text((-l, -t), ch, font=font, fill=255)
        crop = img.crop((0, 0, box_w, box_h))
        px_data = list(crop.getdata())
        data = pack_glyph(px_data, box_w, box_h, bpp)
        ofs_x = l
        ofs_y = ascent - b
        glyphs.append((cp, adv_w, box_w, box_h, ofs_x, ofs_y, data))
        bitmap.extend(data)

    # assign bitmap indices
    idx = 0
    recs = []
    for (cp, adv_w, bw, bh, ox, oy, data) in glyphs:
        recs.append((cp, adv_w, bw, bh, ox, oy, idx))
        idx += len(data)

    range_start = charset[0]
    range_len = charset[-1] - range_start + 1

    with open(out_c, "w", encoding="utf-8") as f:
        w = f.write
        w("/*******************************************************************************\n")
        w(" * Generated offline (Pillow/freetype) as an lv_font_conv replacement.\n")
        w(" * Font: %s  Size: %dpx  Bpp: %d\n" % (ttf, px, bpp))
        w(" * Coverage: printable ASCII + full GB2312 (%d glyphs)\n" % len(charset))
        w(" ******************************************************************************/\n\n")
        w('#include "lvgl.h"\n\n')
        w("#ifndef %s\n#define %s 1\n#endif\n\n" % (macro, macro))
        w("#if %s\n\n" % macro)

        # bitmaps
        w("/*-----------------\n *    BITMAPS\n *----------------*/\n\n")
        w("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n")
        line = []
        for i, byte in enumerate(bitmap):
            line.append("0x%02x," % byte)
            if len(line) == 16:
                w("    " + "".join(line) + "\n")
                line = []
        if line:
            w("    " + "".join(line) + "\n")
        if not bitmap:
            w("    0x00\n")
        w("};\n\n")

        # glyph descriptors
        w("/*-----------------\n *    GLYPH DESCRIPTION\n *----------------*/\n\n")
        w("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n")
        w("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,\n")
        for (cp, adv_w, bw, bh, ox, oy, bi) in recs:
            w("    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},\n"
              % (bi, adv_w, bw, bh, ox, oy))
        w("};\n\n")

        # character map
        w("/*---------------------\n *  CHARACTER MAPPING\n *--------------------*/\n\n")
        w("static const uint16_t unicode_list_0[] = {\n")
        line = []
        for (cp, *_rest) in recs:
            line.append("0x%x," % (cp - range_start))
            if len(line) == 12:
                w("    " + "".join(line) + "\n")
                line = []
        if line:
            w("    " + "".join(line) + "\n")
        w("};\n\n")

        w("static const lv_font_fmt_txt_cmap_t cmaps[] =\n{\n")
        w("    {\n")
        w("        .range_start = %d, .range_length = %d, .glyph_id_start = 1,\n"
          % (range_start, range_len))
        w("        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, "
          ".list_length = %d, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n" % len(recs))
        w("    }\n};\n\n")

        # dsc + font
        w("/*--------------------\n *  ALL CUSTOM DATA\n *--------------------*/\n\n")
        w("#if LVGL_VERSION_MAJOR == 8\nstatic  lv_font_fmt_txt_glyph_cache_t cache;\n#endif\n\n")
        w("#if LVGL_VERSION_MAJOR >= 8\nstatic const lv_font_fmt_txt_dsc_t font_dsc = {\n"
          "#else\nstatic lv_font_fmt_txt_dsc_t font_dsc = {\n#endif\n")
        w("    .glyph_bitmap = glyph_bitmap,\n")
        w("    .glyph_dsc = glyph_dsc,\n")
        w("    .cmaps = cmaps,\n")
        w("    .kern_dsc = NULL,\n")
        w("    .kern_scale = 0,\n")
        w("    .cmap_num = 1,\n")
        w("    .bpp = %d,\n" % bpp)
        w("    .kern_classes = 0,\n")
        w("    .bitmap_format = 0,\n")
        w("#if LVGL_VERSION_MAJOR == 8\n    .cache = &cache\n#endif\n};\n\n")

        w("/*-----------------\n *  PUBLIC FONT\n *----------------*/\n\n")
        w("#if LVGL_VERSION_MAJOR >= 8\nconst lv_font_t %s = {\n"
          "#else\nlv_font_t %s = {\n#endif\n" % (symbol, symbol))
        w("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n")
        w("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n")
        w("    .line_height = %d,\n" % line_height)
        w("    .base_line = %d,\n" % base_line)
        w("#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)\n    .subpx = LV_FONT_SUBPX_NONE,\n#endif\n")
        w("#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8\n    .underline_position = -2,\n    .underline_thickness = 1,\n#endif\n")
        w("    .dsc = &font_dsc,\n")
        w("#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9\n    .fallback = NULL,\n#endif\n")
        w("    .user_data = NULL,\n};\n\n")
        w("#endif /*#if %s*/\n" % macro)

    print("glyphs=%d  bitmap_bytes=%d  line_height=%d base_line=%d ascent=%d descent=%d"
          % (len(charset), len(bitmap), line_height, base_line, ascent, descent))
    print("range_start=0x%x range_length=%d" % (range_start, range_len))

if __name__ == "__main__":
    main()
