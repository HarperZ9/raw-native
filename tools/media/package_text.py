"""Type for media packages: burned-in captions and on-picture labels (tools/media/package.py).

Two faces only, from the spec's font files (Hanken Grotesk for reading, Conso for figures), ink on
a calm ground, one hot mark at a time. Caption cues follow the site's rule: at most two lines of at
most 42 characters, no one- or two-word orphan line.
"""

from __future__ import annotations

from functools import lru_cache

from PIL import ImageDraw, ImageFont

WIDTH, LINES, MIN_WORDS = 42, 2, 3


def rgb(hexstr: str) -> tuple[int, int, int]:
    h = hexstr.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


@lru_cache(maxsize=64)
def font(path: str, size: int, weight: int | None = None) -> ImageFont.FreeTypeFont:
    f = ImageFont.truetype(path, size)
    if weight is not None:
        try:
            f.set_variation_by_axes([weight])
        except (OSError, AttributeError) as e:  # a static face has no weight axis
            print(f"package_text: {path} has no weight axis ({e}); using its own weight")
    return f


def wrap(text: str, width: int = WIDTH) -> list[str]:
    """Greedy lines of at most `width` characters, then pull words back so no line is an orphan."""
    lines: list[list[str]] = [[]]
    for w in text.split():
        if lines[-1] and len(" ".join(lines[-1] + [w])) > width:
            lines.append([])
        lines[-1].append(w)
    for i in range(len(lines) - 1, 0, -1):
        while len(lines[i]) < MIN_WORDS and len(lines[i - 1]) > MIN_WORDS and len(" ".join([lines[i - 1][-1]] + lines[i])) <= width:
            lines[i].insert(0, lines[i - 1].pop())
    return [" ".join(x) for x in lines if x]


def cues(rows: list[dict], offset: float = 0.0) -> list[dict]:
    """Caption cues from narration timing rows ({start, end, text}): each sentence split into cues
    of at most two lines, timed in proportion to their characters."""
    out = []
    for r in rows:
        lines = wrap(r["text"])
        groups = [lines[i:i + LINES] for i in range(0, len(lines), LINES)]
        total = sum(len(" ".join(g)) for g in groups)
        t = r["start"] + offset
        for g in groups:
            d = (r["end"] - r["start"]) * len(" ".join(g)) / total
            out.append({"start": t, "end": t + d, "lines": g})
            t += d
    return out


def draw_caption(img, cue_lines: list[str], spec: dict, scale: float, top: bool = False) -> None:
    """Ink type on a ground band, centred low in the frame (or high, when the picture needs the foot)."""
    W, H = img.size
    f = font(spec["fonts"]["sans"], max(12, int(spec.get("caption_size", 0.034) * scale)), 560)
    d = ImageDraw.Draw(img, "RGBA")
    lh = int(f.size * 1.32)
    widths = [d.textlength(l, font=f) for l in cue_lines]
    pad_x, pad_y = int(f.size * 0.6), int(f.size * 0.35)
    bw, bh = int(max(widths)) + 2 * pad_x, lh * len(cue_lines) + 2 * pad_y
    x0 = (W - bw) // 2
    if top:
        y0 = int(spec.get("caption_bottom", 0.075) * scale)
        y1 = y0 + bh
    else:
        y1 = H - int(spec.get("caption_bottom", 0.075) * scale)
        y0 = y1 - bh
    d.rectangle([x0, y0, x0 + bw, y1], fill=rgb(spec["ground"]) + (246,))
    for i, l in enumerate(cue_lines):
        d.text(((W - widths[i]) / 2, y0 + pad_y + i * lh), l, font=f, fill=rgb(spec["ink"]))


