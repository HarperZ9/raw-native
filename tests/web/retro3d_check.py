#!/usr/bin/env python3
"""M2 criterion 8: vertex snap and affine UVs, GPU against the CPU twin.

    python tests/web/retro3d_check.py [--adapter gpu|swiftshader] [--out evidence.json]

Opens web/test/retro3d.html: the committed mesh set (cube, floor, cylinder) under the four
settings of snap and affine, compared per pixel with web/world/retro3d.mjs. Bounds
(evidence/m2-retro3d-bounds.json): no coverage mismatch away from edges; at least 99.5% of
covered pixels fetch the same texel and the rest one texel away; the affine and snap
controls fail.
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

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".html": "text/html", ".json": "application/json", ".wgsl": "text/plain"}


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

    def log_message(self, *a):
        pass


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
        args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"]
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/retro3d.html?subpixel=" + ("4" if a.adapter == "swiftshader" else "8"))
            page.wait_for_function("window.__result !== undefined", timeout=600000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    problems = [res["error"]] if "error" in res else []
    for c in res.get("cases", []):
        if not c["pass"]:
            problems.append(f"{c['mesh']} snap={c['snap']} affine={c['affine']}: {c['coverMismatch']} coverage mismatches, same texel {c['sameTexel']:.4f}, {c['farTexel']} far, first {c['firstFar']}")
        if c["covered"] < 2000:
            problems.append(f"{c['mesh']}: only {c['covered']} covered pixels compared")
    for k in ("controlAffine", "controlSnap"):
        if k in res and not res[k].get("fails"):
            problems.append(f"{k} passed: the check cannot tell the flag apart")
    res.update({"adapter_kind": a.adapter, "problems": problems,
                "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip(),
                "does_not_prove": "That the look matches any particular console, or performance."})
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
