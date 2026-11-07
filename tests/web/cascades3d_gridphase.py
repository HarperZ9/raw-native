#!/usr/bin/env python3
"""How much of a 3D-cascades frame's wall mottle is locked to the world grid's phase (exploratory).

    python tests/web/cascades3d_gridphase.py RUN_DIR --pt PT_DIR [--h 1.0]

RUN_DIR holds frame.f32, pos.f32, nrm.f32 and run.json from tests/web/cascades3d.py measure --pos 1
--save. The band-passed relative error of tests/web/cascades3d_mottle.py (walls, sigma 4 to 32 px) is
binned by each pixel's surface position in grid cells of size H (8 x 8 x 8 bins of the fractional
cell position, from the run's grid origin). Reported: the share of the band's energy that the bin
means carry, and the same with the bins shuffled (the control: what binning finds in no pattern).
Needs numpy and scipy (measurement only, not CI).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cascades3d_metrics import LUM, read_pfm, load  # noqa: E402
from cascades3d_mottle import masks, blur, S1, S2  # noqa: E402


def share(band: np.ndarray, key: np.ndarray, nbins: int) -> float:
    cnt = np.bincount(key, minlength=nbins)
    mean = np.bincount(key, weights=band, minlength=nbins) / np.maximum(cnt, 1)
    return float((cnt * mean ** 2).sum() / (band ** 2).sum())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run")
    ap.add_argument("--pt", required=True)
    ap.add_argument("--h", type=float, default=1.0)
    a = ap.parse_args()
    run, pt = Path(a.run), Path(a.pt)
    ref = read_pfm(pt / "radiance.pfm")
    h, w = ref.shape[:2]
    frame, pos, nrm = (load(run, n, w, h) for n in ("frame", "pos", "nrm"))
    cover = (frame[..., 3] > 0) & (np.fromfile(pt / "triangle.i32", dtype="<i4").reshape(h, w) >= 0)
    m = masks(cover, nrm)["walls"]
    yr, ya = ref[..., :3] @ LUM, frame[..., :3] @ LUM
    e = (ya - yr) / yr[cover].mean()
    band = (blur(e, m, S1) - blur(e, m, S2))[m]
    lo = np.array(json.loads((run / "run.json").read_text())["plan"]["lo"])
    g = (pos[..., :3][m] - lo) / a.h
    nb = 8
    idx = np.clip(((g - np.floor(g)) * nb).astype(int), 0, nb - 1)
    key = (idx[:, 0] * nb + idx[:, 1]) * nb + idx[:, 2]
    res = {"run": run.as_posix(), "h": a.h, "band_rms": float(np.sqrt((band ** 2).mean())), "share_grid_phase": share(band, key, nb ** 3),
           "share_shuffled_control": share(band, np.random.default_rng(3).permutation(key), nb ** 3)}
    print(json.dumps(res))
    return 0


if __name__ == "__main__":
    sys.exit(main())