def draw_label(img, x: float, y: float, text: str, spec: dict, scale: float, hot: bool, alpha: float) -> None:
    """A short label with a leader to its point (x, y in output pixels). Hot labels take the one
    verdict colour of the view; the rest are ink."""
    if alpha <= 0:
        return
    W, H = img.size
    f = font(spec["fonts"]["sans"], max(11, int(spec.get("label_size", 0.026) * scale)), 650)
    d = ImageDraw.Draw(img, "RGBA")
    colour = rgb(spec["hot"] if hot else spec["ink"]) + (int(255 * alpha),)
    ground = rgb(spec["ground"]) + (int(225 * alpha),)
    tw, th = d.textlength(text, font=f), f.size
    lead = int(0.07 * scale)
    tx = min(max(8, x - tw / 2), W - tw - 8)
    ty = max(8, y - lead - th * 1.6)
    r = max(3, int(0.006 * scale))
    d.line([x, y - r, x, ty + th * 1.5], fill=colour, width=max(2, int(0.0022 * scale)))
    d.ellipse([x - r, y - r, x + r, y + r], fill=colour)
    pad = int(th * 0.35)
    d.rectangle([tx - pad, ty - pad * 0.6, tx + tw + pad, ty + th * 1.25], fill=ground)
    d.text((tx, ty), text, font=f, fill=colour)


def frame_ticks(d, W: int, H: int, spec: dict, scale: float, alpha: float) -> None:
    """The site's corner registration ticks (harperz9.github.io), drawn in a quiet ink."""
    m, L, w = int(0.03 * scale), int(0.03 * scale), max(1, int(0.0016 * scale))
    c = rgb(spec["ink"]) + (int(110 * alpha),)
    for x, y, sx, sy in ((m, m, 1, 1), (W - m, m, -1, 1), (m, H - m, 1, -1), (W - m, H - m, -1, -1)):
        d.line([x, y, x + sx * L, y], fill=c, width=w)
        d.line([x, y, x, y + sy * L], fill=c, width=w)


def draw_card(img, lines: list[dict], spec: dict, scale: float, alpha: float = 1.0) -> None:
    """A title card in the site's editorial layout: paper ground, corner ticks, a left-aligned
    heading with a hairline rule under it, then the remaining lines. A card whose lines are all
    mono is a terminal or file excerpt: one left edge, block centred. Each line is
    {text, size, weight, mono, hot}."""
    W, H = img.size
    d = ImageDraw.Draw(img, "RGBA")
    d.rectangle([0, 0, W, H], fill=rgb(spec["ground"]) + (int(255 * alpha),))
    frame_ticks(d, W, H, spec, scale, alpha)
    code = all(l.get("mono") for l in lines)
    room = W * (0.84 if not code else 0.92)

    def faces(k):
        return [font(spec["fonts"]["mono" if l.get("mono") else "sans"], max(12, int(l.get("size", 0.06) * scale * k)), None if l.get("mono") else l.get("weight", 700)) for l in lines]
    fonts = faces(1.0)
    widest = max(d.textlength(l["text"], font=f) for l, f in zip(lines, fonts))
    if widest > room:   # a narrow cut: shrink the whole card so its longest line fits
        fonts = faces(room / widest)
        widest = room
    gaps = [int(f.size * 1.3) for f in fonts]
    rule_gap = int(fonts[0].size * 0.55) if not code and len(lines) > 1 and fonts[0].size > 1.2 * fonts[1].size else 0   # a title over smaller lines
    y = (H - sum(gaps) - rule_gap) / 2
    left = (W - widest) / 2 if code else W * 0.08
    for i, (l, f, g) in enumerate(zip(lines, fonts, gaps)):
        d.text((left, y), l["text"], font=f, fill=rgb(spec["hot"] if l.get("hot") else spec["ink"]) + (int(255 * alpha),))
        y += g
        if i == 0 and rule_gap:
            ry = y + rule_gap * 0.25
            d.line([left, ry, W - W * 0.08, ry], fill=rgb(spec["ink"]) + (int(70 * alpha),), width=max(1, int(0.0016 * scale)))
            y += rule_gap
