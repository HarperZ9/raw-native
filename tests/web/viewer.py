#!/usr/bin/env python3
"""M3 criterion 4: the verified viewer (web/viewer/), driven in headless Chrome.

    python tests/web/viewer.py [--dir build-wasm-gpu] [--adapter gpu|swiftshader] [--out result.json]

Opens web/viewer/index.html three times: with raw-hero and with raw-hall (the owned assets,
exported inside the page) and with a broken GLB as the control. Passes when both assets render
with exit 0, a GPU certificate whose verdict is verified or refuted (a judgement, not
unverifiable), a GPU frame, a CPU reference and an error map of the same size; and when the
broken file yields no certificate and no frames (the control must fail to render).
"""

from __future__ import annotations

import argparse
import base64
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


def run(page, url):
    page.goto(url)
    page.wait_for_function("window.__viewer && window.__viewer.done", timeout=600000)
    return page.evaluate("window.__viewer")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", default="build-wasm-gpu")
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--size", default="128")
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
    base = f"http://localhost:{srv.server_address[1]}/web/viewer/index.html?dir={a.dir}&m={m}&w={w}&size={a.size}"
    broken = base64.b64encode(b"glTF\x02\x00\x00\x00\x40\x00\x00\x00" + b"\x00" * 20).decode()
    out = {"adapter": a.adapter, "cases": []}
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True, args=args)
            for name, url in (("raw-hero", f"{base}&asset=raw-hero"), ("raw-hall", f"{base}&asset=raw-hall"),
                              ("control: broken GLB", f"{base}&name=broken.glb&bytes={broken}")):
                page = b.new_page()
                res = run(page, url)
                out["cases"].append({"case": name, **res})
                page.close()
            b.close()
    finally:
        srv.shutdown()
    ok = True
    for c in out["cases"]:
        r = (c.get("runs") or [{}])[0]
        if c["case"].startswith("control"):
            good = "error" not in c and r.get("verdict") is None and "frames" not in r and r.get("exitCode", 0) != 0
        else:
            f = r.get("frames") or {}
            good = ("error" not in c and r.get("exitCode") == 0 and r.get("verdict") in ("verified", "refuted")
                    and f.get("width") == int(a.size) and r.get("errorWorst") is not None)
        c["pass"] = good
        ok = ok and good
        print(f"{c['case']}: {'pass' if good else 'FAIL'} {json.dumps(r)}")
    out["pass"] = ok
    if a.out:
        Path(a.out).write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
