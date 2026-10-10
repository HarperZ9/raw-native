#!/usr/bin/env python3
"""M2 criterion 9, the GPU half: the isometric occlusion fade in a real browser.

    python tests/web/iso_fade.py [--adapter gpu|swiftshader] [--out evidence.json]

Opens web/test/iso-fade.html. Bounds (evidence/m2-iso-fade-bounds.json, written before
the first run): the faded tile pass matches tilesRef with the same fade within 2^-11,
and against the unfaded frame at least 100 pixels change by more than 0.05, every one
of them in a cell the mask fades.
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
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/iso-fade.html")
            page.wait_for_function("window.__result !== undefined", timeout=600000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    problems = [res["error"]] if "error" in res else []
    for c in res.get("cases", []):
        tag = f"scale {c['scale']}"
        if c["covered"] < 1000 or c["fadedCells"] < 3:
            problems.append(f"{tag}: too little drawn or faded for the comparison to prove much ({c['covered']} px, {c['fadedCells']} cells)")
        if c["maxAbs"] > 2 ** -11:
            problems.append(f"{tag}: differs from the CPU twin by {c['maxAbs']}, first {c['first']}")
        if c["changed"] < 100:
            problems.append(f"{tag}: the fade changed only {c['changed']} pixels")
        if c["outside"]:
            problems.append(f"{tag}: {c['outside']} changed pixels lie outside the faded cells, first {c['firstOutside']}")
    res.update({"adapter_kind": a.adapter, "problems": problems,
                "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip(),
                "does_not_prove": "That the fade reads well to a player, or behaviour for sprites (the fade covers tile layers only)."})
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
