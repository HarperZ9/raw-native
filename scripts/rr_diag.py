#!/usr/bin/env python3
"""HW H2 per-pixel diagnosis of one test sequence (no metric of record; it informs the next amendment).

For every pixel of every frame: FLIP (our implementation, checked) of ours, SVGF and SVGF plus TAA against the reference
on the tone-mapped frame, and the pixel's classes: history age below 8 (recent disocclusion), a geometric edge (the
first-hit triangle differs from a 4-neighbour's, or the depth steps by more than 5%), glossy (roughness below 0.35),
metallic (above 0.5), a miss, and the reference luminance tercile. Reports, per class, the pixel share, the mean FLIP of
each method, the share of the total FLIP excess of ours over SVGF, and the mean luminance bias of ours and SVGF
(output over reference, tone-mapped). Also the per-frame mean FLIP of each method.

Usage: rr_diag.py SEQ_DIR --model best.pt --svgf-work DIR [--out diag.json] [--maps DIR]
"""
import argparse
import os

os.environ.setdefault("FOR_DISABLE_CONSOLE_CTRL_HANDLER", "1")
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_data  # noqa: E402
import rr_flip  # noqa: E402
from rr_eval import history_age, run_net, tonemap  # noqa: E402


def classes(x, tri, age):
    n, h, w, _ = x.shape
    dep, rough, metal = x[..., 9], x[..., 15], x[..., 16]
    edge = np.zeros((n, h, w), bool)
    for t in range(n):
        tr, d = tri[t], dep[t]
        e = np.zeros((h, w), bool)
        for sl_a, sl_b in (((slice(None), slice(1, None)), (slice(None), slice(None, -1))),
                           ((slice(1, None), slice(None)), (slice(None, -1), slice(None)))):
            diff = tr[sl_a] != tr[sl_b]
            step = np.abs(d[sl_a] - d[sl_b]) > 0.05 * np.maximum(np.minimum(d[sl_a], d[sl_b]), 1e-6)
            m = diff | step
            e[sl_a] |= m
            e[sl_b] |= m
        edge[t] = e
    miss = dep <= 0
    return {"recent_disocclusion": age < 8, "edge": edge & ~miss, "glossy": (rough < 0.35) & ~miss,
            "metallic": (metal > 0.5) & ~miss, "miss": miss}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seq")
    ap.add_argument("--model", required=True)
    ap.add_argument("--svgf-work", required=True)
    ap.add_argument("--out", default="")
    ap.add_argument("--maps", default="")
    a = ap.parse_args()
    seq = Path(a.seq)
    metas, x, r, tri = rr_data.load_sequence(seq)
    n, h, w, _ = x.shape
    ref = r[..., 0:3]
    ours = run_net(a.model, x)
    sv = np.stack([np.fromfile(Path(a.svgf_work) / "svgf" / f"f{t:03d}.f32", np.float32).reshape(h, w, 3) for t in range(n)])
    st = np.stack([np.fromfile(Path(a.svgf_work) / "taa" / f"f{t:03d}.f32", np.float32).reshape(h, w, 3) for t in range(n)])
    tm_r = tonemap(ref)
    outs = {"ours": ours, "svgf": sv, "svgf_taa": st}
    tm = {k: tonemap(v) for k, v in outs.items()}
    flip = {k: np.stack([rr_flip.flip_map(tm_r[t], tm[k][t]) for t in range(n)]) for k in outs}
    age = history_age(tri, x[..., 10:12])
    cls = classes(x, tri, age)
    lum_r = rr_data.lum(tm_r)
    terc = np.quantile(lum_r, [1 / 3, 2 / 3])
    cls["dark_tercile"] = lum_r <= terc[0]
    cls["bright_tercile"] = lum_r > terc[1]
    cls["all"] = np.ones(lum_r.shape, bool)
    excess = flip["ours"] - flip["svgf"]
    total_excess = float(excess.sum())
    rows = {}
    for name, m in cls.items():
        if not m.any():
            continue
        row = {"pixel_share": float(m.mean()),
               "mean_flip": {k: float(v[m].mean()) for k, v in flip.items()},
               "share_of_total_excess_over_svgf": float(excess[m].sum() / total_excess) if total_excess else None}
        for k in ("ours", "svgf"):
            lo, lr = rr_data.lum(tm[k])[m], lum_r[m]
            row[f"tonemapped_luminance_bias_{k}"] = float((lo - lr).mean())
        rows[name] = row
    per_frame = {k: [float(v[t].mean()) for t in range(n)] for k, v in flip.items()}
    res = {"sequence": seq.name, "frames": n, "model": a.model, "total_flip_excess_ours_minus_svgf_mean": float(excess.mean()),
           "classes": rows, "per_frame_mean_flip": per_frame}
    s = json.dumps(res, indent=1)
    print(s)
    if a.out:
        Path(a.out).write_text(s + "\n")
    if a.maps:
        md = Path(a.maps)
        md.mkdir(parents=True, exist_ok=True)
        for k, v in flip.items():
            v.astype(np.float32).tofile(md / f"flip_{k}.f32")
        excess.astype(np.float32).tofile(md / "flip_excess_ours_minus_svgf.f32")
    return 0


if __name__ == "__main__":
    sys.exit(main())
