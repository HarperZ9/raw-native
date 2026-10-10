#!/usr/bin/env python3
"""M2 criterion 7: pixel-perfect scaling in a real browser.

    python tests/web/pixel_scaling.py [--adapter gpu|swiftshader] [--out evidence.json]

Opens web/test/pixel.html, which renders a 160 x 90 frame and scales it to 960 x 540.
Bounds (evidence/m2-pixel-scaling-bounds.json, written before the first run):
- integer mode is bit-equal to integerUpscale of the low-resolution frame;
- sharp mode at sub-texel offsets 0, 0.25, 0.5 and 0.75 is within 1 of sharpBilinear;
- over a 120-frame pan in 0.1-pixel steps, a square's left edge stays within one output
  pixel of the continuous position and never moves backwards.
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
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/pixel.html")
            page.wait_for_function("window.__result !== undefined", timeout=600000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    problems = [res["error"]] if "error" in res else []
    for c in res.get("cases", []):
        limit = 0 if c["check"] == "integer" else 1
        if c["max"] > limit:
            problems.append(f"{c['check']} {c.get('want', '')}: off by {c['max']} ({c['bad']} values), first {c['first']}")
    if res.get("cases") and res["cases"][0].get("lowNonGround", 0) < 1000:
        problems.append("the low-resolution frame is almost empty, so the comparison proves little")
    pan = res.get("pan")
    if pan:
        if pan["missing"]:
            problems.append(f"pan: no edge found on {pan['missing']} frames")
        if pan["maxErr"] > 1:
            problems.append(f"pan: edge off by up to {pan['maxErr']:.3f} output pixels")
        if pan["backwards"]:
            problems.append(f"pan: the edge moved backwards on {pan['backwards']} frames")
    res.update({"adapter_kind": a.adapter, "problems": problems,
                "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip(),
                "does_not_prove": "Behaviour on content other than these frames and hard-edged squares, or on a display."})
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
