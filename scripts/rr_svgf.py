#!/usr/bin/env python3
"""SVGF, spatiotemporal variance-guided filtering (Schied et al., HPG 2017), our own numpy
implementation from the paper, as the H2 baseline (evidence/rr-bounds.json, "baselines").

Per frame: reproject the history with motion vectors and accept it where depth and normal
agree; accumulate demodulated radiance and its first two luminance moments with an
exponential moving average (alpha 0.2, the paper's value) after a short cumulative ramp;
estimate variance from the moments (or spatially over 7 x 7 for the first 4 frames of a
pixel's history); then 5 a-trous iterations of a 5 x 5 B3-spline kernel with the paper's
edge-stopping functions (sigma_z 1, sigma_n 128, sigma_l 4). The first iteration's output
feeds the next frame's colour history, as in the paper. The caller remodulates by albedo.

numpy only. Usage: rr_svgf.py --selftest
"""
import sys

import numpy as np

ALPHA, ALPHA_M, SIGMA_Z, SIGMA_N, SIGMA_L, ITER, EPS = 0.2, 0.2, 1.0, 128.0, 4.0, 5, 1e-10
B3 = np.array([1 / 16, 1 / 4, 3 / 8, 1 / 4, 1 / 16])


def lum(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def shifted(a, dy, dx):
    """a shifted so out[y, x] = a[y + dy, x + dx], clamped at the border."""
    h, w = a.shape[:2]
    ys = np.clip(np.arange(h) + dy, 0, h - 1)
    xs = np.clip(np.arange(w) + dx, 0, w - 1)
    return a[ys][:, xs]


class Svgf:
    def __init__(self, h, w):
        self.h, self.w = h, w
        self.color = None

    def reproject(self, motion, depth, normal):
        """Previous-frame sample positions and a validity mask (nearest pixel; depth within 10%
        relative, normals within 0.9 cosine)."""
        yy, xx = np.mgrid[0:self.h, 0:self.w]
        py = np.rint(yy + motion[..., 1]).astype(int)
        px = np.rint(xx + motion[..., 0]).astype(int)
        inside = (py >= 0) & (py < self.h) & (px >= 0) & (px < self.w)
        py, px = np.clip(py, 0, self.h - 1), np.clip(px, 0, self.w - 1)
        if self.color is None:
            return py, px, np.zeros_like(inside)
        dz = np.abs(self.depth[py, px] - depth) <= 0.1 * np.maximum(depth, EPS)
        dn = np.sum(self.normal[py, px] * normal, -1) >= 0.9
        return py, px, inside & dz & dn

    def temporal(self, radiance, motion, depth, normal):
        py, px, ok = self.reproject(motion, depth, normal)
        l = lum(radiance)
        m = np.stack([l, l * l], -1)
        if self.color is None:
            hist_len = np.ones((self.h, self.w))
            col, mom = radiance, m
        else:
            hist_len = np.where(ok, np.minimum(self.hist[py, px] + 1, 255), 1)
            a = np.maximum(1 / hist_len, ALPHA)[..., None]
            am = np.maximum(1 / hist_len, ALPHA_M)[..., None]
            col = np.where(ok[..., None], (1 - a) * self.color[py, px] + a * radiance, radiance)
            mom = np.where(ok[..., None], (1 - am) * self.moments[py, px] + am * m, m)
        var = np.maximum(mom[..., 1] - mom[..., 0] ** 2, 0)
        # Short histories: spatial variance over 7 x 7, as the paper does for fewer than 4 frames.
        short = hist_len < 4
        if short.any():
            s1 = sum(shifted(l, dy, dx) for dy in range(-3, 4) for dx in range(-3, 4)) / 49
            s2 = sum(shifted(l * l, dy, dx) for dy in range(-3, 4) for dx in range(-3, 4)) / 49
            var = np.where(short, np.maximum(s2 - s1 * s1, 0), var)
        return col, mom, var, hist_len

    def atrous(self, col, var, depth, normal):
        dzdx = np.abs(shifted(depth, 0, 1) - shifted(depth, 0, -1)) * 0.5
        dzdy = np.abs(shifted(depth, 1, 0) - shifted(depth, -1, 0)) * 0.5
        first = None
        for it in range(ITER):
            step = 1 << it
            g = self.gauss3(var)   # the paper's 3 x 3 Gaussian prefilter of the variance
            lp = lum(col)
            num_c, num_v, den = np.zeros_like(col), np.zeros_like(var), np.zeros_like(var)
            for j in range(-2, 3):
                for i in range(-2, 3):
                    k = B3[j + 2] * B3[i + 2]
                    cq, vq = shifted(col, j * step, i * step), shifted(var, j * step, i * step)
                    zq, nq = shifted(depth, j * step, i * step), shifted(normal, j * step, i * step)
                    wz = np.abs(depth - zq) / (SIGMA_Z * (np.abs(dzdx * i * step) + np.abs(dzdy * j * step)) + EPS)
                    wn = np.maximum(np.sum(normal * nq, -1), 0) ** SIGMA_N
                    wl = np.abs(lp - lum(cq)) / (SIGMA_L * np.sqrt(np.maximum(g, 0)) + EPS)
                    w = k * np.exp(-wz - wl) * wn
                    num_c += w[..., None] * cq
                    num_v += w * w * vq
                    den += w
            col = num_c / np.maximum(den, EPS)[..., None]
            var = num_v / np.maximum(den * den, EPS)
            if it == 0:
                first = col
        return col, first

    @staticmethod
    def gauss3(v):
        k = np.array([0.25, 0.5, 0.25])
        return sum(k[dy + 1] * k[dx + 1] * shifted(v, dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1))

    def frame(self, radiance, motion, depth, normal):
        """radiance: demodulated (h, w, 3); motion: (h, w, 2) pixel offsets to the previous frame
        (x, y); depth: (h, w) linear; normal: (h, w, 3) unit. Returns the filtered radiance."""
        col, mom, var, hist = self.temporal(radiance, motion, depth, normal)
        out, first = self.atrous(col, var, depth, normal)
        self.color, self.moments, self.hist = first, mom, hist
        self.depth, self.normal = depth, normal
        return out


def selftest():
    rng = np.random.default_rng(3)
    h, w = 48, 64
    yy, xx = np.mgrid[0:h, 0:w]
    clean = np.stack([0.5 + 0.4 * np.sin(xx / 9.0), 0.5 + 0.3 * np.cos(yy / 7.0), 0.4 + 0.0 * xx], -1)
    clean[:, w // 2:] *= 0.3   # an edge the filter must keep
    depth = np.where(xx < w // 2, 2.0, 3.0)
    normal = np.zeros((h, w, 3)); normal[..., 2] = 1
    motion = np.zeros((h, w, 2))
    f = Svgf(h, w)
    errs = []
    for t in range(8):
        noisy = clean * rng.exponential(1.0, clean.shape)   # unbiased, heavy 1 spp-like noise
        out = f.frame(noisy, motion, depth, normal)
        errs.append((np.sqrt(np.mean((noisy - clean) ** 2)), np.sqrt(np.mean((out - clean) ** 2))))
    edge = abs(out[:, w // 2 - 3:w // 2].mean() - out[:, w // 2:w // 2 + 3].mean())
    true_edge = abs(clean[:, w // 2 - 3:w // 2].mean() - clean[:, w // 2:w // 2 + 3].mean())
    ok = errs[-1][1] < 0.25 * errs[-1][0] and errs[-1][1] < errs[0][1] and edge > 0.8 * true_edge
    print("rmse noisy -> filtered by frame:", ", ".join(f"{a:.3f}->{b:.3f}" for a, b in errs),
          f"; edge kept {edge:.3f} of {true_edge:.3f}")
    print("selftest", "pass" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        sys.exit(selftest())
    print(__doc__)
    sys.exit(2)
