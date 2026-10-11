#!/usr/bin/env python3
"""Render every guide shot in templates/guides/shots/ as a still, then paint the painted ones.

    python tools/guides/render_all.py --out DIR [--only NAME ...] [--paint-scale 2]

Each shot is drawn by the Motion renderer on Chrome's SwiftShader adapter (software WebGPU,
so no GPU lock is needed) through tools/guides/still.py. Painted shots then run through the
paint shader's CPU reference, and thriller shots through the film and tube CPU references
(tools/guides/post_still.mjs); pixel shots render at 480 x 270 (960 x 540 for the sheet) with the dither pass and are scaled up by nearest neighbour, which is what the engine's pixel mode does; its own capture path dropped the low-resolution render and the passes in this run. Writes DIR/NAME.png, keeps the
unpainted frame as DIR/raw/NAME.png, and writes DIR/renders.json with each file's sha256.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHOTS = ROOT / "templates" / "guides" / "shots"
HOME = {"tallow-sheet": "thriller", "pylon-sheet": "painted", "rerun-sheet": "pixel", "lesson-night-shift": "thriller"}


def style_of(name: str) -> str:
    return HOME.get(name) or next(s for s in ("thriller", "painted", "pixel") if s in name)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--paint-scale", type=int, default=2)
    ap.add_argument("--film-ev", type=float, default=0.0)
    a = ap.parse_args()
    out = Path(a.out).resolve()
    (out / "raw").mkdir(parents=True, exist_ok=True)
    names = sorted(p.stem for p in SHOTS.glob("*.mjs"))
    if a.only:
        names = [n for n in names if n in a.only]
    record = {}
    for name in names:
        t0 = time.time()
        raw = out / "raw" / f"{name}.png"
        style = style_of(name)
        # Pixel shots render at the low resolution itself, then scale by nearest neighbour.
        k = 0 if style != "pixel" else (2 if name.endswith("sheet") else 4)
        size = ["--width", str(1920 // k), "--height", str(1080 // k)] if k else []
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "guides" / "still.py"), "--scene", f"../../templates/guides/shots/{name}.mjs",
                            "--out", str(raw), "--t", "0.2", *size], capture_output=True, text=True)
        if r.returncode != 0:
            print(f"{name}: render failed\n{r.stdout}\n{r.stderr}", file=sys.stderr)
            return 1
        final = out / f"{name}.png"
        post = ROOT / "tools" / "guides" / "post_still.mjs"
        if style == "painted":
            # The hud (labels, key terms) renders apart on black and on white, and goes on after the paint.
            hud = []
            for ground in ("black", "white"):
                hp = out / "raw" / f"{name}.hud-{ground}.png"
                subprocess.run([sys.executable, str(ROOT / "tools" / "guides" / "still.py"), "--scene", f"../../templates/guides/shots/{name}.mjs?layer=hud-{ground}",
                                "--out", str(hp), "--t", "0.2"], check=True, capture_output=True)
                hud += [f"--hud-{ground}", str(hp)]
            subprocess.run(["node", str(post), str(raw), str(final), "paint", "--scale", str(a.paint_scale), *hud], check=True)
        elif style == "thriller":
            subprocess.run(["node", str(post), str(raw), str(final), "film", "--ev", str(a.film_ev)], check=True)
        else:
            subprocess.run(["node", str(post), str(raw), str(final), "upscale", "--scale", str(k)], check=True)
        record[name] = {"sha256": hashlib.sha256(final.read_bytes()).hexdigest(), "style": style, "seconds": round(time.time() - t0, 1)}
        print(f"{name}: {record[name]['seconds']} s", flush=True)
    meta = {"adapter": "swiftshader", "size": [1920, 1080], "renders": record}
    (out / "renders.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
