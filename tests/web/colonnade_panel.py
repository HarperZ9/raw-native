#!/usr/bin/env python3
"""The colonnade's live settings panel (web/colonnade/), driven in headless Chrome.

    python tests/web/colonnade_panel.py [--dir build-wasm] [--out result.json]

Checks l1 to l4 of evidence/colonnade-panel-bounds.json: the page loads the engine's WebAssembly
build (hash-checked), renders the default settings (noon), then renders again after the test sets
sunset; every run exits 0 with a PNG, the engine echoes the settings it was given, and the frame's
mean luminance changes by at least 10% between noon and sunset at the same exposure.
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


def echoes(run: dict) -> bool:
    j, s = run.get("json") or {}, run.get("settings") or {}
    return (abs(j.get("sun_elevation", -1) - s.get("elev", -2)) < 1e-6 and abs(j.get("sun_azimuth", -1) - s.get("az", -2)) < 1e-6
            and abs(j.get("turbidity", -1) - s.get("turbidity", -2)) < 1e-6 and abs(j.get("ev100", -1) - s.get("ev", -2)) < 1e-6
            and j.get("emission") == s.get("emission") and j.get("spp") == run.get("spp"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", default="build-wasm")
    ap.add_argument("--out")
    a = ap.parse_args()
    sha = lambda p: hashlib.sha256((ROOT / a.dir / p).read_bytes()).hexdigest()
    m, w = sha("raw-native.mjs"), sha("raw-native.wasm")
    from playwright.sync_api import sync_playwright
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(ROOT)))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = f"http://localhost:{srv.server_address[1]}/web/colonnade/index.html?dir={a.dir}&m={m}&w={w}"
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel="chrome", headless=True)
            page = b.new_page()
            page.goto(url)
            page.wait_for_function("window.__colonnade && (window.__colonnade.ready || window.__colonnade.error)", timeout=600000)
            page.evaluate("window.__setSettings({elev: 2, az: 30, turbidity: 4.5, ev: 15})")
            state = page.evaluate("window.__colonnade")
            if a.out:
                page.screenshot(path=str(Path(a.out).with_suffix(".png")), full_page=True)   # for review
            b.close()
    finally:
        srv.shutdown()
    runs = state.get("runs", [])
    noon = [r for r in runs if r["settings"]["elev"] == 60 and r["spp"] == 16]
    sunset = [r for r in runs if r["settings"]["elev"] == 2 and r["spp"] == 16]
    l1 = len(runs) == 4 and all(r["exitCode"] == 0 and r["hasPng"] for r in runs)
    l2 = l1 and all(echoes(r) for r in runs)
    change = None
    if noon and sunset and noon[0]["meanLuminance"]:
        change = abs(sunset[0]["meanLuminance"] - noon[0]["meanLuminance"]) / noon[0]["meanLuminance"]
    l3 = change is not None and change >= 0.10
    out = {"error": state.get("error"), "runs": runs, "l1_renders": l1, "l2_echo": l2, "l3_relative_change": change, "l3_changes": l3,
           "l4_page_ms": [round(r["pageMs"], 1) for r in runs], "pass": l1 and l2 and l3}
    for r in runs:
        print(f"{r['settings']} spp {r['spp']}: exit {r['exitCode']}, png {r['hasPng']}, mean {r['meanLuminance']}, {r['pageMs']:.0f} ms in the page")
    print(f"l1 {l1}, l2 {l2}, l3 change {change} ({l3}); pass {out['pass']}")
    if a.out:
        Path(a.out).write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 0 if out["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
