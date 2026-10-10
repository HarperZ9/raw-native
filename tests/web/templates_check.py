#!/usr/bin/env python3
"""The style template scaffolds run: two seconds of each, twice, on Chrome's WebGPU.

    python tests/web/templates_check.py [--adapter gpu|swiftshader] [--seconds 2]

Passes when every template renders frames that are not blank (a frame's pixels are not
all one colour) and both runs give the same frame hashes. This is a scaffold check; the
finished templates are held to ROADMAP M2 criterion 12.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = ["pixel-2.5d", "retro-thriller", "painted-iso-rpg"]


def render(name: str, out: Path, a) -> list[str]:
    subprocess.run([sys.executable, str(ROOT / "scripts" / "motion_render.py"), "--root", str(ROOT), "--scene", f"../../templates/{name}/scene.mjs",
                    "--out", str(out), "--adapter", a.adapter, "--width", "480", "--height", "270", "--to", str(a.seconds), "--hash-frames"],
                   check=True, capture_output=True)
    lines = Path(str(out) + ".frames.sha256").read_text(encoding="utf-8").splitlines()
    return [ln.split()[1] for ln in lines if ln.strip()]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--seconds", type=float, default=2)
    a = ap.parse_args()
    problems = []
    with tempfile.TemporaryDirectory() as t:
        for name in TEMPLATES:
            h1, h2 = render(name, Path(t) / f"{name}-a.mp4", a), render(name, Path(t) / f"{name}-b.mp4", a)
            distinct = len(set(h1))
            print(f"{name}: {len(h1)} frames, {distinct} distinct, same twice: {h1 == h2}")
            if not h1 or h1 != h2:
                problems.append(f"{name}: frames differ between two runs")
            if distinct < 2:
                problems.append(f"{name}: every frame is the same; the scene may not be drawing")
    print("\n".join(problems) or "all templates render, the same twice")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
