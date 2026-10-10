#!/usr/bin/env python3
"""M1 exit criterion 2: the WGSL colour pipelines against the C++ reference, in a real browser.

    python tests/web/colour_grid.py --cli build/Release/raw_native_cli.exe [--adapter gpu|swiftshader] [--out evidence.json]

The C++ reference writes the committed grid (evidence/m1-colour-bounds.json) and its
output for every pipeline. Headless Chrome runs web/colour/colour.wgsl over the same
float32 inputs and the page compares the two at 8-bit output. Passes when every
channel of every input is within one 8-bit code and nothing is NaN.

It also checks that the committed ACES 2.0 tables under web/colour/tables/ match
the tables this build of the reference produces, to a relative 1e-5 (libm differs
in the last bits between compilers, so this is not a byte comparison).
"""

from __future__ import annotations

import argparse
import functools
import hashlib
import http.server
import json
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html", ".wgsl": "text/plain",
         ".json": "application/json", ".f32": "application/octet-stream"}


def handler(data_dir: Path):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def translate_path(self, path):
            if path.startswith("/_data/"):
                return str(data_dir / path[len("/_data/"):].split("?")[0])
            return super().translate_path(path)

        def guess_type(self, path):
            return TYPES.get(Path(str(path)).suffix, super().guess_type(path))

        def log_message(self, *a):
            pass
    return functools.partial(Handler, directory=str(ROOT))


def reference(cli: str, tmp: Path) -> list[str]:
    run = lambda *a: subprocess.run([cli, "colour", *a], check=True, capture_output=True, text=True).stdout  # noqa: E731
    run("grid", str(tmp / "grid.f32"))
    names = run("list").split()
    for n in names:
        f = n.replace("/", "_")
        run("apply", n, str(tmp / "grid.f32"), str(tmp / f"{f}.f32"))
        if n.startswith("aces2"):
            run("tables", n, str(tmp / f"{f}.json"))
    return names


def tables_match(tmp: Path, names: list[str]) -> list[str]:
    problems = []
    for n in (n for n in names if n.startswith("aces2")):
        f = n.replace("/", "_") + ".json"
        fresh = json.loads((tmp / f).read_text(encoding="utf-8"))
        committed = json.loads((ROOT / "web" / "colour" / "tables" / f).read_text(encoding="utf-8"))

        def walk(a, b, path):
            if isinstance(a, dict):
                for k in a:
                    walk(a[k], b.get(k), f"{path}.{k}")
            elif isinstance(a, list):
                if not isinstance(b, list) or len(a) != len(b):
                    problems.append(f"{n}: {path} has a different length")
                    return
                for i, (x, y) in enumerate(zip(a, b)):
                    walk(x, y, f"{path}[{i}]")
            elif isinstance(a, (int, float)):
                if not isinstance(b, (int, float)) or abs(a - b) > 1e-5 * max(1.0, abs(a)):
                    problems.append(f"{n}: {path} is {b} in the committed tables and {a} from this build")
            elif a != b:
                problems.append(f"{n}: {path} differs")
        walk(fresh, committed, "")
    return problems[:20]


def run(cli: str, adapter: str) -> dict:
    from playwright.sync_api import sync_playwright
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        names = reference(str(Path(cli).resolve()), tmp)
        table_problems = tables_match(tmp, names)
        grid_sha = hashlib.sha256((tmp / "grid.f32").read_bytes()).hexdigest()
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler(tmp))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        args = ["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
        if adapter == "swiftshader":
            args += ["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"]
        try:
            with sync_playwright() as p:
                b = p.chromium.launch(channel="chrome", headless=True, args=args)
                page = b.new_page()
                page.goto(f"http://localhost:{srv.server_address[1]}/web/test/colour-grid.html")
                page.wait_for_function("window.__result !== undefined", timeout=300000)
                res = page.evaluate("window.__result")
                b.close()
        finally:
            srv.shutdown()
    commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    res.update({"adapter_kind": adapter, "commit": commit, "grid_sha256": grid_sha, "table_problems": table_problems})
    return res


def check(res: dict) -> list[str]:
    if "error" in res:
        return [res["error"]]
    problems = list(res["table_problems"])
    for name, r in res["pipelines"].items():
        if r["nan"]:
            problems.append(f"{name}: {r['nan']} non-finite GPU values")
        if r["maxCode"] > 1:
            problems.append(f"{name}: {r['over']} values differ from the C++ reference by more than one 8-bit code "
                            f"(worst {r['maxCode']} at input {r['worstInput']})")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cli", required=True)
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--out")
    a = ap.parse_args()
    res = run(a.cli, a.adapter)
    res["problems"] = check(res)
    res["bound"] = "every channel of every input within one 8-bit code of the C++ reference (evidence/m1-colour-bounds.json)"
    res["does_not_prove"] = "Agreement on this adapter and browser, on this grid. Not on other GPUs, and not what a display shows."
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if res["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
