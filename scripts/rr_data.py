#!/usr/bin/env python3
"""HW H2 dataset access (numpy only): frames written by `raw_native_cli rr-render`.

Layout: ROOT/{train,val,test}/{scene}_p{path}/fKKK_in.f32 (H x W x 17), fKKK_ref.f32 (H x W x 7),
fKKK_tri.i32 (H x W) and fKKK.json. Channels of _in: radiance 0:3 (1 spp), albedo 3:6, normal
6:9, depth 9, motion 10:12 (previous - current, pixels), previous position 12:14, expected
previous depth 14, roughness 15, metallic 16. _ref: radiance 0:3, variance of the mean of
luminance 3, albedo 4:7. fKKK_split.f32 (H x W x 15, rr-render with path tracer interface v2), when present,
is appended to _in as channels 17 to 31: diffuse 17:20, specular 20:23, emission 23:26, diffuse albedo 26:29,
specular albedo 29:32 (evidence/rr-bounds-amendment-6.json).
"""
import json
from pathlib import Path

import numpy as np

EPS = 1e-3


def sequences(root, split):
    return sorted(p for p in (Path(root) / split).iterdir() if p.is_dir())


def frame_count(d):
    n = 0
    while (Path(d) / f"f{n:03d}.json").exists():
        n += 1
    return n


def load_frame(d, k):
    d = Path(d)
    meta = json.loads((d / f"f{k:03d}.json").read_text())
    h, w = meta["height"], meta["width"]
    x = np.fromfile(d / f"f{k:03d}_in.f32", np.float32).reshape(h, w, 17)
    sp = d / f"f{k:03d}_split.f32"
    if sp.exists():
        x = np.concatenate([x, np.fromfile(sp, np.float32).reshape(h, w, 15)], -1)
    r = np.fromfile(d / f"f{k:03d}_ref.f32", np.float32).reshape(h, w, 7)
    t = np.fromfile(d / f"f{k:03d}_tri.i32", np.int32).reshape(h, w)
    return meta, x, r, t


def load_sequence(d):
    n = frame_count(d)
    metas, xs, rs, ts = [], [], [], []
    for k in range(n):
        m, x, r, t = load_frame(d, k)
        metas.append(m), xs.append(x), rs.append(r), ts.append(t)
    return metas, np.stack(xs), np.stack(rs), np.stack(ts)


def lum(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def warp_nearest(prev, motion):
    """prev sampled at round(pixel + motion), clamped; returns the warped array and an inside mask."""
    h, w = motion.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    py = np.rint(yy + motion[..., 1]).astype(int)
    px = np.rint(xx + motion[..., 0]).astype(int)
    inside = (py >= 0) & (py < h) & (px >= 0) & (px < w)
    return prev[np.clip(py, 0, h - 1), np.clip(px, 0, w - 1)], inside


def disocclusion(tri, tri_prev, motion):
    """True where the pixel's first-hit triangle differs from the triangle at its reprojected
    position in the previous frame, or the reprojection leaves the frame (amendment 1)."""
    warped, inside = warp_nearest(tri_prev, motion)
    return (~inside) | (warped != tri)
