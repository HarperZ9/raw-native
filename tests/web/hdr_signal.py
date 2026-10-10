#!/usr/bin/env python3
"""M1 exit criterion 3 (amended 2026-10-10): the HDR signal, checked in software.

    python tests/web/hdr_signal.py --cli build/Release/raw_native_cli.exe [--adapter gpu|swiftshader] [--cpu-only] [--out evidence.json]

This checks the signal the engine writes for an HDR display, not the light leaving a
panel. Bounds were written before the first run, in evidence/m1-hdr-signal-bounds.json.

CPU half (a): the C++ PQ (SMPTE ST 2084) encode and decode, the extended sRGB curve,
and the aces2-hdr1000 pipelines on the neutral axis, against the float64 reference of
the published formulas in tools/colour/hdr_signal_ref.py.
GPU half (b): the WGSL HDR pipelines over the committed grid, read back as float32,
against the C++ output (web/test/hdr-signal.html).
Controls: a PQ exponent with the factor of 4 dropped, on the CPU and on the GPU, must
exceed the bounds. A control that passes fails the run.
"""

from __future__ import annotations

import argparse
import hashlib
import http.server
import json
import math
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "colour"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import hdr_signal_ref as ref  # noqa: E402
from colour_grid import handler, reference, tables_match  # noqa: E402

WRONG_M1 = 2610 / 4096
GREYS = [2.0 ** (-12 + 20 * k / 255) for k in range(256)]


def cli_encode(cli: str, enc: str, xs: list[float], tmp: Path) -> list[float]:
    src, dst = tmp / f"{enc}.in.f64", tmp / f"{enc}.out.f64"
    src.write_bytes(struct.pack(f"<{len(xs)}d", *xs))
    subprocess.run([cli, "colour", "encode", enc, str(src), str(dst)], check=True)
    return list(struct.unpack(f"<{len(xs)}d", dst.read_bytes()))


def cli_apply(cli: str, pipeline: str, greys: list[float], tmp: Path) -> list[float]:
    src, dst = tmp / "grey.in.f32", tmp / "grey.out.f32"
    src.write_bytes(struct.pack(f"<{3 * len(greys)}f", *[g for g in greys for _ in range(3)]))
    subprocess.run([cli, "colour", "apply", pipeline, str(src), str(dst)], check=True)
    v = struct.unpack(f"<{3 * len(greys)}f", dst.read_bytes())
    return [v[3 * i: 3 * i + 3] for i in range(len(greys))]


def worst(name, inputs, got, want, bound):
    """Largest ratio of difference to bound; over 1 fails."""
    rows = [(abs(g - w) / bound(w), x, g, w) for x, g, w in zip(inputs, got, want)]
    r, x, g, w = max(rows, key=lambda t: t[0])
    return {"check": name, "n": len(rows), "maxRatio": r, "over": sum(1 for t in rows if t[0] > 1),
            "worstInput": x, "got": g, "reference": w}


