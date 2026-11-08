#!/usr/bin/env python3
"""The bright-band region of a 3D-cascades frame on raw-hall, and the signed error of runs inside it.

    python tests/web/cascades3d_band.py --select RUN_DIR --pt PT_DIR [--pt2 PT_DIR2] RUN_DIR... [--json OUT]

The region (evidence/cascades3d-furnace-bounds.json, "band") is every pixel where the selection run's
signed luminance error, box-blurred over 13 x 13 px and divided by the reference's frame mean, lies
above +0.15: the column flanks, the far-wall patch and the arch soffits of round 6's default. The region
is chosen on one run and scored on others. For each run: the mean signed error inside the region over
the frame's reference mean, and the same inside the region's complement. Needs numpy (measurement
only, not CI).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cascades3d_metrics import LUM, load, read_pfm  # noqa: E402

BLUR, THRESHOLD = 6, 0.15


def box(x: np.ndarray, k: int) -> np.ndarray:
    """Mean over a (2k+1) square window, clipped at the frame's edges."""
    c = np.cumsum(np.cumsum(np.pad(x, ((1, 0), (1, 0))), 0), 1)
    h, w = x.shape
    ys, ye = np.clip(np.arange(h) - k, 0, h), np.clip(np.arange(h) + k + 1, 0, h)
    xs, xe = np.clip(np.arange(w) - k, 0, w), np.clip(np.arange(w) + k + 1, 0, w)
    s = c[ye][:, xe] - c[ys][:, xe] - c[ye][:, xs] + c[ys][:, xs]
    return s / ((ye - ys)[:, None] * (xe - xs)[None, :])


def reference(pt: Path, pt2: Path | None) -> np.ndarray:
    r = read_pfm(sorted(pt.glob("radiance.pfm"))[0])[..., :3]
    if pt2:
        r = 0.5 * (r + read_pfm(sorted(pt2.glob("radiance.pfm"))[0])[..., :3])
    return r @ LUM


def signed(run: Path, yr: np.ndarray, mask: np.ndarray, mr: float) -> np.ndarray:
    h, w = yr.shape
    ya = load(run, "frame", w, h)[..., :3] @ LUM
    return np.where(mask, (ya - yr) / mr, 0.0)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+", type=Path)
    ap.add_argument("--select", type=Path, required=True)
    ap.add_argument("--pt", type=Path, required=True)
    ap.add_argument("--pt2", type=Path)
    ap.add_argument("--json", type=Path)
    a = ap.parse_args()
    yr = reference(a.pt, a.pt2)
    mask = yr > 0
    mr = float(yr[mask].mean())
    band = (box(signed(a.select, yr, mask, mr), BLUR) > THRESHOLD) & mask
    rest = mask & ~band
    out = {"select": str(a.select), "pixels": int(band.sum()), "mask_sha256": hashlib.sha256(np.packbits(band).tobytes()).hexdigest(), "runs": {}}
    for run in a.runs:
        d = signed(run, yr, mask, mr)
        out["runs"][run.name] = {"band_mean_signed": float(d[band].mean()), "rest_mean_signed": float(d[rest].mean())}
        print(f"{run.name}: band {d[band].mean():+.4f}, rest {d[rest].mean():+.4f}")
    if a.json:
        a.json.write_text(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
