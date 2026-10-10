#!/usr/bin/env python3
"""M1 exit criterion 4: the vector corpus on the GPU against the CPU reference, in a real browser.

    python tests/web/vector_corpus.py [--adapter gpu|swiftshader] [--out evidence.json] [--sheet sheet.png]

Node writes the CPU reference images (web/motion/vector_ref.mjs) for every case of
evidence/m1-vector-corpus.json. Headless Chrome renders the same items with the
Motion vector pass (compile() and VECTOR_WGSL) and measures the difference by pixel
class. Passes when every case is inside the bounds the corpus file states.
--sheet saves a contact sheet: each case as GPU then reference, over a dark ground.
"""

from __future__ import annotations

import argparse
import base64
import functools
import http.server
import json
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html", ".json": "application/json"}


def handler(data_dir: Path):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def translate_path(self, path):
            if path.startswith("/_data/"):
                return str(data_dir / path[len("/_data/"):].split("?")[0])
            return super().translate_path(path)

        def guess_type(self, path):
            return TYPES.get(Path(str(path)).suffix, "application/octet-stream")

        def log_message(self, *a):
            pass
    return functools.partial(Handler, directory=str(ROOT))


def run(adapter: str) -> dict:
    from playwright.sync_api import sync_playwright
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        subprocess.run(["node", str(ROOT / "tests" / "web" / "vector_corpus.mjs"), str(tmp)], check=True, capture_output=True)
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler(tmp))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
        if adapter == "swiftshader":
            args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"]
        try:
            with sync_playwright() as p:
                b = p.chromium.launch(channel="chrome", headless=True, args=args)
                page = b.new_page()
                page.goto(f"http://localhost:{srv.server_address[1]}/web/test/vector-corpus.html")
                page.wait_for_function("window.__result !== undefined", timeout=600000)
                res = page.evaluate("window.__result")
                b.close()
        finally:
            srv.shutdown()
    res["adapter_kind"] = adapter
    return res


def check(res: dict, corpus: dict) -> list[str]:
    if "error" in res:
        return [res["error"]]
    b = corpus["bounds"]
    sharp = set(b["sharp"]["cases"])
    problems = []
    for c in corpus["cases"]:
        r = res["cases"].get(c["id"])
        if r is None:
            problems.append(f"{c['id']}: not rendered")
            continue
        edge_max = b["sharp"]["max"] if c["id"] in sharp else b["edge"]["max"]
        if r["nan"]:
            problems.append(f"{c['id']}: {r['nan']} non-finite values")
        if r["interior"]["max"] > b["interior"]["max"]:
            problems.append(f"{c['id']}: interior differs by {r['interior']['max']:.4f} (bound {b['interior']['max']})")
        if r["outside"]["max"] > b["outside"]["max"]:
            problems.append(f"{c['id']}: draws {r['outside']['max']:.4f} outside the shape")
        if r["edge"]["max"] > edge_max:
            problems.append(f"{c['id']}: an edge pixel differs by {r['edge']['max']:.4f} (bound {edge_max}) at {r['worst']}")
        if r["edge"]["mean"] > b["edge"]["mean"]:
            problems.append(f"{c['id']}: edge pixels differ by {r['edge']['mean']:.4f} on average (bound {b['edge']['mean']})")
        if r["stats"]["items"] != 1:
            problems.append(f"{c['id']}: the compiler culled the item")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--out")
    ap.add_argument("--sheet")
    a = ap.parse_args()
    corpus = json.loads((ROOT / "evidence" / "m1-vector-corpus.json").read_text(encoding="utf-8"))
    res = run(a.adapter)
    sheet = res.pop("sheet", None)
    if a.sheet and sheet:
        Path(a.sheet).write_bytes(base64.b64decode(sheet.split(",", 1)[1]))
    res["problems"] = check(res, corpus)
    res["commit"] = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    res["does_not_prove"] = corpus["does_not_prove"]
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if res["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
