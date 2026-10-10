#!/usr/bin/env python3
"""Shader lab 2 frame times in a real browser.

    python tests/web/lab2_timing.py [--adapter gpu|swiftshader] [--width 1920 --height 1080] [--frames 30] [--out evidence.json]

Opens web/test/shaders-lab2-timing.html, which renders each technique's presets on the GPU only and
reports median and p90 wall time per frame (submit to onSubmittedWorkDone). On a shared machine,
run the gpu adapter only while holding the machine's GPU lock.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shaders_parity import run  # noqa: E402  (the same server and browser launch)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--frames", type=int, default=30)
    ap.add_argument("--shaders", default="adjacency,sag,lightpaint,glaze,hysteresis")
    ap.add_argument("--out")
    a = ap.parse_args()
    res = run("lab2-timing", a.adapter, {"w": a.width, "h": a.height, "frames": a.frames, "shaders": a.shaders}, 3600000)
    text = json.dumps(res, indent=2)
    print(text)
    if a.out:
        Path(a.out).parent.mkdir(parents=True, exist_ok=True)
        Path(a.out).write_text(text + "\n", encoding="utf-8", newline="\n")
    problems = [res["error"]] if "error" in res else ["WebGPU error: " + m for m in res.get("gpuErrors", [])]
    for p in problems:
        print("FAIL:", p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