def cpu_checks(cli: str, tmp: Path) -> dict:
    nits = [0.0] + [10 ** (-4 + 8 * k / 4000) for k in range(4001)]
    lin = [n / 100 for n in nits]
    signals = [k / 4096 for k in range(4097)]
    srgb_in = [-4 + 20 * k / 4000 for k in range(4001)]
    for t in (0.0031308, 0.0031308 - 1e-9, 0.0031308 + 1e-9):
        srgb_in += [t, -t]
    pq_cpp = cli_encode(cli, "pq", lin, tmp)
    checks = [
        worst("a1_pq_encode", nits, pq_cpp, [ref.pq_encode(x) for x in lin], lambda w: 1e-12),
        worst("a2_pq_decode", signals, cli_encode(cli, "pq-decode", signals, tmp), [ref.pq_decode(s) for s in signals],
              lambda w: max(1e-12, 1e-9 * abs(w))),
        worst("a3_srgb_extended", srgb_in, cli_encode(cli, "srgb-extended", srgb_in, tmp),
              [ref.srgb_extended(x) for x in srgb_in], lambda w: 1e-12),
    ]
    disp = [ref.grey_display_linear(g) for g in GREYS]
    tone = {}
    for pipe, enc, bound, key in (("aces2-hdr1000/rec2100-pq", ref.pq_encode, lambda w: 1e-4, "a4_tone_pq"),
                                  ("aces2-hdr1000/srgb-extended", ref.srgb_extended, lambda w: 1e-4 * max(1.0, abs(w)),
                                   "a5_tone_srgb_extended")):
        rgb = cli_apply(cli, pipe, GREYS, tmp)
        tone[pipe] = rgb
        chans = [(g, c) for g, px in zip(GREYS, rgb) for c in px]
        want = [enc(d) for d in disp for _ in range(3)]
        checks.append(worst(key, [g for g, _ in chans], [c for _, c in chans], want, bound))
    # The CPU control: a wrong PQ exponent must exceed both PQ bounds.
    ctl_encode = worst("control_pq_encode", nits, pq_cpp, [ref.pq_encode(x, WRONG_M1) for x in lin], lambda w: 1e-12)
    pq_px = [c for px in tone["aces2-hdr1000/rec2100-pq"] for c in px]
    ctl_tone = worst("control_tone_pq", [g for g in GREYS for _ in range(3)], pq_px,
                     [ref.pq_encode(d, WRONG_M1) for d in disp for _ in range(3)], lambda w: 1e-4)
    mid = ref.aces2_tonescale(18.0)
    return {"checks": checks, "controls": [ctl_encode, ctl_tone],
            "spot": {"pq_100_nits": ref.pq_encode(1.0), "pq_1000_nits": ref.pq_encode(10.0),
                     "aces2_hdr1000_grey_0_18_nits": mid}}


def gpu_check(cli: str, adapter: str, tmp: Path) -> dict:
    from playwright.sync_api import sync_playwright
    names = reference(cli, tmp)
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
            page.goto(f"http://localhost:{srv.server_address[1]}/web/test/hdr-signal.html")
            page.wait_for_function("window.__result !== undefined", timeout=300000)
            res = page.evaluate("window.__result")
            b.close()
    finally:
        srv.shutdown()
    res.update({"grid_sha256": grid_sha, "table_problems": table_problems})
    return res


def problems_of(res: dict) -> list[str]:
    out = []
    for c in res["cpu"]["checks"]:
        if c["over"]:
            out.append(f"{c['check']}: {c['over']} of {c['n']} over the bound (worst ratio {c['maxRatio']:.3g} at {c['worstInput']})")
    for c in res["cpu"]["controls"]:
        if c["maxRatio"] <= 1:
            out.append(f"{c['check']}: the control passed its bound, so the check cannot see a wrong PQ exponent")
    g = res.get("gpu")
    if g is None:
        return out
    if "error" in g:
        return out + [f"gpu: {g['error']}"]
    out += list(g["table_problems"])
    for name, r in g["pipelines"].items():
        if r["nan"] or r["over"]:
            out.append(f"{name}: {r['over']} over the bound, {r['nan']} non-finite (worst ratio {r['maxRatio']:.3g} at {r['worstInput']})")
    ctl = g["control"]
    if not ctl["replaced"] or ctl["maxRatio"] <= 1 or not math.isfinite(ctl["maxRatio"]):
        out.append("gpu control: the wrong PQ exponent did not exceed the bound")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cli", required=True)
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--cpu-only", action="store_true")
    ap.add_argument("--out")
    a = ap.parse_args()
    cli = str(Path(a.cli).resolve())
    with tempfile.TemporaryDirectory() as t:
        res = {"criterion": "M1 exit criterion 3, HDR signal (amended 2026-10-10)", "cpu": cpu_checks(cli, Path(t))}
        if not a.cpu_only:
            res["gpu"] = gpu_check(cli, a.adapter, Path(t))
            res["adapter_kind"] = a.adapter
    res["commit"] = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    res["bounds"] = "evidence/m1-hdr-signal-bounds.json"
    res["problems"] = problems_of(res)
    res["does_not_prove"] = ("What any display shows or emits. This checks the encoded signal against the published "
                             "formulas and the GPU against the CPU, on this adapter.")
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 1 if res["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
