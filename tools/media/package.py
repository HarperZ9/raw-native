#!/usr/bin/env python3
"""raw-native media package: cut a film from engine image sequences, screen captures and cards,
burn in captions and labels, lay the narration under it, and encode a 4K master with social cuts.

    python tools/media/package.py SPEC.json --out DIR [--only master,vertical,square] [--preview 0.25]

SPEC (JSON):
    fps, width, height                  the master (3840 x 2160 at 30 for the 4K master)
    fonts {sans, mono}, ground, ink, hot   type files and the three colours (ink on ground, one hot)
    narration                           a wav, laid at time 0 (optional)
    captions {timing, offset}           narration timing rows {start, end, text} (optional)
    clips [{start, end, kind, focus, ...}]   kind seq: dir, fps, from; still: path, rect or from/to
                                        ([x, y, w, h] in source pixels), rects {output: rect} for a
                                        hand-placed crop in one cut, fit {output: rect} to show a
                                        page region whole on the ground; card: lines. Overlapping clips
                                        cross-fade. focus [fx, fy] is the subject in the 16:9 frame,
                                        used to place the narrower cuts. caption_top moves captions up.
    labels [{start, end, text, at [fx, fy] | track {file, group, fps, clip_start}, hot}]
    outputs [{name, width, height}]     cuts: each crops the 16:9 frame around the clip's focus,
                                        then draws captions and labels at its own size.
Frames are composed with Pillow and piped to ffmpeg (libx264, yuv420p, BT.709). A research tool: it
ships in no product. ffmpeg comes from PATH, $FFMPEG or imageio-ffmpeg.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
from functools import lru_cache
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_text as T  # noqa: E402


def ffmpeg() -> str:
    exe = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError as e:
        raise SystemExit(f"package: no ffmpeg on PATH, in $FFMPEG or from imageio-ffmpeg ({e})")


def ease(u: float) -> float:
    u = min(1.0, max(0.0, u))
    return u * u * (3 - 2 * u)


@lru_cache(maxsize=32)
def seq_files(d: str) -> list[Path]:
    return sorted(Path(d).glob("f*.png"))


@lru_cache(maxsize=6)   # stills only, a few 4K images; sequence frames are read once and dropped
def load(path: str) -> Image.Image:
    return Image.open(path).convert("RGB")


def read(path: str) -> Image.Image:
    with Image.open(path) as im:
        return im.convert("RGB")


def view_box(img: Image.Image, rect, master: float, aspect: float, fx: float):
    """The source box for one output: rect widened to the master's aspect around its centre, then,
    for a narrower output, a slice of that around the focus fx (a fraction of the master frame)."""
    x, y, w, h = rect or (0, 0, img.width, img.height)
    cx, cy = x + w / 2, y + h / 2
    if w / h > master:
        h = w / master
    else:
        w = h * master
    k = min(1.0, img.width / w, img.height / h)   # shrink, keeping the aspect, to fit the source
    w, h = w * k, h * k
    x0, y0 = min(max(0, cx - w / 2), img.width - w), min(max(0, cy - h / 2), img.height - h)
    if aspect < master - 1e-3:
        cw = h * aspect
        x0 = min(max(x0, x0 + fx * w - cw / 2), x0 + w - cw)
        w = cw
    return (max(0.0, x0), max(0.0, y0), min(float(img.width), x0 + w), min(float(img.height), y0 + h))


def fit(src: Image.Image, rect, W: int, H: int, spec: dict) -> Image.Image:
    """rect shown whole inside W x H on the ground colour, with a margin: for pages in narrow cuts."""
    x, y = max(0, rect[0]), max(0, rect[1])
    w, h = min(rect[2], src.width - x), min(rect[3], src.height - y)
    k = min(W * 0.94 / w, H * 0.80 / h)
    part = src.resize((max(1, int(w * k)), max(1, int(h * k))), Image.LANCZOS, box=(x, y, x + w, y + h))
    img = Image.new("RGB", (W, H), T.rgb(spec["ground"]))
    img.paste(part, ((W - part.width) // 2, int((H * 0.86 - part.height) / 2)))
    return img


def clip_frame(c: dict, t: float, out: dict, spec: dict):
    """One clip at time t for one output: (image, source box, source size); box None for a card."""
    W, H, local = out["width"], out["height"], t - c["start"]
    master, aspect, fx = spec["width"] / spec["height"], W / H, c.get("focus", [0.5, 0.5])[0]
    if c["kind"] == "card":
        img = Image.new("RGB", (W, H), T.rgb(spec["ground"]))
        T.draw_card(img, c["lines"], spec, min(W, H) if aspect < master - 1e-3 else H)
        return img, None, None
    if c["kind"] == "seq":
        files = seq_files(c["dir"])
        src, rect = read(str(files[min(len(files) - 1, max(0, int((local + c.get("from", 0)) * c.get("fps", 12))))])), None
    else:
        src = load(c["path"])
        if out["name"] in c.get("fit", {}):
            return fit(src, c["fit"][out["name"]], W, H, spec), None, None
        if out["name"] in c.get("rects", {}):
            rect, master = c["rects"][out["name"]], aspect   # a hand-placed crop for this output
        elif "from" in c:
            u = ease(local / max(1e-6, c["end"] - c["start"]))
            rect = [p + (q - p) * u for p, q in zip(c["from"], c["to"])]
        else:
            rect = c.get("rect")
    box = view_box(src, rect, master, aspect, fx)
    return src.resize((W, H), Image.LANCZOS, box=box), box, src.size


def compose(spec: dict, t: float, out: dict):
    """The picture for one output at time t (no type), with the primary clip's box for labels."""
    live = [c for c in spec["clips"] if c["start"] <= t < c["end"]]
    if not live:
        return Image.new("RGB", (out["width"], out["height"]), T.rgb(spec["ground"])), None, False
    img, box, size = clip_frame(live[0], t, out, spec)
    view, top = (box, size), live[0].get("caption_top", False)
    for c in live[1:]:
        a = ease((t - c["start"]) / spec.get("fade", 0.5))
        nxt, b2, s2 = clip_frame(c, t, out, spec)
        img = Image.blend(img, nxt, a)
        if a > 0.5:
            view, top = (b2, s2), c.get("caption_top", False)
    return img, view, top


