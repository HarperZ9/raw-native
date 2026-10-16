#!/usr/bin/env python3
"""The explainer kit's WGSL against its float64 reference, on the GPU through headless Chrome.

    python tests/web/explainer_parity.py [--adapter gpu|swiftshader] [--out evidence.json]

Opens web/test/explainer-parity.html. A compute kernel runs the same WGSL functions the passes
use (orb, atmosphere, curve, pulse, LOD hash) on rows of inputs, compared with
web/explainer/ref.mjs; the point layer is drawn through Motion and its blobs located; a
1,048,576-point cloud is drawn with LOD and timed. Bounds, committed before the first run:
evidence/explainer-parity-bounds.json. Three controls (a 3% core radius change, a changed hash
multiplier, positions shifted 1 px) must fail.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import subprocess
import sys
import threading
from pathlib import Path

# With the CPU WebGPU adapter, keep the rest of Chrome off the GPU too: by default its compositor and
# 2D canvas still draw on the machine's GPU through ANGLE (checked 2026-10-14: an RTX 4090 via D3D11).
CPU_FRAME = ["--disable-gpu-compositing", "--disable-accelerated-2d-canvas", "--disable-gpu-rasterization"]

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".html": "text/html", ".json": "application/json", ".wgsl": "text/plain"}


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

    def log_message(self, *a):
        pass


def judge(res: dict, B: dict) -> list[str]:
    """Every bound, and every control failing."""
    k, out = res["kernel"], []
    for i, c in enumerate(k["orb"]):
        if c["max"] > B["orb"]["max"] or c["p99"] > B["orb"]["p99"]:
            out.append(f"orb spec {i}: max {c['max']:.3g}, p99 {c['p99']:.3g}")
    for i, c in enumerate(k["atmosphere"]):
        if c["max"] > B["atmosphere"]["max"] or c["p99"] > B["atmosphere"]["p99"]:
            out.append(f"atmosphere spec {i}: max {c['max']:.3g}, p99 {c['p99']:.3g}")
    if k["bezier"]["max"] > B["bezier"]["max"]:
        out.append(f"bezier max {k['bezier']['max']:.3g}")
    if k["pulse"]["max"] > B["pulse"]["max"]:
        out.append(f"pulse max {k['pulse']['max']:.3g}")
    if k["lod"]["mismatches"] > B["lod_hash"]["max"]:
        out.append(f"LOD mismatches {k['lod']['mismatches']}")
    pl = res["placement"]
    if pl["max"] > B["points_placement"]["max"]:
        out.append(f"placement max {pl['max']:.3f} px")
    if max(abs(pl["meanDx"]), abs(pl["meanDy"])) > B["points_placement"]["bias_max"]:
        out.append(f"placement bias {pl['meanDx']:.3f}, {pl['meanDy']:.3f} px")
    if res["million"]["countError"] > B["points_lod_1m"]["max_count_error"]:
        out.append(f"1M LOD count error {res['million']['countError']}")
    if k["controls"]["orb"]["max"] <= B["orb"]["max"]:
        out.append("control: the 3% core radius change passed the orb bound")
    if k["controls"]["hash"]["mismatches"] == 0:
        out.append("control: the changed hash passed")
    if res["placement"]["controlShiftedMax"] <= B["points_placement"]["max"]:
        out.append("control: positions shifted 1 px passed")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--out")
    a = ap.parse_args()
    from playwright.sync_api import sync_playwright
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(ROOT)))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
    if a.adapter == "swiftshader":
        args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader", *CPU_FRAME]
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/explainer-parity.html")
            page.wait_for_function("window.__result !== undefined", timeout=900000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    bounds = json.loads((ROOT / "evidence/explainer-parity-bounds.json").read_text(encoding="utf-8"))["bounds"]
    problems = [res["error"]] if "error" in res else []
    if not problems:
        problems = judge(res, bounds)
    res.update({"adapter_kind": a.adapter, "problems": problems,
                "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip(),
                "does_not_prove": "That the pictures look right to a viewer, or that a GPU other than this adapter agrees; the timings are this adapter only."})
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
