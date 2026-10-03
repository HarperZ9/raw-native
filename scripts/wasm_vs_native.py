#!/usr/bin/env python3
"""Render the README's five views at 512 x 512 with the native CLI and with the
wasm build under Node, then compare each pair with compare_renders.py.
Usage: wasm_vs_native.py <raw_native_cli> <raw-native.mjs> <workdir> <out.json>"""
import json, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from compare_renders import compare  # noqa: E402

VIEWS = {
    "default": [],
    "high": ["--eye", "0,9,3", "--target", "0,0.5,0"],
    "low": ["--eye", "6,1.5,2", "--target", "0,1,0"],
    "close": ["--eye", "2,2.5,3", "--target", "0,0.8,0", "--fovy", "0.7"],
    "wide": ["--eye", "5,5,8", "--target", "0,0.5,0", "--fovy", "1.2"],
}
ROOT = Path(__file__).resolve().parent.parent


def main():
    cli, mjs, work, out = sys.argv[1:5]
    work = Path(work)
    report = {}
    for name, flags in VIEWS.items():
        n, w = work / f"{name}-native", work / f"{name}-wasm"
        n.mkdir(parents=True, exist_ok=True)
        size = ["--width", "512", "--height", "512"]
        subprocess.run([cli, "--out", str(n), *size, *flags], check=True, capture_output=True)
        subprocess.run(["node", str(ROOT / "wasm" / "run-node.mjs"), mjs, str(w), *size, *flags],
                       check=True, capture_output=True)
        rep = compare(n, w)
        report[name] = {k: rep[k] for k in ("a", "b", "delta", "within_tolerance")}
        report[name]["bitwise_identical_files"] = len(rep["bitwise_identical_outputs"])
        report[name]["files"] = len(json.loads((n / "certificate.json").read_text())["outputs"])
        print(name, rep["b"]["verdict"], rep["delta"], rep["within_tolerance"],
              f'{report[name]["bitwise_identical_files"]}/{report[name]["files"]} files identical', flush=True)
    Path(out).write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
