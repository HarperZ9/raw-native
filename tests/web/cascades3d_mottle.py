#!/usr/bin/env python3
"""Mottle of a 3D-cascades frame against the path trace, and the width of a planted irradiance edge.

    python tests/web/cascades3d_mottle.py RUN_DIR --pt PT_DIR [--pt2 PT_DIR2] [--frame frame] [--json OUT.json]
    python tests/web/cascades3d_mottle.py RUN_DIR --edge X0 [--json OUT.json]

RUN_DIR holds frame.f32, pos.f32 and nrm.f32 from tests/web/cascades3d.py measure --pos 1 --save.
Mottle (evidence/cascades3d-denoise-bounds.json): e = (frame - reference) / mean(reference) in
luminance; on wall and column pixels (|n_y| < 0.5) and on floor pixels (n_y > 0.9), each at least
3 px from a normal or coverage edge; band = masked Gaussian blur of e at sigma 4 px minus at 32 px;
mottle = RMS of the band over the mask, low-pass = RMS of the sigma-4 blur alone. probe_band (added
after the first runs, exploratory): the same with sigma 1 px minus 8 px, the scale of the stride-8 probes. 95% interval from
1000 bootstrap draws of 64 x 64 tiles. --pt2 also scores the second reference seed against the first,
and a planted 5% sinusoid (period 64 px) added to the reference on the wall mask.
--edge X0: the frame is the irradiance of a run with --planted {"x": X0, "a": A, "b": B}; reports the
10% to 90% width in pixels of the step across x = X0 over floor pixels within 64 px of it.
Needs numpy and scipy (measurement only, not CI).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cascades3d_metrics import LUM, read_pfm, load  # noqa: E402

S1, S2 = 4.0, 32.0


def masks(cover: np.ndarray, nrm: np.ndarray) -> dict:
    n = nrm[..., :3]
    edge = np.zeros_like(cover)
    for ax in (0, 1):
        d = (n * np.roll(n, 1, axis=ax)).sum(-1) < 0.95
        edge |= d | np.roll(d, -1, axis=ax)
    keep = ndimage.binary_erosion(cover & ~edge, iterations=3)
    return {"walls": keep & (np.abs(n[..., 1]) < 0.5), "floor": keep & (n[..., 1] > 0.9)}


def blur(e: np.ndarray, m: np.ndarray, s: float) -> np.ndarray:
    w = ndimage.gaussian_filter(m.astype(np.float64), s, mode="constant")
    return ndimage.gaussian_filter(np.where(m, e, 0.0), s, mode="constant") / np.maximum(w, 1e-9)


def mottle(ya: np.ndarray, yr: np.ndarray, cover: np.ndarray, m: np.ndarray, reps: int = 1000, tile: int = 64) -> dict:
    e = (ya - yr) / yr[cover].mean()
    lo = blur(e, m, S1)
    band = lo - blur(e, m, S2)
    h, w = m.shape
    tid = ((np.arange(h) // tile)[:, None] * ((w + tile - 1) // tile) + (np.arange(w) // tile)[None, :])[m]
    nt = int(tid.max()) + 1
    sb, sl, cnt = (np.bincount(tid, weights=x, minlength=nt) for x in (band[m] ** 2, lo[m] ** 2, np.ones(tid.size)))
    keep = cnt > 0
    sb, cnt = sb[keep], cnt[keep]
    pick = np.random.default_rng(11).integers(0, sb.size, size=(reps, sb.size))
    vals = np.sqrt(sb[pick].sum(1) / cnt[pick].sum(1))
    fine = blur(e, m, 1.0) - blur(e, m, 8.0)
    return {"mottle": float(np.sqrt((band[m] ** 2).mean())), "interval95": [float(np.quantile(vals, 0.025)), float(np.quantile(vals, 0.975))],
            "lowpass": float(np.sqrt((lo[m] ** 2).mean())), "probe_band": float(np.sqrt((fine[m] ** 2).mean())), "pixels": int(m.sum())}


def edge_width(irr: np.ndarray, pos: np.ndarray, floor: np.ndarray, x0: float) -> dict:
    below = floor & (pos[..., 0] < x0)
    above = floor & (pos[..., 0] >= x0)
    d = np.where(below, -ndimage.distance_transform_edt(~above), ndimage.distance_transform_edt(~below))
    sel = floor & (np.abs(d) <= 64)
    v = irr[..., 0][sel]
    a, b = np.median(v[d[sel] < -48]), np.median(v[d[sel] > 48])
    bins = np.arange(-64, 65)
    idx = np.clip(np.round(d[sel]).astype(int) + 64, 0, 128)
    cnt = np.bincount(idx, minlength=129)
    prof = np.bincount(idx, weights=(v - a) / (b - a), minlength=129) / np.maximum(cnt, 1)
    ok = cnt > 0  # rows of floor seams hold no pixels; the crossing interpolates across them
    xb, pb = bins[ok], prof[ok]
    def cross(t: float) -> float:
        k = int(np.argmax(pb >= t))
        return float(xb[k - 1] + (t - pb[k - 1]) / (pb[k] - pb[k - 1]) * (xb[k] - xb[k - 1])) if k > 0 else float(xb[0])
    return {"width_10_90_px": cross(0.9) - cross(0.1), "x10": cross(0.1), "x90": cross(0.9), "far_below": float(a), "far_above": float(b),
            "pixels": int(sel.sum()), "profile": [round(float(x), 4) if c else None for x, c in zip(prof, ok)]}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run")
    ap.add_argument("--pt")
    ap.add_argument("--pt2")
    ap.add_argument("--frame", default="frame")
    ap.add_argument("--edge", type=float)
    ap.add_argument("--json")
    a = ap.parse_args()
    run = Path(a.run)
    nbytes = (run / f"{a.frame}.f32").stat().st_size // 16
    if a.pt:
        ref = read_pfm(Path(a.pt) / "radiance.pfm")
        h, w = ref.shape[:2]
    else:
        w = int(json.loads((run / "run.json").read_text())["size"][0])
        h = nbytes // w
    frame, pos, nrm = (load(run, n, w, h) for n in (a.frame, "pos", "nrm"))
    cover = frame[..., 3] > 0
    M = masks(cover, nrm)
    res: dict = {"size": [w, h], "mask_pixels": {k: int(v.sum()) for k, v in M.items()}}
    if a.edge is not None:
        res["edge"] = edge_width(frame, pos, M["floor"], a.edge)
    if a.pt:
        tri = np.fromfile(Path(a.pt) / "triangle.i32", dtype="<i4").reshape(h, w)
        cover &= tri >= 0
        yr, ya = ref[..., :3] @ LUM, frame[..., :3] @ LUM
        res["frame"] = {k: mottle(ya, yr, cover, m) for k, m in M.items()}
        if a.pt2:
            y2 = read_pfm(Path(a.pt2) / "radiance.pfm")[..., :3] @ LUM
            res["reference_seed2"] = {k: mottle(y2, yr, cover, m, reps=200) for k, m in M.items()}
            xs = np.arange(w)[None, :].repeat(h, 0)
            planted = yr + np.where(M["walls"], 0.05 * yr[cover].mean() * np.sin(2 * np.pi * xs / 64.0), 0.0)
            res["planted_sinusoid_5pct"] = mottle(planted, yr, cover, M["walls"], reps=200)
            res["planted_sinusoid_5pct"]["expected_rms"] = 0.05 / np.sqrt(2)
    text = json.dumps(res, indent=2)
    print(text if len(text) < 6000 else text[:6000])
    if a.json:
        Path(a.json).write_text(text + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