@lru_cache(maxsize=16)
def track(path: str) -> list[dict]:
    return [json.loads(l) for l in Path(path).read_text(encoding="utf-8").splitlines() if l.strip()]


def label_point(lb: dict, t: float):
    if "at" in lb:
        return lb["at"]
    tr = lb["track"]
    rows = track(tr["file"])
    i = min(len(rows) - 1, max(0, int((t - tr["clip_start"]) * tr.get("fps", 12))))
    p = rows[i][tr["group"]]
    return p if p[0] >= 0 else None


def dress(spec: dict, out: dict, t: float, caps: list[dict]) -> Image.Image:
    img, view, top = compose(spec, t, out)
    W, H = out["width"], out["height"]
    scale = min(W, H) if W / H < spec["width"] / spec["height"] - 1e-3 else H
    box, size = view if view else (None, None)
    for lb in spec.get("labels", []) if box else []:
        p = label_point(lb, t) if lb["start"] <= t < lb["end"] else None
        if not p:
            continue
        x = (p[0] * size[0] - box[0]) / (box[2] - box[0]) * W
        y = (p[1] * size[1] - box[1]) / (box[3] - box[1]) * H
        if 0 <= x <= W and 0 <= y <= H:
            a = min(ease((t - lb["start"]) / 0.35), ease((lb["end"] - t) / 0.35))
            T.draw_label(img, x, y, lb["text"], spec, scale, lb.get("hot", False), a)
    for c in caps:
        if c["start"] <= t < c["end"]:
            T.draw_caption(img, c["lines"], spec, scale, top)
            break
    return img


def encoder(path: Path, out: dict, spec: dict, audio: str | None) -> subprocess.Popen:
    cmd = [ffmpeg(), "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{out['width']}x{out['height']}",
           "-r", str(spec["fps"]), "-i", "-"]
    if audio:
        cmd += ["-i", audio, "-c:a", "aac", "-b:a", "256k", "-shortest"]
    cmd += ["-c:v", "libx264", "-preset", out.get("preset", "medium"), "-crf", str(out.get("crf", 17)), "-pix_fmt", "yuv420p",
            "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-movflags", "+faststart", str(path)]
    return subprocess.Popen(cmd, stdin=subprocess.PIPE)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--preview", type=float, default=1.0, help="scale every output, for quick drafts")
    a = ap.parse_args()
    spec = json.loads(a.spec.read_text(encoding="utf-8"))
    a.out.mkdir(parents=True, exist_ok=True)
    caps = T.cues(json.loads(Path(spec["captions"]["timing"]).read_text(encoding="utf-8")), spec["captions"].get("offset", 0)) if spec.get("captions") else []
    outs = [dict(o, width=2 * round(o["width"] * a.preview / 2), height=2 * round(o["height"] * a.preview / 2)) for o in spec["outputs"]
            if not a.only or o["name"] in a.only.split(",")]
    if a.preview != 1.0:
        spec = dict(spec, width=2 * round(spec["width"] * a.preview / 2), height=2 * round(spec["height"] * a.preview / 2))
    end = max(c["end"] for c in spec["clips"])
    procs = {o["name"]: encoder(a.out / f"{spec.get('name', 'film')}-{o['name']}.mp4", o, spec, spec.get("narration")) for o in outs}
    n = math.ceil(end * spec["fps"])
    for i in range(n):
        t = i / spec["fps"]
        for o in outs:
            procs[o["name"]].stdin.write(dress(spec, o, t, caps).tobytes())
        if i % 150 == 0:
            print(f"package: frame {i}/{n}", flush=True)
    bad = 0
    for name, p in procs.items():
        p.stdin.close()
        bad |= p.wait() != 0
        print(f"package: {name} {'failed' if p.returncode else 'written'}")
    (a.out / "captions.json").write_text(json.dumps(caps, indent=1), encoding="utf-8")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
