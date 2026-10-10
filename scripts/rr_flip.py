#!/usr/bin/env python3
"""LDR FLIP (Andersson, Nilsson, Akenine-Moller, Oskarsson, Astrom, Fairchild, 2020), our own
numpy implementation for HW H2 (evidence/rr-bounds.json, metric "flip").

Written from the paper's algorithm; the constants were read from the published BSD-3
reference implementation (NVlabs/flip, src/pytorch/flip_loss.py) and are cited inline.
No reference code is copied. numpy only, so it runs in any environment.

Status: UNCHECKED against the reference implementation's own test pair. That check needs the
reference images on disk (a download the author has not approved). Until it is done, every
FLIP number this script produces is labelled unchecked (rr-bounds.json, metric "flip").

Usage: rr_flip.py --selftest
       rr_flip.py REF.pfm TEST.pfm [--ppd 67.02]   (linear RGB in [0, 1] after tone mapping is the
                                                    caller's job; inputs are sRGB-encoded floats)
"""
import math
import sys

import numpy as np

QC, QF, PC, PT, EPS = 0.7, 0.5, 0.4, 0.95, 1e-15
PPD_DEFAULT = (0.7 * 3840 / 0.7) * math.pi / 180  # about 67.02 pixels per degree
# Spatial CSF parameters (a1, b1, a2, b2) for the achromatic, red-green and blue-yellow channels.
CSF = {"A": (1.0, 0.0047, 0.0, 1e-5), "RG": (1.0, 0.0053, 0.0, 1e-5), "BY": (34.1, 0.04, 13.5, 0.025)}
WHITE = np.array([0.950428545, 1.0, 1.088900371])
RGB2XYZ = np.array([[10135552 / 24577794, 8788810 / 24577794, 4435075 / 24577794],
                    [2613072 / 12288897, 8788810 / 12288897, 887015 / 12288897],
                    [1425312 / 73733382, 8788810 / 73733382, 70074185 / 73733382]])
XYZ2RGB = np.array([[3.241003275, -1.537398934, -0.498615861],
                    [-0.969224334, 1.875930071, 0.041554224],
                    [0.055639423, -0.204011202, 1.057148933]])
GW = 0.082  # peak-to-trough width of the human edge detector, in degrees


def srgb2lin(v):
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def lin2xyz(rgb):
    return rgb @ RGB2XYZ.T


def xyz2ycxcz(xyz):
    x, y, z = np.moveaxis(xyz / WHITE, -1, 0)
    return np.stack([116 * y - 16, 500 * (x - y), 200 * (y - z)], -1)


def ycxcz2lin(ycc):
    y = (ycc[..., 0] + 16) / 116
    xyz = np.stack([y + ycc[..., 1] / 500, y, y - ycc[..., 2] / 200], -1) * WHITE
    return xyz @ XYZ2RGB.T


