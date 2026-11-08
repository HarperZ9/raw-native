#!/usr/bin/env python3
"""Finish the held-time loops (evidence/heldtime-loop-bounds.json B2, B5, B6): MP4 and offline checks.

Reads the run JSON that `raw_native_cli held-time --raw DIR` writes. For every loop with raw RGB:
  B6  writes <gif stem>.mp4 (H.264, yuv420p, whole-number nearest-neighbour scale: 2 below 960
      wide, else 1) through an external ffmpeg, decodes it back, and reports frame count and PSNR
      against the source repeated k x k.
  B5  decodes the lossless WebP with Pillow and compares every frame to the source exactly.
  B2  decodes every palette GIF with Pillow and counts pixels whose colour is not in its palette.
Pillow and numpy are offline checkers only, not dependencies. ffmpeg is never linked or shipped (ADR 0005): pass --ffmpeg, or have ffmpeg on PATH, or the
imageio-ffmpeg binary CI already installs.

Usage: loop_finish.py RUN_JSON [--out RESULTS_JSON] [--ffmpeg EXE] [--keep-raw]
Exit 0 when every check holds, 1 otherwise.
"""
import argparse
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np


def find_ffmpeg(arg):
    if arg:
        return arg
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        sys.exit("ffmpeg not found: pass --ffmpeg")


def upscale(frame, w, h, k):
    a = np.frombuffer(frame, np.uint8).reshape(h, w, 3)
    return a if k == 1 else a.repeat(k, axis=0).repeat(k, axis=1)


def frames_of(raw_path, w, h, n):
    data = Path(raw_path).read_bytes()
    size = w * h * 3
    if len(data) != size * n:
        raise ValueError(f"{raw_path}: {len(data)} bytes, expected {size * n}")
    return [data[i * size:(i + 1) * size] for i in range(n)]


def mp4(ffmpeg, loop, frames, k):
    out = str(Path(loop["gif"]).with_suffix(".mp4"))
    w, h = loop["w"], loop["h"]
    enc = [ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{w}x{h}", "-r", str(loop["fps"]),
           "-i", "-", "-vf", f"scale=iw*{k}:ih*{k}:flags=neighbor", "-c:v", "libx264", "-preset", "slow", "-crf", "12",
           "-pix_fmt", "yuv420p", "-movflags", "+faststart", out]
    subprocess.run(enc, input=b"".join(frames), check=True)
    dec = subprocess.run([ffmpeg, "-loglevel", "error", "-i", out, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], check=True, capture_output=True).stdout
    size = w * k * h * k * 3
    got = [dec[i:i + size] for i in range(0, len(dec), size)]
    se, count = 0.0, 0
    for a, b in zip(got, frames):
        d = np.frombuffer(a, np.uint8).astype(np.float64) - upscale(b, w, h, k).reshape(-1)
        se += float((d * d).sum())
        count += d.size
    mse = se / max(1, count)
    psnr = 99.0 if mse == 0 else 10 * math.log10(255 ** 2 / mse)
    return {"mp4": out, "mp4_frames": len(got), "mp4_scale": k, "mp4_psnr_db": round(psnr, 2), "mp4_bytes": Path(out).stat().st_size}


def webp_exact(path, frames, w, h, k):
    from PIL import Image
    im = Image.open(path)
    n, exact = getattr(im, "n_frames", 1), True
    for i in range(min(n, len(frames))):
        im.seek(i)
        exact = exact and np.array_equal(np.asarray(im.convert("RGB")), upscale(frames[i], w, h, k))
    return {"webp_frames": n, "webp_exact": exact and n == len(frames), "webp_loop": im.info.get("loop")}


def gif_off_palette(path):
    from PIL import Image
    im = Image.open(path)
    pal = im.getpalette() or []
    allowed = {tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)}
    off = 0
    for i in range(im.n_frames):
        im.seek(i)
        off += sum(c for c, rgb in im.convert("RGB").getcolors(1 << 24) if rgb not in allowed)
    return {"gif_frames_pillow": im.n_frames, "gif_off_palette_pillow": off}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--out")
    ap.add_argument("--ffmpeg")
    ap.add_argument("--keep-raw", action="store_true")
    a = ap.parse_args()
    ffmpeg, rows, ok = find_ffmpeg(a.ffmpeg), [], True
    for loop in json.loads(Path(a.run).read_text()):
        res = dict(loop)
        res.update(gif_off_palette(loop["gif"]))
        ok = ok and res["gif_off_palette_pillow"] == 0
        raw_loop = loop if loop["raw"] else next((x for x in rows if x["loop"] == loop["loop"] and x.get("raw")), None)
        if raw_loop:
            frames = frames_of(raw_loop["raw"], loop["w"], loop["h"], loop["frames"])
            res.update(webp_exact(loop["webp"], frames, loop["w"], loop["h"], loop["scale"]))
            ok = ok and res["webp_exact"]
            if loop["scale"] == 1:
                res.update(mp4(ffmpeg, loop, frames, 2 if loop["w"] < 960 else 1))
                ok = ok and res["mp4_frames"] == loop["frames"] and res["mp4_psnr_db"] >= 35
        rows.append(res)
        print(json.dumps({k: v for k, v in res.items() if k not in ("gif", "webp", "raw", "mp4")}))
    if not a.keep_raw:
        for r in rows:
            if r.get("raw"):
                Path(r["raw"]).unlink(missing_ok=True)
    if a.out:
        Path(a.out).write_text(json.dumps(rows, indent=1) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
