#!/usr/bin/env python3
"""M2 criterion 10: the tile pass in a real browser.

    python tests/web/tiles_check.py [--adapter gpu|swiftshader] [--out evidence.json]

Opens web/test/tiles.html. The page checks two things:
- small random maps (every flip) at scales 1, 2 and 3, against the CPU twin
  (web/world/tiles_ref.mjs);
- 100,000 tiles on screen, captured 20 times for the frame time, with identical
  frames (SHA-256) before and after.
Passes when every pixel is within half-float precision of the twin (2^-11) and the two
large frames are identical. Frame times are recorded, not judged: the criterion's
time is the reference machine's.
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
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/tiles.html")
            page.wait_for_function("window.__result !== undefined", timeout=600000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    problems = [res["error"]] if "error" in res else []
    for c in res.get("cases", []):
        if c["maxAbs"] > 2 ** -11:
            problems.append(f"scale {c['scale']}: differs from the CPU twin by {c['maxAbs']} ({c['bad']} values), first {c['first']}")
    if res.get("big") and not res["big"]["sameTwice"]:
        problems.append("the 100,000-tile frame differs between two captures")
    res.update({"adapter_kind": a.adapter, "problems": problems,
                "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip(),
                "does_not_prove": "Frame time on this adapter only; the criterion's time is the RTX 4090's."})
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