def lin2lab(rgb):
    t = lin2xyz(rgb) / WHITE
    d = 6 / 29
    f = np.where(t > d ** 3, np.cbrt(t), t / (3 * d * d) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def hunt(lab):
    return np.stack([lab[..., 0], 0.01 * lab[..., 0] * lab[..., 1], 0.01 * lab[..., 0] * lab[..., 2]], -1)


def hyab(a, b):
    d = a - b
    return np.sqrt(np.maximum(d[..., 0] ** 2, EPS)) + np.sqrt(d[..., 1] ** 2 + d[..., 2] ** 2)


def conv_sep(img, kx, ky):
    """Separable correlation with replicate padding: rows by kx, columns by ky."""
    r = len(kx) // 2
    p = np.pad(img, ((r, r), (r, r)), mode="edge")
    h, w = img.shape
    rows = sum(kx[i] * p[:, i:i + w] for i in range(len(kx)))
    return sum(ky[i] * rows[i:i + h, :] for i in range(len(ky)))


def csf_radius(ppd):
    m = max(max(v[1], v[3]) for v in CSF.values())
    return int(math.ceil(3 * math.sqrt(m / (2 * math.pi ** 2)) * ppd))


def csf_filter(ch, img, ppd):
    """g = a1 sqrt(pi/b1) exp(-pi^2 z / b1) + a2 sqrt(pi/b2) exp(-pi^2 z / b2), normalised to sum 1.
    Each term is a separable Gaussian, so the 2D filter is applied as a weighted sum of two
    separable passes, each term weighted by its share of the 2D kernel's total."""
    a1, b1, a2, b2 = CSF[ch]
    r = csf_radius(ppd)
    x = np.arange(-r, r + 1) / ppd
    out, terms = 0.0, []
    for a, b in ((a1, b1), (a2, b2)):
        if a == 0:
            continue
        g1 = np.exp(-math.pi ** 2 * x * x / b)
        terms.append((a * math.sqrt(math.pi / b) * g1.sum() ** 2, g1 / g1.sum()))
    total = sum(t[0] for t in terms)
    for weight, g1 in terms:
        out = out + (weight / total) * conv_sep(img, g1, g1)
    return out


def feature_kernels(ppd):
    sd = 0.5 * GW * ppd
    r = int(math.ceil(3 * sd))
    x = np.arange(-r, r + 1, dtype=np.float64)
    g = np.exp(-x * x / (2 * sd * sd))
    gy = g / g.sum()

    def norm(k):  # negatives sum to -1, positives to +1
        k = k.copy()
        k[k < 0] /= -k[k < 0].sum()
        k[k > 0] /= k[k > 0].sum()
        return k
    return norm(-x * g), norm((x * x / (sd * sd) - 1) * g), gy


def features(y, ppd):
    edge, point, gy = feature_kernels(ppd)
    ex, ey = conv_sep(y, edge, gy), conv_sep(y, gy, edge)
    px, py = conv_sep(y, point, gy), conv_sep(y, gy, point)
    return np.hypot(ex, ey), np.hypot(px, py)


def flip_map(ref_srgb, test_srgb, ppd=PPD_DEFAULT):
    """Per-pixel LDR FLIP of two sRGB-encoded float images of shape (h, w, 3)."""
    ref, test = (np.clip(np.asarray(im, np.float64), 0, 1) for im in (ref_srgb, test_srgb))
    yr, yt = (xyz2ycxcz(lin2xyz(srgb2lin(im))) for im in (ref, test))
    filt = []
    for ycc in (yr, yt):
        f = np.stack([csf_filter(ch, ycc[..., i], ppd) for i, ch in enumerate(("A", "RG", "BY"))], -1)
        filt.append(hunt(lin2lab(np.clip(ycxcz2lin(f), 0, 1))))
    cmax = hyab(hunt(lin2lab(np.array([0.0, 1.0, 0.0]))), hunt(lin2lab(np.array([0.0, 0.0, 1.0])))) ** QC
    x = hyab(filt[0], filt[1]) ** QC
    pcc = PC * cmax
    dc = np.where(x < pcc, (PT / pcc) * x, PT + ((x - pcc) / (cmax - pcc)) * (1 - PT))
    er, pr = features((yr[..., 0] + 16) / 116, ppd)
    et, pt_ = features((yt[..., 0] + 16) / 116, ppd)
    df = np.maximum(np.maximum(np.abs(er - et), np.abs(pt_ - pr)), EPS)
    df = (df / math.sqrt(2)) ** QF
    return dc ** (1 - df)


def selftest():
    rng = np.random.default_rng(7)
    a = rng.random((48, 64, 3))
    ok = True
    same = flip_map(a, a).max()
    ok &= same < 1e-6
    noisy = np.clip(a + rng.normal(0, 0.05, a.shape), 0, 1)
    worse = np.clip(a + rng.normal(0, 0.2, a.shape), 0, 1)
    f1, f2 = flip_map(a, noisy).mean(), flip_map(a, worse).mean()
    ok &= 0 < f1 < f2 <= 1
    sym = abs(flip_map(a, noisy).mean() - flip_map(noisy, a).mean())
    black, white = np.zeros((32, 32, 3)), np.ones((32, 32, 3))
    bw = flip_map(black, white).mean()
    ok &= 0.9 < bw <= 1.0
    print(f"identical max {same:.3g}; noise 0.05 mean {f1:.4f} < noise 0.2 mean {f2:.4f}; "
          f"swap difference {sym:.3g} (reported); black vs white {bw:.4f}; csf radius {csf_radius(PPD_DEFAULT)} "
          f"(reference 10 at the default ppd)")
    ok &= csf_radius(PPD_DEFAULT) == 10
    print("selftest", "pass" if ok else "FAIL", "(UNCHECKED against the reference test pair)")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        sys.exit(selftest())
    print(__doc__)
    sys.exit(2)
