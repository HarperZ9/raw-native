#!/usr/bin/env python3
"""M1 criterion 1 diagnostic: the additive blend probe (web/test/blend-probe.html) on an adapter.
    python tests/web/blend_probe.py [--adapter gpu|swiftshader] [--out result.json]"""
import argparse, functools, http.server, json, sys, threading
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".html": "text/html", ".json": "application/json"}
class H(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, p): return TYPES.get(Path(str(p)).suffix, "application/octet-stream")
    def log_message(self, *a): pass
ap = argparse.ArgumentParser(); ap.add_argument("--adapter", default="swiftshader"); ap.add_argument("--out"); a = ap.parse_args()
from playwright.sync_api import sync_playwright
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(H, directory=str(ROOT))); threading.Thread(target=srv.serve_forever, daemon=True).start()
args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"] + (["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"] if a.adapter == "swiftshader" else [])
with sync_playwright() as p:
    b = p.chromium.launch(channel="chrome", headless=True, args=args); pg = b.new_page()
    pg.goto(f"http://localhost:{srv.server_address[1]}/web/test/blend-probe.html"); pg.wait_for_function("window.__result !== undefined", timeout=120000)
    res = pg.evaluate("window.__result"); b.close()
srv.shutdown(); res["adapter_kind"] = a.adapter
print(json.dumps(res, indent=1))
if a.out: Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8")
