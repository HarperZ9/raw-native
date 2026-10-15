#!/usr/bin/env python3
"""HW H2 diagnosis: where a model loses to SVGF, from per-pixel FLIP maps (coordinator direction 2026-10-15).

For one sequence: the model's output (rr_eval.run_net), SVGF's (cached by rr_eval in WORK/svgf, or computed), the
reference, and the FLIP map of each against the reference on the tone-mapped frames. The FLIP excess (model - SVGF)
is split over pixel classes, each pixel in exactly one class by this priority:
  miss        no first hit (sky)
  disoccluded history age below 8 frames (rr_eval.history_age, as the ghosting metric)
  firefly     input luminance above 20 times the median of its 5 x 5 neighbourhood
  edge        a different first-hit triangle or a depth step above 5% among the 4 neighbours
  glossy      roughness below 0.3
  other
Reported per class: pixel share, mean FLIP of each method, and the class's share of the total excess.

Usage: rr_diag_flipclass.py MODEL.pt SEQ_DIR [--svgf-cache DIR] --out OUT.json [--maps DIR]
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
import rr_eval  # noqa: E402
import rr_flip  # noqa: E402

CLASSES = ("miss", "disoccluded", "firefly", "edge", "glossy", "other")


def median5(img):
    h, w = img.shape
    p = np.pad(img, 2, mode="edge")
    stack = np.stack([p[dy:dy + h, dx:dx + w] for dy in range(5) for dx in range(5)])
    return np.median(stack, 0)


def classify(x, tri, age):
    dep = x[..., 9]
    lum = rr_data.lum(x[..., 0:3])
    cls = np.full(dep.shape, CLASSES.index("other"), np.int8)
    edge = np.zeros(dep.shape, bool)
    for dy, dx in ((0, 1), (1, 0), (0, -1), (-1, 0)):
        t2 = np.roll(tri, (dy, dx), (0, 1))
        d2 = np.roll(dep, (dy, dx), (0, 1))
        edge |= (t2 != tri) | (np.abs(d2 - dep) > 0.05 * np.maximum(dep, 1e-6))
    order = [("glossy", x[..., 15] < 0.3), ("edge", edge), ("firefly", lum > 20 * np.maximum(median5(lum), 1e-3)),
             ("disoccluded", age < 8), ("miss", dep <= 0)]
    for name, m in order:   # later entries win, so the list runs from the lowest priority up
        cls[m] = CLASSES.index(name)
    return cls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("seq")
    ap.add_argument("--svgf-cache", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--maps", default="")
    a = ap.parse_args()
    metas, x, r, tri = rr_data.load_sequence(a.seq)
    ref = r[..., 0:3]
    ours = rr_eval.run_net(a.model, x)
    n, h, w = ref.shape[:3]
    if a.svgf_cache and (Path(a.svgf_cache) / "f000.f32").exists():
        svgf = np.stack([np.fromfile(Path(a.svgf_cache) / f"f{t:03d}.f32", np.float32).reshape(h, w, 3) for t in range(n)])
    else:
        svgf = rr_eval.run_svgf(x)
    age = rr_eval.history_age(tri, x[..., 10:12])
    tr, to, ts = rr_eval.tonemap(ref), rr_eval.tonemap(ours), rr_eval.tonemap(svgf)
    stats = {c: {"pixels": 0, "flip_ours": 0.0, "flip_svgf": 0.0} for c in CLASSES}
    total_excess = 0.0
    for t in range(n):
        fo, fs = rr_flip.flip_map(tr[t], to[t]), rr_flip.flip_map(tr[t], ts[t])
        cls = classify(x[t], tri[t], age[t])
        for i, c in enumerate(CLASSES):
            m = cls == i
            stats[c]["pixels"] += int(m.sum())
            stats[c]["flip_ours"] += float(fo[m].sum())
            stats[c]["flip_svgf"] += float(fs[m].sum())
        total_excess += float((fo - fs).sum())
        if a.maps and t in (0, 16, 32, 48, 63):
            Path(a.maps).mkdir(parents=True, exist_ok=True)
            np.stack([fo, fs, cls.astype(np.float32)], -1).astype(np.float32).tofile(Path(a.maps) / f"f{t:03d}_flip_ours_svgf_class.f32")
    npx = n * h * w
    rows = {}
    for c in CLASSES:
        s = stats[c]
        if s["pixels"] == 0:
            continue
        rows[c] = {"pixel_share": s["pixels"] / npx, "flip_ours": s["flip_ours"] / s["pixels"], "flip_svgf": s["flip_svgf"] / s["pixels"],
                   "share_of_excess": (s["flip_ours"] - s["flip_svgf"]) / total_excess if total_excess else 0.0}
    res = {"model": a.model, "sequence": Path(a.seq).name, "frames": n, "mean_flip_ours": sum(s["flip_ours"] for s in stats.values()) / npx,
           "mean_flip_svgf": sum(s["flip_svgf"] for s in stats.values()) / npx, "classes": rows}
    Path(a.out).write_text(json.dumps(res, indent=1) + "\n")
    print(json.dumps(res, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
