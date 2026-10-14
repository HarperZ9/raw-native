#!/usr/bin/env python3
"""The Canvas 2D fallback (web/motion/canvas2d.mjs) against the CPU reference, on the vector corpus.

    python tests/web/canvas2d_corpus.py [--out evidence.json] [--sheet sheet.png]

Node writes the CPU reference images (web/motion/vector_ref.mjs) for every case of
evidence/m1-vector-corpus.json, as for the GPU check (tests/web/vector_corpus.py).
Headless Chrome draws the same items with drawFrame() on a 2D canvas, which needs no
WebGPU, and the difference is measured by the corpus's pixel classes. Passes when every
case is inside BOUNDS below. The bounds were set before the first run and are looser
than the GPU's: the canvas is 8-bit, unpremultiplied and antialiased by the browser's
own rasteriser, so edges and gradients are not expected to match the reference as
closely. They show that the fallback draws the same picture, not the same pixels.
"""

from __future__ import annotations

import argparse
import base64
import http.server
import json
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vector_corpus import handler  # noqa: E402

BOUNDS = {
    "interior": {"max": 0.02, "why": "8-bit canvas: rounding and gradient or image interpolation in 8 bits"},
    "outside": {"max": 0.004, "why": "one 8-bit code at most, at least 1.5 px from every edge"},
    "edge": {"mean": 0.05, "max": 0.5, "why": "the browser's own antialiasing against box-filtered coverage"},
    "sharp": {"max": 0.7, "why": "miter tips and star points narrower than a pixel"},
}

# Cases where the reference itself is wrong at a known place, each with its evidence. Their
# edge maximum is reported but does not fail the check; every other bound still applies.
KNOWN = {
    "stroke-curve": "the reference covers about 2 px past the butt cap at the curve's end, where the canvas "
                    "correctly draws nothing (pixel (240, 127) lies wholly past the cap; issue #123)",
}


def run() -> dict:
    from playwright.sync_api import sync_playwright
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        subprocess.run(["node", str(ROOT / "tests" / "web" / "vector_corpus.mjs"), str(tmp)], check=True, capture_output=True)
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler(tmp))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        try:
            with sync_playwright() as p:
                b = p.chromium.launch(channel="chrome", headless=True)
                page = b.new_page()
                page.goto(f"http://localhost:{srv.server_address[1]}/web/test/canvas2d-corpus.html", timeout=180000)
                page.wait_for_function("window.__result !== undefined", timeout=300000)
                res = page.evaluate("window.__result")
                b.close()
        finally:
            srv.shutdown()
    return res


def check(res: dict, corpus: dict) -> list[str]:
    if "error" in res:
        return [res["error"]]
    sharp = set(corpus["bounds"]["sharp"]["cases"])
    problems = []
    for c in corpus["cases"]:
        r = res["cases"].get(c["id"])
        if r is None:
            problems.append(f"{c['id']}: not drawn")
            continue
        edge_max = BOUNDS["sharp"]["max"] if c["id"] in sharp else BOUNDS["edge"]["max"]
        if r["interior"]["max"] > BOUNDS["interior"]["max"]:
            problems.append(f"{c['id']}: interior differs by {r['interior']['max']:.4f} (bound {BOUNDS['interior']['max']}) at {r['worst']}")
        if r["outside"]["max"] > BOUNDS["outside"]["max"]:
            problems.append(f"{c['id']}: draws {r['outside']['max']:.4f} outside the shape")
        if r["edge"]["max"] > edge_max and c["id"] not in KNOWN:
            problems.append(f"{c['id']}: an edge pixel differs by {r['edge']['max']:.4f} (bound {edge_max}) at {r['worst']}")
        if r["edge"]["mean"] > BOUNDS["edge"]["mean"]:
            problems.append(f"{c['id']}: edge pixels differ by {r['edge']['mean']:.4f} on average (bound {BOUNDS['edge']['mean']})")
        if r["stats"]["items"] != 1:
            problems.append(f"{c['id']}: the item was not drawn ({r['stats']})")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out")
    ap.add_argument("--sheet")
    a = ap.parse_args()
    corpus = json.loads((ROOT / "evidence" / "m1-vector-corpus.json").read_text(encoding="utf-8"))
    res = run()
    sheet = res.pop("sheet", None)
    if a.sheet and sheet:
        Path(a.sheet).write_bytes(base64.b64decode(sheet.split(",", 1)[1]))
    res["bounds"] = BOUNDS
    res["known"] = KNOWN
    res["problems"] = check(res, corpus)
    res["commit"] = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    res["does_not_prove"] = ("Twenty single items on a 256 x 256 canvas in one browser. It does not show that whole scenes match, "
                             "and it says nothing about the layers and post passes the fallback leaves out.")
    print(json.dumps({k: v for k, v in res.items() if k != "cases"}, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if res["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
