#!/usr/bin/env python3
"""M1 exit criterion 1: host textures, compositing and a shared device, in a real browser.

    python tests/web/host_textures.py [--adapter gpu|swiftshader] [--out evidence.json]

Opens web/test/host-textures.html in headless Chrome. The page composites a 2D
canvas and two RGBA textures on the GPU and compares the read-back pixels with the
same composite done on the CPU. Passes when:
- both consumers match within 1/255;
- the page made exactly one requestDevice call for the two of them;
- the canvas-only fallback draws.
The time to composite a 1920 x 1080 canvas is recorded where the adapter has
timestamps, and checked against the 1.0 ms budget on a GPU adapter.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html", ".wgsl": "text/plain", ".json": "application/json"}
BUDGET_MS = 1.0


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, super().guess_type(path))

    def log_message(self, *a):
        pass


def run(adapter: str) -> dict:
    from playwright.sync_api import sync_playwright
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(ROOT)))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
    if adapter == "swiftshader":
        args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"]
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/host-textures.html")
            page.wait_for_function("window.__result !== undefined", timeout=120000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    res["adapter_kind"] = adapter
    return res


def check(res: dict) -> list[str]:
    problems = []
    if "error" in res:
        return [res["error"]]
    for name in ("host", "shared"):
        if res[name]["maxDiff"] > 1:
            problems.append(f"{name}: GPU composite differs from the CPU composite by {res[name]['maxDiff']}/255")
    if res.get("requestDevice") != 1:
        problems.append(f"requestDevice was called {res.get('requestDevice')} times for two consumers")
    if not res.get("sameDevice"):
        problems.append("the shared consumer has its own device")
    if not res["fallback"]["drawn"]:
        problems.append("the canvas fallback drew nothing")
    ms = (res.get("timings") or {}).get("composite")
    if res["adapter_kind"] == "gpu" and ms is not None and ms > BUDGET_MS:
        problems.append(f"compositing 1920 x 1080 took {ms:.3f} ms, over the {BUDGET_MS} ms budget")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--out")
    a = ap.parse_args()
    res = run(a.adapter)
    problems = check(res)
    res["problems"] = problems
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
