#!/usr/bin/env python3
"""The shader library: GPU passes against their CPU references, in a real browser.

    python tests/web/shaders_parity.py --shader crt|film [--adapter gpu|swiftshader] [--case ID] [--out evidence.json]
    python tests/web/shaders_parity.py --shader crt --timing --preset pvm-20 --width 3840 --height 2880 [--adapter gpu]

Parity mode opens web/test/shaders-<shader>.html, which renders every case in
evidence/shaders-<shader>-parity-bounds.json through the CPU reference and the WebGPU passes and
compares the 8-bit encodes. Passes when every case is inside the bounds in that file,
which were committed before the first run. Timing mode reports per-pass GPU time.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import sys
import threading
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html", ".json": "application/json"}


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, super().guess_type(path))

    def log_message(self, *a):
        pass


def run(shader: str, adapter: str, query: dict, timeout_ms: int) -> dict:
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
            qs = urllib.parse.urlencode(query)
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/shaders-{shader}.html?{qs}")
            page.wait_for_function("window.__result !== undefined", timeout=timeout_ms)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    res["adapter_kind"] = adapter
    return res


def check(res: dict) -> list[str]:
    if "error" in res:
        return [res["error"]]
    b, problems = res["bounds"], []
    if "mismatched_pixels" in b:
        return [f"{c['id']}: {c['mismatched']} pixels differ (first at {c['firstMismatch']})" for c in res["cases"] if c["mismatched"] > b["mismatched_pixels"]]
    for c in res["cases"]:
        if c["max"] > b["max_abs_code"] or c["p999"] > b["p999_abs_code"] or c["mean"] > b["mean_abs_code"]:
            problems.append(f"{c['id']}: max {c['max']}, p99.9 {c['p999']}, mean {c['mean']:.4f} (bounds {b})")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--shader", choices=["crt", "film", "dither", "classic"], required=True)
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--case")
    ap.add_argument("--exhaustive", help="dither only: palette:mode over all 16,777,216 colours")
    ap.add_argument("--timing", action="store_true")
    ap.add_argument("--preset", default="pvm-20")
    ap.add_argument("--overrides")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1440)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.timing:
        q = {"timing": 1, "preset": a.preset, "w": a.width, "h": a.height, "frames": a.frames}
        if a.overrides:
            q["overrides"] = a.overrides
        res = run(a.shader, a.adapter, q, 600000)
        problems = [res["error"]] if "error" in res else []
    else:
        q = {"case": a.case} if a.case else {}
        if a.exhaustive:
            q = {"exhaustive": a.exhaustive}
        res = run(a.shader, a.adapter, q, 3600000)
        problems = check(res)
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
