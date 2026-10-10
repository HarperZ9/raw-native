"""Run the sound engine's live path in Chrome and reconcile it with the offline render.

    python scripts/sound_live_check.py DIR [--seconds S] [--start S] [--port 8790]

DIR holds sheet.resolved.json and mix.wav from web/sound/render.mjs, and
narration.wav when the sheet has narration. DIR must sit inside the repository
(the page fetches it from the same local server). Prints the reconcile block
(identity, max_abs_lsb, exact_frac, snr_db) as JSON. Needs Python Playwright
with Chrome or Chromium. The server stops before the script exits.
"""
from __future__ import annotations

import argparse
import functools
import http.server
import json
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dir", type=Path)
    ap.add_argument("--seconds", type=float, default=None)
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--channel", default="chrome")
    a = ap.parse_args()
    rel = a.dir.resolve().relative_to(ROOT).as_posix()
    class Quiet(http.server.SimpleHTTPRequestHandler):
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".mjs": "text/javascript"}

        def log_message(self, *args):
            pass

    handler = functools.partial(Quiet, directory=str(ROOT))
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        from playwright.sync_api import sync_playwright
        q = f"sheet=/{rel}/sheet.resolved.json&ref=/{rel}/mix.wav&start={a.start}"
        if (a.dir / "narration.wav").is_file():
            q += f"&narration=/{rel}/narration.wav"
        if a.seconds:
            q += f"&seconds={a.seconds}"
        with sync_playwright() as p:
            b = p.chromium.launch(channel=a.channel, args=["--autoplay-policy=no-user-gesture-required"])
            page = b.new_page()
            page.goto(f"http://127.0.0.1:{a.port}/web/sound/check.html?{q}")
            page.wait_for_function("window.result !== undefined", timeout=600_000)
            res = page.evaluate("window.result")
            b.close()
    finally:
        srv.shutdown()
        srv.server_close()
    print(json.dumps(res, indent=1))
    return 0 if "error" not in res and res.get("tolerance", {}).get("verdict") == "verified" else 1


if __name__ == "__main__":
    sys.exit(main())
