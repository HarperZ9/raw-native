#!/usr/bin/env python3
"""Render the guides' motion clips (templates/guides/clips/clip.mjs) as short videos, and a reel.

    python tools/guides/render_clips.py --out DIR [--adapter gpu] [--only tallow-idle ...]

Each guide gets an idle loop, a talk loop and its signature action, in its home world: Tallow
in the retro thriller office, Pylon on the painted harbour, Rerun on the pixel shore. Frames
come from scripts/motion_render.py (raw transport, H.264 by libx264, no audio). --adapter gpu
is for use under the D:/gpu.lock.d protocol only. The reel joins every clip in order into
DIR/guides-reel.mp4 with ffmpeg.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLIPS = {"idle": 2.4, "talk": 2.4, "action": 3.2}
# The module query is percent-encoded: the capture page takes the scene as one parameter.
# (guide, clip, framing): talk is close on the face, idle and action are medium.
PLAN = [(g, c, "close" if c == "talk" else "medium") for g in ("tallow", "pylon", "rerun") for c in ("idle", "talk", "action")]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--adapter", choices=["swiftshader", "gpu"], default="swiftshader")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--loops", type=int, default=2, help="times each loop clip repeats in the reel")
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    made = []
    for g, c, f in PLAN:
        name = f"{g}-{c}"
        if a.only and name not in a.only:
            continue
        t0 = time.time()
        mp4 = out / f"{name}.mp4"
        cmd = [sys.executable, str(ROOT / "scripts" / "motion_render.py"), "--root", str(ROOT), "--scene",
               f"../../templates/guides/clips/clip.mjs%3Fguide={g}%26clip={c}%26frame={f}", "--out", str(mp4), "--from", "0", "--to", str(CLIPS[c]),
               "--fps", str(a.fps), "--transport", "raw", "--codec", "libx264", "--quality", "16", "--adapter", a.adapter]
        # The local page server sometimes drops a fetch under load ("Failed to fetch"): retry twice.
        for _ in range(3):
            try:
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
            except subprocess.TimeoutExpired:
                continue
            if r.returncode == 0 or "Failed to fetch" not in (r.stdout + r.stderr):
                break
        else:
            print(f"{name}: three attempts failed or timed out", file=sys.stderr)
            return 1
        if r.returncode != 0:
            print(f"{name}: failed\n{r.stdout[-1500:]}\n{r.stderr[-1500:]}", file=sys.stderr)
            return 1
        print(f"{name}: {time.time() - t0:.1f} s", flush=True)
        made.append((name, mp4, c))
    if not a.only:
        lst = out / "reel.txt"
        lines = []
        for name, mp4, c in made:
            lines += [f"file '{mp4.as_posix()}'"] * (a.loops if c != "action" else 1)
        lst.write_text("\n".join(lines) + "\n", encoding="utf-8")
        sys.path.insert(0, str(ROOT / "scripts"))
        import motion_render as mr
        ff = mr.find_ffmpeg(None)
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(lst), "-c:v", "libx264", "-crf", "18", "-preset", "slow",
                        "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(out / "guides-reel.mp4")], check=True)
        print(out / "guides-reel.mp4")
    return 0


if __name__ == "__main__":
    sys.exit(main())
