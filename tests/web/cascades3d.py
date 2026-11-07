#!/usr/bin/env python3
"""3D radiance cascades in a real browser: GPU parity, and measured runs.

    python tests/web/cascades3d.py parity [--adapter gpu|swiftshader] [--case ID] [--out evidence.json]
    python tests/web/cascades3d.py measure (--scene NAME | --file build/gi/x.bin) --plan '{"h0": 0.5}'
        [--width 512 --height 320] [--frames 50] [--baseline 16] [--save DIR] [--out run.json]

Parity opens web/test/shaders-cascades3d.html, which runs every case of
evidence/shaders-cascades3d-parity-bounds.json through the float64 CPU reference and the WebGPU
passes and passes when every case is inside the bounds committed before the first run. Measure runs
one scene on the GPU, times it with GPU timestamps and saves the frame (and the no-GI baseline) as
float32 .npy-free raw files (W*H*4 little-endian f32) in DIR for tests/web/cascades3d_metrics.py.
A --file path is served from the repository root, so it must lie inside it (build/ is ignored by git).
On a shared machine, run the gpu adapter with nvidia-smi checked first.
"""

from __future__ import annotations

import argparse
import base64
import functools
import http.server
import json
import sys
import threading
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shaders_parity import ROOT, Handler, CPU_FRAME, run  # noqa: E402  (the same server and browser launch)

CHUNK = 8 << 20


def check(res: dict) -> list[str]:
    if "error" in res:
        return [res["error"]]
    if res.get("gpuErrors"):
        return ["WebGPU error: " + m for m in res["gpuErrors"]]
    b, out = res["bounds"], []
    for c in res["cases"]:
        if c["rel_rmse"] > b["rel_rmse"]:
            out.append(f"{c['id']}: GPU against CPU {c['rel_rmse']:.2e} (bound {b['rel_rmse']})")
        p = c.get("planted")
        if p and max(p["gpu_vs_closed_form"], p["cpu_vs_closed_form"]) > b["planted_rel_rmse_against_closed_form"]:
            out.append(f"{c['id']}: against the closed form {p} (bound {b['planted_rel_rmse_against_closed_form']})")
    return out


def measure(a) -> dict:
    from playwright.sync_api import sync_playwright
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(ROOT)))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
    if a.adapter == "swiftshader":
        args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader", *CPU_FRAME]
    q = {"mode": "measure", "plan": a.plan, "w": a.width, "h": a.height, "frames": a.frames, "baseline": a.baseline, "pos": a.pos, "static": a.static}
    if a.file:
        q["file"] = "/" + Path(a.file).resolve().relative_to(ROOT).as_posix()
    else:
        q["scene"] = a.scene
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/shaders-cascades3d.html?{urllib.parse.urlencode(q)}")
            page.wait_for_function("window.__result !== undefined", timeout=3600000)
            res = page.evaluate("window.__result")
            if a.save and "frames" in res:
                d = Path(a.save)
                d.mkdir(parents=True, exist_ok=True)
                for name, n in res["frames"].items():
                    with open(d / f"{name}.f32", "wb") as f:
                        for i in range((n + CHUNK - 1) // CHUNK):
                            f.write(base64.b64decode(page.evaluate(f"window.__chunk('{name}', {i}, {CHUNK})")))
            b.close()
    finally:
        srv.shutdown()
    res["adapter_kind"] = a.adapter
    return res


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mode", choices=["parity", "measure", "debug"])
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--case")
    ap.add_argument("--scene")
    ap.add_argument("--file")
    ap.add_argument("--plan", default="{}")
    ap.add_argument("--width", type=int, default=512)
    ap.add_argument("--height", type=int, default=320)
    ap.add_argument("--frames", type=int, default=50)
    ap.add_argument("--baseline", type=int, default=0)
    ap.add_argument("--save")
    ap.add_argument("--static", type=int, default=0, help="probe flags once at setup (static geometry), not every frame")
    ap.add_argument("--pos", type=int, default=0, help="also save each pixel's surface position (pos.f32)")
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.mode == "debug":
        res = run("cascades3d", a.adapter, {"mode": "debug", "case": a.case}, 3600000)
        problems = []
    elif a.mode == "parity":
        res = run("cascades3d", a.adapter, {"case": a.case} if a.case else {}, 3600000)
        problems = check(res)
    else:
        res = measure(a)
        problems = [res["error"]] if "error" in res else ["WebGPU error: " + m for m in res.get("gpuErrors", [])]
    text = json.dumps(res, indent=2)
    print(text if len(text) < 20000 else text[:20000] + "\n...")
    if a.out:
        Path(a.out).parent.mkdir(parents=True, exist_ok=True)
        Path(a.out).write_text(text + "\n", encoding="utf-8", newline="\n")
    for p in problems:
        print("FAIL:", p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
