#!/usr/bin/env python3
"""Error of a 3D-cascades frame against the path-traced reference (pt-error's measure), with intervals.

    python tests/web/cascades3d_metrics.py RUN_DIR --pt PT_DIR [--pt2 PT_DIR2] [--frame frame]
        [--room-b X] [--closed-form V] [--images OUT_PREFIX] [--json OUT.json]

RUN_DIR holds frame.f32 (and baseline.f32, pos.f32) from tests/web/cascades3d.py measure --save;
PT_DIR holds radiance.pfm and triangle.i32 from raw_native_cli gi-reference --pt. The measure is
pt-error's: relative RMSE and bias of luminance over pixels both cover, normalised by the reference
mean; a 95% interval from 1000 bootstrap draws of 32 x 32 tiles; the second seed gives the
reference's own noise. --room-b X: also the leak numbers over pixels whose surface has x > X.
--closed-form V: the reference is the constant V (the planted check), and a 1% offset is scored too.
Needs numpy, and Pillow for --images (measurement only, not CI).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

LUM = np.array([0.2126, 0.7152, 0.0722])


def read_pfm(path: Path) -> np.ndarray:
    with open(path, "rb") as f:
        kind = f.readline().strip()
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        ch = 3 if kind == b"PF" else 1
        data = np.fromfile(f, dtype="<f4" if scale < 0 else ">f4", count=w * h * ch).reshape(h, w, ch)
    return data[::-1].astype(np.float64)


def load(run: Path, name: str, w: int, h: int) -> np.ndarray:
    return np.fromfile(run / f"{name}.f32", dtype="<f4").reshape(h, w, 4).astype(np.float64)


def stats(a: np.ndarray, r: np.ndarray, mask: np.ndarray, reps: int = 1000, tile: int = 32) -> dict:
    ya, yr = a[..., :3] @ LUM, r[..., :3] @ LUM
    d = (ya - yr)[mask]
    mr = yr[mask].mean()
    out = {"relative_rmse": float(np.sqrt((d * d).mean()) / mr), "bias": float(d.mean() / mr), "mean_reference": float(mr),
           "mean_frame": float(ya[mask].mean()), "pixels": int(mask.sum())}
    h, w = mask.shape
    ty, tx = (np.arange(h) // tile)[:, None], (np.arange(w) // tile)[None, :]
    tid = (ty * ((w + tile - 1) // tile) + tx)[mask]
    nt = int(tid.max()) + 1 if tid.size else 1
    se, sr, cnt = (np.bincount(tid, weights=x, minlength=nt) for x in ((ya - yr)[mask] ** 2, yr[mask], np.ones(tid.size)))
    keep = cnt > 0
    se, sr, cnt = se[keep], sr[keep], cnt[keep]
    rng = np.random.default_rng(7)
    pick = rng.integers(0, se.size, size=(reps, se.size))
    vals = np.sqrt(se[pick].sum(1) / cnt[pick].sum(1)) / (sr[pick].sum(1) / cnt[pick].sum(1))
    out["interval95"] = [float(np.quantile(vals, 0.025)), float(np.quantile(vals, 0.975))]
    out["tiles"] = int(se.size)
    return out


def tonemap(img: np.ndarray, gain: float) -> np.ndarray:
    l = 1.0 - np.exp(-np.maximum(img[..., :3], 0) * gain)
    s = np.where(l <= 0.0031308, l * 12.92, 1.055 * np.power(l, 1 / 2.4) - 0.055)
    return (np.clip(s, 0, 1) * 255 + 0.5).astype(np.uint8)


def error_png(a: np.ndarray, r: np.ndarray, mask: np.ndarray, scale: float, top: float = 0.5) -> np.ndarray:
    e = np.abs(a[..., :3] @ LUM - r[..., :3] @ LUM) / scale / top
    ramp = np.array([[0, 0, 0], [40, 40, 160], [200, 60, 120], [250, 200, 60], [255, 255, 255]], dtype=np.float64)
    x = np.clip(e, 0, 1) * (len(ramp) - 1)
    k = np.minimum(len(ramp) - 2, np.floor(x).astype(int))
    f = (x - k)[..., None]
    out = ramp[k] + f * (ramp[k + 1] - ramp[k])
    out[~mask] = 0
    return out.astype(np.uint8)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run")
    ap.add_argument("--pt", required=True)
    ap.add_argument("--pt2")
    ap.add_argument("--frame", default="frame")
    ap.add_argument("--room-b", type=float)
    ap.add_argument("--closed-form", type=float)
    ap.add_argument("--images")
    ap.add_argument("--json")
    a = ap.parse_args()
    run, pt = Path(a.run), Path(a.pt)
    ref = read_pfm(pt / "radiance.pfm")
    h, w = ref.shape[:2]
    tri = np.fromfile(pt / "triangle.i32", dtype="<i4").reshape(h, w)
    res: dict = {"size": [w, h]}
    frames = {n: load(run, n, w, h) for n in (a.frame, "baseline") if (run / f"{n}.f32").exists()}
    cover = (tri >= 0) & (frames[a.frame][..., 3] > 0)
    res["coverage_agreement"] = float(((tri >= 0) == (frames[a.frame][..., 3] > 0)).mean())
    if a.closed_form is not None:
        cf = np.full_like(ref, a.closed_form)
        res["planted"] = {"reference_vs_closed_form": stats(ref, cf, cover, reps=200)["relative_rmse"],
                          "frame_vs_closed_form": stats(frames[a.frame], cf, cover, reps=200)["relative_rmse"],
                          "metric_on_closed_form_plus_1pct": stats(cf * 1.01, cf, cover, reps=50)["relative_rmse"]}
    for n, img in frames.items():
        res[n] = stats(img, ref, cover)
    if a.pt2:
        ref2 = read_pfm(Path(a.pt2) / "radiance.pfm")
        between = stats(ref2, ref, cover, reps=50)["relative_rmse"]
        res["reference_noise"] = {"between_seeds": between, "per_reference": between / np.sqrt(2)}
    if a.room_b is not None and (run / "pos.f32").exists():
        pos = load(run, "pos", w, h)
        inb = cover & (pos[..., 0] > a.room_b)
        ina = cover & (pos[..., 0] <= a.room_b)
        yr = ref[..., :3] @ LUM
        res["room_b"] = {"pixels": int(inb.sum()), "reference_mean": float(yr[inb].mean()) if inb.any() else None,
                         "room_a_reference_mean": float(yr[ina].mean()) if ina.any() else None}
        for n, img in frames.items():
            y = img[..., :3] @ LUM
            res["room_b"][n] = {"mean": float(y[inb].mean()) if inb.any() else None,
                                "ratio_to_reference": float(y[inb].mean() / yr[inb].mean()) if inb.any() and yr[inb].mean() > 0 else None,
                                "relative_rmse": stats(img, ref, inb, reps=200)["relative_rmse"] if inb.any() and yr[inb].mean() > 0 else None}
    if a.images:
        from PIL import Image
        gain = 1.0 / max(1e-9, 2.0 * float((ref[..., :3] @ LUM)[cover].mean()))
        pre = Path(a.images)
        pre.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(tonemap(ref, gain)).save(f"{pre}-reference.png")
        scale = float((ref[..., :3] @ LUM)[cover].mean())
        for n, img in frames.items():
            Image.fromarray(tonemap(img, gain)).save(f"{pre}-{n}.png")
            Image.fromarray(error_png(img, ref, cover, scale)).save(f"{pre}-{n}-error.png")
    text = json.dumps(res, indent=2)
    print(text)
    if a.json:
        Path(a.json).write_text(text + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
