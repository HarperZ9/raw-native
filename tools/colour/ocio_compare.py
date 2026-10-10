#!/usr/bin/env python3
"""Compare the C++ colour reference with OpenColorIO 2.6.0, offline (M1 exit criterion 2).

    python tools/colour/ocio_compare.py --cli build/Release/raw_native_cli.exe [--out evidence/m1-colour-ocio.json]

Needs `pip install opencolorio==2.6.0 numpy` in the Python that runs it. OCIO is a
reference run on the author's machine, never a build or test dependency of the engine.
The grid, the pipelines, the OCIO transforms each pipeline is compared with, and the
bounds are fixed in evidence/m1-colour-bounds.json, written before the first run.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from colour_metrics import ciede2000_display, delta_e_itp_pq  # noqa: E402

CONFIG = "studio-config-v5.0.0_aces-v2.1_ocio-v2.6"
OUT_BUILTIN = "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - "


def ocio_processor(ocio, config, pipeline: str):
    """The OCIO transform group the bounds file names for a pipeline, and whether to clamp after it."""
    cst = lambda a, b: ocio.ColorSpaceTransform(src=a, dst=b)  # noqa: E731
    bt = lambda name: ocio.BuiltinTransform(style=name)  # noqa: E731
    lin = "Linear Rec.709 (sRGB)"
    groups = {
        "clip/srgb": ([cst(lin, "sRGB Encoded Rec.709 (sRGB)")], True),
        "clip/display-p3": ([cst(lin, "sRGB Encoded P3-D65")], True),
        "clip/rec2020": ([cst(lin, "ACES2065-1"), bt("UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD"),
                          bt("DISPLAY - CIE-XYZ-D65_to_REC.1886-REC.2020")], True),
        "aces2-sdr/srgb": ([cst(lin, "ACES2065-1"), bt(OUT_BUILTIN + "SDR-100nit-REC709_2.0"),
                            bt("DISPLAY - CIE-XYZ-D65_to_sRGB")], False),
        "aces2-sdr/display-p3": ([cst(lin, "ACES2065-1"), bt(OUT_BUILTIN + "SDR-100nit-P3-D65_2.0"),
                                  bt("DISPLAY - CIE-XYZ-D65_to_DisplayP3")], False),
        "aces2-hdr1000/rec2100-pq": ([cst(lin, "ACES2065-1"), bt(OUT_BUILTIN + "HDR-1000nit-P3-D65_2.0"),
                                      bt("DISPLAY - CIE-XYZ-D65_to_REC.2100-PQ")], False),
    }
    if pipeline not in groups:
        return None, False
    ts, clamp = groups[pipeline]
    g = ocio.GroupTransform()
    for t in ts:
        g.appendTransform(t)
    return config.getProcessor(g).getDefaultCPUProcessor(), clamp


def run_cli(cli: str, *args: str) -> None:
    subprocess.run([cli, "colour", *args], check=True)


def stats(d: np.ndarray) -> dict:
    return {"max": round(float(d.max()), 6), "p99": round(float(np.percentile(d, 99)), 6),
            "mean": round(float(d.mean()), 6), "median": round(float(np.median(d)), 6)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cli", required=True)
    ap.add_argument("--bounds", default="evidence/m1-colour-bounds.json")
    ap.add_argument("--out")
    a = ap.parse_args()
    a.cli = str(Path(a.cli).resolve())
    import PyOpenColorIO as ocio
    bounds = json.loads(Path(a.bounds).read_text(encoding="utf-8"))["bounds"]
    config = ocio.Config.CreateFromBuiltinConfig(CONFIG)
    results, ok = {}, True
    with tempfile.TemporaryDirectory() as tmp:
        grid_path = Path(tmp) / "grid.f32"
        run_cli(a.cli, "grid", str(grid_path))
        grid_bytes = grid_path.read_bytes()
        grid = np.frombuffer(grid_bytes, dtype="<f4").reshape(-1, 3)
        names = subprocess.run([a.cli, "colour", "list"], capture_output=True, text=True, check=True).stdout.split()
        for name in names:
            proc, clamp = ocio_processor(ocio, config, name)
            if proc is None:
                results[name] = {"ocio": "no OCIO 2.6.0 reference (see the bounds file)"}
                continue
            mine_path = Path(tmp) / "mine.f32"
            run_cli(a.cli, "apply", name, str(grid_path), str(mine_path))
            mine = np.fromfile(mine_path, dtype="<f4").reshape(-1, 3).astype(np.float64)
            theirs = grid.copy().reshape(-1)
            proc.applyRGB(theirs)
            theirs = theirs.reshape(-1, 3).astype(np.float64)
            if clamp:
                theirs = np.clip(theirs, 0.0, 1.0)
            output = name.split("/")[1]
            if output == "rec2100-pq":
                d = delta_e_itp_pq(mine, theirs)
                bound = bounds["cpp_vs_ocio_hdr"]["aces2"]
                metric = "delta_e_itp"
            else:
                d = ciede2000_display(mine, theirs, output)
                bound = bounds["cpp_vs_ocio_sdr"]["clip" if name.startswith("clip") else "aces2"]
                metric = "ciede2000"
            s = stats(d)
            worst = int(np.argmax(d))
            passed = s["max"] <= bound["max"] and ("p99" not in bound or s["p99"] <= bound["p99"])
            ok &= passed
            results[name] = {"metric": metric, **s, "bound": bound, "pass": passed,
                             "max_code_diff_8bit": int(np.abs(np.round(mine * 255) - np.round(theirs * 255)).max()),
                             "worst_input": [float(x) for x in grid[worst]],
                             "worst_mine": [round(float(x), 6) for x in mine[worst]],
                             "worst_ocio": [round(float(x), 6) for x in theirs[worst]]}
    commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    res = {"schema": "raw-native.evidence/1", "criterion": "M1 exit criterion 2: C++ reference against OCIO 2.6.0",
           "ocio": ocio.__version__, "config": CONFIG, "commit": commit, "inputs": len(grid),
           "grid_sha256": hashlib.sha256(grid_bytes).hexdigest(), "pass": ok, "pipelines": results,
           "does_not_prove": "Agreement with OCIO on this grid only; nothing about negative input, other configs, or a display."}
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
