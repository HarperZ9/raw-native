#!/usr/bin/env python3
"""Render every guide shot in templates/guides/shots/ as a still, through the engine's own looks.

    python tools/guides/render_all.py --out DIR [--only NAME ...] [--jobs 2]

Each shot is drawn by the Motion renderer through scripts/motion_still.py, on Chrome's
SwiftShader adapter by default (software WebGPU, no GPU lock) or with --adapter gpu under
the D:/gpu.lock.d protocol, with its style's post stack in the
display list: the paint pass for the painted harbour, film then the soft tube for the
thriller, and pixel mode with the tidepool's fixed palette for the pixel shore. Nothing is
finished by hand afterwards. The one extra step is for painted shots with labels: the hud
renders apart on black and on white and goes over the painted art (tools/guides/post_still.mjs),
because the paint pass is a scene pass and would paint the type. Writes DIR/NAME.png and
DIR/renders.json with each file's sha256 and render time.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHOTS = ROOT / "templates" / "guides" / "shots"
HOME = {"tallow-sheet": "thriller", "pylon-sheet": "painted", "rerun-sheet": "pixel", "lesson-night-shift": "thriller"}
HUD = {"pylon-painted", "pylon-sheet"}          # painted shots that carry labels


def style_of(name: str) -> str:
    return HOME.get(name) or next(s for s in ("thriller", "painted", "pixel") if s in name)


ADAPTER = ["--adapter", "swiftshader"]


def still(scene: str, out: Path) -> None:
    # The local page server sometimes drops a fetch under load ("Failed to fetch"): retry twice.
    for _ in range(3):
        # A render that hangs (seen once on the GPU adapter) is stopped after 5 minutes and retried.
        try:
            r = subprocess.run([sys.executable, str(ROOT / "scripts" / "motion_still.py"), "--scene", scene, "--out", str(out), "--t", "0.2", "--warm", "2", *ADAPTER],
                               capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            continue
        if r.returncode == 0 or "Failed to fetch" not in (r.stdout + r.stderr):
            break
    else:
        raise RuntimeError(f"{scene}: three attempts failed or timed out")
    if r.returncode != 0:
        raise RuntimeError(f"{scene}: render failed\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")


def render(name: str, out: Path) -> tuple[str, dict]:
    t0 = time.time()
    style, final = style_of(name), out / f"{name}.png"
    scene = f"../../templates/guides/shots/{name}.mjs"
    if style == "painted" and name in HUD:
        art = out / "raw" / f"{name}.art.png"
        still(scene + "?layer=art", art)
        huds = []
        for ground in ("black", "white"):
            hp = out / "raw" / f"{name}.hud-{ground}.png"
            still(scene + f"?layer=hud-{ground}", hp)
            huds += [f"--hud-{ground}", str(hp)]
        subprocess.run(["node", str(ROOT / "tools" / "guides" / "post_still.mjs"), str(art), str(final), *huds], check=True, capture_output=True)
    else:
        still(scene + ("?layer=all" if style == "painted" else ""), final)
    rec = {"sha256": hashlib.sha256(final.read_bytes()).hexdigest(), "style": style, "seconds": round(time.time() - t0, 1)}
    print(f"{name}: {rec['seconds']} s", flush=True)
    return name, rec


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--adapter", choices=["swiftshader", "gpu"], default="swiftshader", help="gpu only under the D:/gpu.lock.d protocol")
    a = ap.parse_args()
    ADAPTER[1] = a.adapter
    out = Path(a.out).resolve()
    (out / "raw").mkdir(parents=True, exist_ok=True)
    names = sorted(p.stem for p in SHOTS.glob("*.mjs"))
    if a.only:
        names = [n for n in names if n in a.only]
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        record = dict(ex.map(lambda n: render(n, out), names))
    prev = out / "renders.json"
    old = json.loads(prev.read_text(encoding="utf-8")).get("renders", {}) if prev.exists() else {}
    meta = {"adapter": a.adapter, "size": [1920, 1080], "renders": {**old, **record}}
    prev.write_text(json.dumps(meta, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
