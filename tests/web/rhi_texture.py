#!/usr/bin/env python3
"""M2 criterion 3 and the M3 material parity, the WebGPU half: a GPU check of the WebGPU build in a browser.

    python tests/web/rhi_texture.py [--command texture-identity|pbr-parity] [--dir build-wasm-gpu]
                                    [--adapter gpu|swiftshader] [--out result.json]

Serves the repository, opens web/test/texture-identity.html in headless Chrome, which runs
`raw_native_cli <command>` from the WebAssembly WebGPU build, and reads its JSON.
Passes when the CLI exits 0, which means every case is within the bounds in
evidence/m2-rhi-texture-bounds.json (texture-identity) or evidence/m3-materials-bounds.json
(pbr-parity) and the control fails. Without a WebGPU adapter it fails
(an adapter is the point of the check).
"""

from __future__ import annotations

import argparse
import functools
import hashlib
import http.server
import json
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html", ".wasm": "application/wasm", ".json": "application/json"}


class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

    def log_message(self, *a):
        pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--command", choices=["texture-identity", "pbr-parity", "lighting-parity", "raster-identity", "shadow-parity", "post-parity", "swr-parity", "rt-bvh-parity", "rt-pt-parity"], default="texture-identity")
    ap.add_argument("--dir", default="build-wasm-gpu")
    ap.add_argument("--args", default="", help="extra CLI flags, comma separated (diagnosis)")
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--out")
    a = ap.parse_args()
    sha = lambda p: hashlib.sha256((ROOT / a.dir / p).read_bytes()).hexdigest()
    m, w = sha("raw-native-gpu.mjs"), sha("raw-native-gpu.wasm")
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
            logs = []
            page.on("console", lambda msg: logs.append(msg.text))
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/texture-identity.html?dir={a.dir}&m={m}&w={w}&cmd={a.command}&args={a.args}")
            page.wait_for_function("window.__result !== undefined", timeout=300000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    print(json.dumps(res, indent=1))
    if "error" in res:
        print("\n".join(logs[-20:]), file=sys.stderr)
        return 1
    body = res["stdout"][res["stdout"].find("{"):]
    if a.out:
        Path(a.out).write_text(body.strip() + "\n", encoding="utf-8", newline="\n")
    return 0 if res["exitCode"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
