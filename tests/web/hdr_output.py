#!/usr/bin/env python3
"""M1 exit criterion 3, the engine half: extended-range output through the whole Motion frame.

    python tests/web/hdr_output.py --cli build/Release/raw_native_cli.exe [--adapter gpu|swiftshader] [--out evidence.json]

Headless Chrome opens web/test/hdr.html. The page asks for a canvas in extended
tone-mapping mode and renders a patch at SDR white and one at 10 times SDR white,
in linear light, through aces2-hdr1000/srgb-extended. It reads the frame back as
floats. Passes when (--require-canvas: also when the browser grants the mode and a frame draws to it;
headless Chrome on Linux with SwiftShader has no WebGPU canvas, so CI records the canvas
result without requiring it):
- both patches match the C++ reference for the same inputs within 0.5%;
- the bright patch is above 1, so above SDR white.
The HDR signal itself (PQ, the extended sRGB curve, the tone map) is checked against
the published formulas by tests/web/hdr_signal.py (criterion 3, amended 2026-10-10).
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".html": "text/html", ".json": "application/json", ".wgsl": "text/plain"}


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

    def log_message(self, *a):
        pass


def reference(cli: str) -> list[float]:
    with tempfile.TemporaryDirectory() as t:
        src, dst = Path(t) / "in.f32", Path(t) / "out.f32"
        src.write_bytes(struct.pack("<6f", 1, 1, 1, 10, 10, 10))
        subprocess.run([cli, "colour", "apply", "aces2-hdr1000/srgb-extended", str(src), str(dst)], check=True)
        v = struct.unpack("<6f", dst.read_bytes())
    return [v[0], v[3]]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cli", required=True)
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--require-canvas", action="store_true", help="fail when the browser gives no extended-range canvas")
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
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/hdr.html")
            page.wait_for_function("window.__result !== undefined", timeout=120000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    white, bright = reference(str(Path(a.cli).resolve()))
    res.update({"adapter_kind": a.adapter, "reference": {"white": white, "bright": bright}})
    problems = []
    if "error" in res:
        problems.append(res["error"])
    else:
        c = res["canvas"]
        granted = c.get("hdr") and c.get("toneMapping") == "extended" and c.get("drew")
        res["canvas_granted"] = bool(granted)
        if a.require_canvas and not granted:
            problems.append(f"the browser did not give an extended-range canvas: {c}")
        for k, ref in (("white", white), ("bright", bright)):
            if abs(res[k] - ref) > 0.005 * abs(ref) + 1e-3:
                problems.append(f"{k} patch: {res[k]} on the GPU, {ref} from the C++ reference")
        if not res.get("bright", 0) > 1.0:
            problems.append("the bright patch did not pass SDR white")
        if res.get("errors"):
            problems += res["errors"]
    res["problems"] = problems
    res["does_not_prove"] = "What a display shows: the canvas maps 1.0 to the system's SDR white, which the viewer sets."
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
