#!/usr/bin/env python3
"""Radiance cascades in a real browser: GPU parity, and measurement against the brute force.

    python tests/web/cascades.py parity [--adapter gpu|swiftshader] [--case ID] [--out evidence.json]
    python tests/web/cascades.py sweep --scene room --configs '[{"branch": 2, "n0": 4, "r0": 1}]' [--rays 1024 --stride 8]
    python tests/web/cascades.py measure --scene disk|room|glass [--adapter gpu] [--width 3840 --height 2160]
        [--branch 2 --n0 4 --r0 1 --fix 0] [--rays 4096 --stride 2] [--frames 50] [--images DIR] [--out evidence.json]

Parity opens web/test/shaders-cascades.html, which runs every case of
evidence/shaders-cascades-parity-bounds.json through the float64 CPU reference and the WebGPU passes
(cascades and brute force), and passes when every case is inside the bounds committed before the
first run. Measure renders one scene with the cascades, times it with GPU timestamps, renders the
brute-force reference twice (two seeds, so its own noise is known) and reports the error; --images
writes the render, the reference and the error map as PNG files. On a shared machine, run the gpu
adapter only while holding the machine's GPU lock.
"""

from __future__ import annotations

import argparse
import base64
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shaders_parity import run  # noqa: E402  (the same server and browser launch)


def check(res: dict) -> list[str]:
    if "error" in res:
        return [res["error"]]
    if res.get("gpuErrors"):
        return ["WebGPU error: " + m for m in res["gpuErrors"]]
    b = res["bounds"]

    def ok(c: dict) -> bool:
        ref_ok = (c["reference_flip_fraction"] <= b["glass_reference_flip_fraction"] if c["scene"] == "glass"
                  else c["reference_rel_rmse"] <= b["reference_rel_rmse"])
        return c["cascades_rel_rmse"] <= b["cascades_rel_rmse"] and ref_ok
    return [f"{c['id']}: cascades {c['cascades_rel_rmse']:.2e}, reference {c['reference_rel_rmse']:.2e}, "
            f"flipped samples {c['reference_flip_fraction']:.3f} (bounds {b})" for c in res["cases"] if not ok(c)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mode", choices=["parity", "measure", "sweep"])
    ap.add_argument("--configs", help="sweep: a JSON list of {branch, n0, r0, fix}")
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--case")
    ap.add_argument("--scene", default="disk")
    ap.add_argument("--width", type=int, default=3840)
    ap.add_argument("--height", type=int, default=2160)
    ap.add_argument("--branch", type=int, default=2)
    ap.add_argument("--n0", type=int, default=4)
    ap.add_argument("--r0", type=float, default=1)
    ap.add_argument("--fix", type=int, default=0)
    ap.add_argument("--rays", type=int, default=4096)
    ap.add_argument("--stride", type=int, default=2)
    ap.add_argument("--frames", type=int, default=50)
    ap.add_argument("--chunk", type=int, default=32)
    ap.add_argument("--images")
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.mode == "parity":
        res = run("cascades", a.adapter, {"case": a.case} if a.case else {}, 3600000)
        problems = check(res)
    elif a.mode == "sweep":
        q = {"mode": "sweep", "scene": a.scene, "w": a.width, "h": a.height, "rays": a.rays, "stride": a.stride, "frames": a.frames,
             "chunk": a.chunk, "configs": a.configs}
        res = run("cascades", a.adapter, q, 3600000)
        problems = [res["error"]] if "error" in res else ["WebGPU error: " + m for m in res.get("gpuErrors", [])]
    else:
        q = {"mode": "measure", "scene": a.scene, "w": a.width, "h": a.height, "branch": a.branch, "n0": a.n0, "r0": a.r0,
             "fix": a.fix, "rays": a.rays, "stride": a.stride, "frames": a.frames, "chunk": a.chunk, "images": 1 if a.images else 0}
        res = run("cascades", a.adapter, q, 3600000)
        problems = [res["error"]] if "error" in res else ["WebGPU error: " + m for m in res.get("gpuErrors", [])]
        images = res.pop("images", None)
        if images and a.images:
            d = Path(a.images)
            d.mkdir(parents=True, exist_ok=True)
            tag = f"{a.scene}-b{a.branch}-n{a.n0}-r{a.r0:g}{'-fix' if a.fix else ''}-{a.width}x{a.height}"
            for k, v in images.items():
                (d / f"{tag}-{k}.png").write_bytes(base64.b64decode(v))
    text = json.dumps(res, indent=2)
    print(text)
    if a.out:
        Path(a.out).parent.mkdir(parents=True, exist_ok=True)
        Path(a.out).write_text(text + "\n", encoding="utf-8", newline="\n")
    for p in problems:
        print("FAIL:", p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
