#!/usr/bin/env python3
"""M1 exit criteria 9 and 10: every registered post pass against its CPU reference, in a real browser.

    python tests/web/post_passes.py [--adapter gpu|swiftshader] [--modules web/shaders/index.mjs,...] [--out evidence.json]

Headless Chrome loads web/test/post-passes.html. It runs every pass that has a CPU
reference (web/motion/post.mjs, and the shader library's modules named by
--modules) over the committed frame set (tests/web/post_frames.mjs) for each of
the pass's test settings, and compares with the reference at 8 bits. A history
pass runs the frames in order, so its reference sees the same previous output.
Passes when every value is within one 8-bit code, and when, inside the Motion
renderer, a neutral stack (exposure at 0 EV and a pass-through display pass) leaves
a frame within one code and +1 EV brightens it.
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
TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html", ".json": "application/json", ".wgsl": "text/plain"}


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

    def log_message(self, *a):
        pass


def run(adapter: str, modules: str) -> dict:
    from playwright.sync_api import sync_playwright
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(ROOT)))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
    if adapter == "swiftshader":
        args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"]
    mods = ",".join("/" + m.lstrip("/") for m in modules.split(",") if m)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/post-passes.html?modules={mods}")
            page.wait_for_function("window.__result !== undefined", timeout=1200000)
            res = page.evaluate("window.__result")
            # The stack inside the Motion renderer (web/test/motion-post.html).
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/motion-post.html")
            page.wait_for_function("window.__result !== undefined", timeout=120000)
            res["motion"] = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    res["adapter_kind"] = adapter
    return res


def check(res: dict) -> list[str]:
    if "error" in res:
        return [res["error"]]
    problems = []
    m = res.get("motion", {})
    if "error" in m:
        problems.append("motion: " + m["error"])
    elif m.get("maxAB", 99) > 1 or not m.get("brighter"):
        problems.append(f"motion: a neutral stack changed the frame by {m.get('maxAB')} codes, or +1 EV did not brighten it")
    elif abs(m.get("clip", 0) - 118) > 1 or abs(m.get("aces", 0) - 89) > 2:
        problems.append(f"motion: colour-managed grey encoded to {m.get('clip')} (clip, expect 118) and {m.get('aces')} (ACES 2.0, expect about 89)")
    for name, p in res["passes"].items():
        for r in p.get("runs", []):
            if r["maxCode"] > 1:
                problems.append(f"{name} {r['spec']}: {r['over']} values differ by more than one 8-bit code; worst {r['worst']}")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--modules", default="")
    ap.add_argument("--out")
    a = ap.parse_args()
    res = run(a.adapter, a.modules)
    res["problems"] = check(res)
    res["commit"] = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    res["bound"] = "every channel of every pixel of every frame within one 8-bit code of the pass's CPU reference"
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if res["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
