#!/usr/bin/env python3
"""M2 criterion 1, the WebGPU half: the ten glTF models on the WebGPU build in a browser.

    python tests/web/gpu_models.py <models dir> [--dir build-wasm-gpu] [--adapter gpu|swiftshader] [--out result.json]

Serves the repository, and <models dir> (fetched by tools/assets/fetch_gltf.py) under
/models/, opens web/test/gpu-models.html, which renders each model with --gpu on the WebGPU
build. Passes when every model exits 0 with a verified GPU certificate.
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


def handler(models: Path, listing: bytes):
    class H(http.server.SimpleHTTPRequestHandler):
        def guess_type(self, path):
            return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

        def log_message(self, *a):
            pass

        def do_GET(self):
            if self.path == "/models/list.json":
                return self._send(listing, "application/json")
            if self.path.startswith("/models/"):
                f = (models / self.path[len("/models/"):]).resolve()
                if models.resolve() in f.parents and f.is_file():
                    return self._send(f.read_bytes(), "application/octet-stream")
                return self.send_error(404)
            return super().do_GET()

        def _send(self, data, ctype):
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
    return functools.partial(H, directory=str(ROOT))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("models")
    ap.add_argument("--dir", default="build-wasm-gpu")
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--out")
    a = ap.parse_args()
    spec = json.loads((ROOT / "evidence" / "m2-gltf-models.json").read_text(encoding="utf-8"))
    listing = json.dumps([{"name": m["name"], "files": [f["path"] for f in m["files"]]} for m in spec["models"]]).encode()
    sha = lambda p: hashlib.sha256((ROOT / a.dir / p).read_bytes()).hexdigest()
    from playwright.sync_api import sync_playwright
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler(Path(a.models), listing))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
    if a.adapter == "swiftshader":
        args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"]
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            page = b.new_page()
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/gpu-models.html?dir={a.dir}&m={sha('raw-native-gpu.mjs')}&w={sha('raw-native-gpu.wasm')}")
            page.wait_for_function("window.__result !== undefined", timeout=900000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    res["adapter_kind"] = a.adapter
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    ok = "error" not in res and len(res["models"]) == 10 and all(m["exit"] == 0 and m["verdict"] == "verified" for m in res["models"])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
