#!/usr/bin/env python3
"""Lay the guide renders out on one contact sheet: a row per guide, then the lesson still.

    python tools/guides/contact_sheet.py DIR OUT.png [r2|r3]

Row order: turnaround sheet, home-style scene, the two other template styles. Assembly only
(Pillow); every tile is a raw-native render from tools/guides/render_all.py.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

LAYOUTS = {
    "r2": [
        ("Tallow", ["tallow-sheet", "tallow-thriller", "tallow-painted", "tallow-pixel"]),
        ("Pylon", ["pylon-sheet", "pylon-painted", "pylon-thriller", "pylon-pixel"]),
        ("Rerun", ["rerun-sheet", "rerun-pixel", "rerun-thriller", "rerun-painted"]),
        ("Lesson", ["lesson-night-shift"]),
    ],
    # Round 3: each guide's home world in three framings, then the other two styles.
    "r3": [
        ("Tallow", ["tallow-sheet", "tallow-thriller", "tallow-thriller-medium", "tallow-thriller-close", "tallow-painted", "tallow-pixel"]),
        ("Pylon", ["pylon-sheet", "pylon-painted", "pylon-painted-medium", "pylon-painted-close", "pylon-thriller", "pylon-pixel"]),
        ("Rerun", ["rerun-sheet", "rerun-pixel", "rerun-pixel-medium", "rerun-pixel-close", "rerun-thriller", "rerun-painted"]),
        ("Lesson", ["lesson-night-shift"]),
    ],
}
TW, TH, PAD, CAP = 640, 360, 16, 30


def main() -> int:
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    rows = LAYOUTS[sys.argv[3] if len(sys.argv) > 3 else "r2"]
    cols = max(len(n) for _, n in rows)
    W = PAD + cols * (TW + PAD)
    H = PAD + len(rows) * (TH + CAP + PAD)
    sheet = Image.new("RGB", (W, H), (14, 13, 15))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("arial.ttf", 18)
    except OSError:
        font = ImageFont.load_default()
    for r, (_, names) in enumerate(rows):
        for c, name in enumerate(names):
            x, y = PAD + c * (TW + PAD), PAD + r * (TH + CAP + PAD)
            im = Image.open(src / f"{name}.png").convert("RGB").resize((TW, TH), Image.LANCZOS)
            sheet.paste(im, (x, y))
            d.text((x, y + TH + 6), name, fill=(214, 204, 182), font=font)
    sheet.save(out)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
