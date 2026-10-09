#!/usr/bin/env python3
"""Write a glyph atlas for web/motion/text.mjs from a TrueType, OpenType or WOFF font.

    python scripts/glyph_atlas.py FONT OUT.json [--weight 600] [--chars "..."]

The atlas holds, for each character, the glyph outline as SVG path data in
font units (y up, as the font draws it) and its advance, plus the pair
kerning the font's own shaping gives (through HarfBuzz when uharfbuzz is
installed; without it the atlas has no kerning and says so). A variable font
is instanced at --weight first. Only use fonts whose licence lets you
redistribute derived outlines (the SIL OFL does, with its notice kept).
Needs fontTools (and brotli for WOFF2).
"""

from __future__ import annotations

import argparse
import io
import json
import string
import sys
from pathlib import Path

DEFAULT_CHARS = string.ascii_letters + string.digits + " .,:;!?'\"()[]{}-+=/%&*#@<>_$|~^`\\" + \
    "×÷≈·→↑≤≥−–‘’“”±∞" + \
    "αβγδθλμπστφωΔΣ√"


def load(path: str, weight: float | None):
    from fontTools.ttLib import TTFont
    font = TTFont(path)
    if "fvar" in font and weight is not None:
        from fontTools.varLib import instancer
        font = instancer.instantiateVariableFont(font, {"wght": weight})
    font.flavor = None
    return font


def outline(font, gname: str) -> str:
    from fontTools.pens.svgPathPen import SVGPathPen
    gs = font.getGlyphSet()
    pen = SVGPathPen(gs, lambda v: f"{v:.1f}".rstrip("0").rstrip("."))
    gs[gname].draw(pen)
    return pen.getCommands()


def kerning(font, chars: str) -> tuple[dict, str]:
    try:
        import uharfbuzz as hb
    except ImportError:
        return {}, "none (uharfbuzz not installed)"
    buf = io.BytesIO()
    font.save(buf)
    face = hb.Face(buf.getvalue())
    hfont = hb.Font(face)
    cmap = font.getBestCmap()
    letters = [c for c in chars if ord(c) in cmap and not c.isspace()]

    def adv(s: str) -> int:
        b = hb.Buffer()
        b.add_str(s)
        b.guess_segment_properties()
        hb.shape(hfont, b, {"kern": True, "liga": False})
        return sum(p.x_advance for p in b.glyph_positions)

    single = {c: adv(c) for c in letters}
    out = {}
    for a in letters:
        for b in letters:
            k = adv(a + b) - single[a] - single[b]
            if k:
                out[a + b] = k
    return out, "harfbuzz pair shaping"


def build(path: str, weight: float | None, chars: str) -> dict:
    font = load(path, weight)
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    glyphs = {}
    for ch in dict.fromkeys(chars):
        g = cmap.get(ord(ch))
        if g is None:
            continue
        glyphs[ch] = {"adv": hmtx[g][0], "d": outline(font, g)}
    kern, how = kerning(font, chars)
    name = font["name"]
    return {
        "schema": "raw-native.glyph-atlas/1",
        "family": name.getDebugName(1) or Path(path).stem,
        "weight": weight,
        "copyright": name.getDebugName(0) or "",
        "license": name.getDebugName(14) or name.getDebugName(13) or "",
        "unitsPerEm": font["head"].unitsPerEm,
        "ascender": font["hhea"].ascent,
        "descender": font["hhea"].descent,
        "kerning": how,
        "glyphs": glyphs,
        "kern": kern,
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("font")
    ap.add_argument("out")
    ap.add_argument("--weight", type=float, default=None)
    ap.add_argument("--chars", default=DEFAULT_CHARS)
    a = ap.parse_args(argv)
    atlas = build(a.font, a.weight, a.chars)
    Path(a.out).write_text(json.dumps(atlas, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(f"{a.out}: {len(atlas['glyphs'])} glyphs, {len(atlas['kern'])} kerning pairs ({atlas['kerning']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
