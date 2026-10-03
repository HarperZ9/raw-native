#!/usr/bin/env python3
"""Compare two raw-native render directories of the same params, for example
the native CLI against the WebAssembly build.

The tolerance below was fixed before any wasm output existed (2026-10-03):
  pixels      identical (coverage comes from rasterization, no transcendental math)
  verdict     identical
  rmse        |difference| <= 1e-4
  maxError    |difference| <= 1/64 (one ray-traced sample out of 64)
Different libm implementations (MSVC CRT, glibc, musl in Emscripten) may round
sin, cos and sqrt differently, so bitwise identity is reported but not required.

Usage: compare_renders.py <dirA> <dirB> [--json out.json]   exit 0 within tolerance, 3 outside
"""
import hashlib, json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from recheck import read_pfm1, recheck  # noqa: E402

TOL_RMSE = 1e-4
TOL_MAX = 1.0 / 64.0


def ao_diff(a, b, name):
    w, h, ra = read_pfm1(a / name)
    _, _, rb = read_pfm1(b / name)
    diffs = [abs(ra[y][x] - rb[y][x]) for y in range(h) for x in range(w)]
    return {"pixels_differing": sum(d > 0 for d in diffs), "max_abs_diff": max(diffs), "of": w * h}


def compare(a, b):
    a, b = Path(a), Path(b)
    ca = json.loads((a / "certificate.json").read_text())
    cb = json.loads((b / "certificate.json").read_text())
    ra, rb = recheck(a), recheck(b)
    ea, eb = ca["exact"], cb["exact"]
    rep = {
        "a": {"dir": a.name, "renderer": ca["renderer"], "verdict": ca["verdict"], **ea, "self_recheck_exit": ra[0]},
        "b": {"dir": b.name, "renderer": cb["renderer"], "verdict": cb["verdict"], **eb, "self_recheck_exit": rb[0]},
        "params_identical": ca["params"] == cb["params"],
        "tolerance": {"rmse_abs": TOL_RMSE, "maxError_abs": TOL_MAX, "pixels": "identical", "verdict": "identical"},
        "delta": {"pixels": eb["pixels"] - ea["pixels"], "rmse": abs(eb["rmse"] - ea["rmse"]),
                  "maxError": abs(eb["maxError"] - ea["maxError"])},
        "bitwise_identical_outputs": sorted(k for k in ca["outputs"] if ca["outputs"].get(k) == cb["outputs"].get(k)),
        "ao_rt": ao_diff(a, b, "ao_rt.pfm"),
        "ao_ss": ao_diff(a, b, "ao_ss.pfm"),
    }
    ok = (rep["params_identical"] and ra[0] == 0 and rb[0] == 0 and rep["delta"]["pixels"] == 0
          and ca["verdict"] == cb["verdict"] and rep["delta"]["rmse"] <= TOL_RMSE
          and rep["delta"]["maxError"] <= TOL_MAX)
    rep["within_tolerance"] = ok
    return rep


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    rep = compare(sys.argv[1], sys.argv[2])
    text = json.dumps(rep, indent=2)
    if "--json" in sys.argv:
        Path(sys.argv[sys.argv.index("--json") + 1]).write_text(text + "\n")
    print(text)
    sys.exit(0 if rep["within_tolerance"] else 3)
