#!/usr/bin/env python3
"""The post stack at 3840 x 2160 in a real browser (web/test/post-4k.html).

    python tests/web/post_4k.py [--adapter swiftshader|gpu] [--page post-4k.html|pixel-4k.html] [--out result.json]

A 4K frame has 8,294,400 pixels. A pass that dispatches one 64-wide workgroup per 64 pixels in
one dimension asks for 129,600 groups, over WebGPU's limit of 65,535 a dimension, so the
dispatch is refused and the frame comes out black while the render still reports success.
Each case (the dither byte plate, the film and crt encoders) draws a gradient at 4K. Passes when
every frame carries the image (8-bit luma standard deviation of at least 2) and the device
reports no validation error. --page pixel-4k.html runs the same check on the pixel-art frame loop,
whose splat-buffer clear has the same one-dimensional dispatch. Exit 0 pass, 1 fail.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import sys
import threading
from pathlib import Path

CPU_FRAME = ["--disable-gpu-compositing", "--disable-accelerated-2d-canvas", "--disable-gpu-rasterization"]
ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html", ".json": "application/json", ".wgsl": "text/plain"}
MIN_SD = 2.0


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

    def log_message(self, *a):
        pass


def run(adapter: str, test_page: str = "post-4k.html") -> dict:
    from playwright.sync_api import sync_playwright
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(ROOT)))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
    if adapter == "swiftshader":
        args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader", *CPU_FRAME]
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/{test_page}")
            page.wait_for_function("window.__result !== undefined", timeout=1800000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    res["adapter_kind"] = adapter
    return res


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="swiftshader", choices=["swiftshader", "gpu"])
    ap.add_argument("--page", default="post-4k.html", choices=["post-4k.html", "pixel-4k.html"])
    ap.add_argument("--out")
    a = ap.parse_args(argv)
    res = run(a.adapter, a.page)
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1), encoding="utf-8")
    if res.get("error"):
        print("FAIL:", res["error"])
        return 1
    ok = bool(res.get("cases"))
    for c in res["cases"]:
        good = c["sd"] >= MIN_SD and not c["errors"]
        ok = ok and good
        print(f"{c['pass']}: luma mean {c['mean']:.1f}, sd {c['sd']:.2f}, validation errors {len(c['errors'])}"
              + ("" if good else "  FAILS") + ("".join(f"\n    {e}" for e in c["errors"][:2])))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
