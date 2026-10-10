"""Colour-difference metrics for the colour evidence (numpy only).

ciede2000_display: decode two encoded SDR outputs to display-linear light, convert
to XYZ with the output's primaries and to CIELAB with the display white as the
reference white, and return CIEDE2000 per sample (Sharma, Wu and Dalal, 2005).
delta_e_itp_pq: decode two PQ Rec.2020 outputs to absolute luminance and return
Delta E ITP per sample (ITU-R BT.2124).
"""

from __future__ import annotations

import numpy as np

PRIMARIES = {
    "srgb": ((0.64, 0.33), (0.30, 0.60), (0.15, 0.06)),
    "display-p3": ((0.680, 0.320), (0.265, 0.690), (0.150, 0.060)),
    "rec2020": ((0.708, 0.292), (0.170, 0.797), (0.131, 0.046)),
}
D65 = (0.3127, 0.3290)


def rgb_to_xyz(prim, white=D65) -> np.ndarray:
    m = np.array([[x for x, _ in prim], [y for _, y in prim], [1 - x - y for x, y in prim]], dtype=np.float64)
    w = np.array([white[0] / white[1], 1.0, (1 - white[0] - white[1]) / white[1]])
    return m * np.linalg.solve(m, w)


def decode(v: np.ndarray, output: str) -> np.ndarray:
    v = np.clip(v, 0.0, None)
    if output == "rec2020":
        return v ** 2.4
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def lab(xyz: np.ndarray, white: np.ndarray) -> np.ndarray:
    t = xyz / white
    d = 6 / 29
    f = np.where(t > d ** 3, np.cbrt(t), t / (3 * d * d) + 4 / 29)
    return np.stack([116 * f[:, 1] - 16, 500 * (f[:, 0] - f[:, 1]), 200 * (f[:, 1] - f[:, 2])], axis=1)


def ciede2000(lab1: np.ndarray, lab2: np.ndarray) -> np.ndarray:
    L1, a1, b1 = lab1.T
    L2, a2, b2 = lab2.T
    C1, C2 = np.hypot(a1, b1), np.hypot(a2, b2)
    Cb7 = ((C1 + C2) / 2) ** 7
    G = 0.5 * (1 - np.sqrt(Cb7 / (Cb7 + 25.0 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dLp, dCp = L2 - L1, C2p - C1p
    dh = h2p - h1p
    dh = np.where(C1p * C2p == 0, 0, np.where(dh > 180, dh - 360, np.where(dh < -180, dh + 360, dh)))
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dh / 2))
    Lbp, Cbp = (L1 + L2) / 2, (C1p + C2p) / 2
    hs = h1p + h2p
    hbp = np.where(C1p * C2p == 0, hs, np.where(np.abs(h1p - h2p) <= 180, hs / 2, np.where(hs < 360, (hs + 360) / 2, (hs - 360) / 2)))
    T = (1 - 0.17 * np.cos(np.radians(hbp - 30)) + 0.24 * np.cos(np.radians(2 * hbp))
         + 0.32 * np.cos(np.radians(3 * hbp + 6)) - 0.20 * np.cos(np.radians(4 * hbp - 63)))
    SL = 1 + 0.015 * (Lbp - 50) ** 2 / np.sqrt(20 + (Lbp - 50) ** 2)
    SC, SH = 1 + 0.045 * Cbp, 1 + 0.015 * Cbp * T
    Cbp7 = Cbp ** 7
    RT = -2 * np.sqrt(Cbp7 / (Cbp7 + 25.0 ** 7)) * np.sin(np.radians(60 * np.exp(-(((hbp - 275) / 25) ** 2))))
    return np.sqrt((dLp / SL) ** 2 + (dCp / SC) ** 2 + (dHp / SH) ** 2 + RT * (dCp / SC) * (dHp / SH))


def ciede2000_display(enc1: np.ndarray, enc2: np.ndarray, output: str) -> np.ndarray:
    m = rgb_to_xyz(PRIMARIES[output])
    white = m @ np.ones(3)
    l1 = lab(decode(enc1, output) @ m.T, white)
    l2 = lab(decode(enc2, output) @ m.T, white)
    return ciede2000(l1, l2)


M1, M2 = 2610 / 16384, 2523 / 4096 * 128
C1, C2, C3 = 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32


def pq_decode(v: np.ndarray) -> np.ndarray:            # to nits
    p = np.clip(v, 0, None) ** (1 / M2)
    return 10000 * (np.clip(p - C1, 0, None) / (C2 - C3 * p)) ** (1 / M1)


def pq_encode(nits: np.ndarray) -> np.ndarray:
    y = (np.clip(nits, 0, None) / 10000) ** M1
    return ((C1 + C2 * y) / (1 + C3 * y)) ** M2


def ictcp(rgb2020_nits: np.ndarray) -> np.ndarray:
    lms = rgb2020_nits @ (np.array([[1688, 2146, 262], [683, 2951, 462], [99, 309, 3688]]) / 4096).T
    lp = pq_encode(lms)
    return lp @ (np.array([[2048, 2048, 0], [6610, -13613, 7003], [17933, -17390, -543]]) / 4096).T


def delta_e_itp_pq(enc1: np.ndarray, enc2: np.ndarray) -> np.ndarray:
    a, b = ictcp(pq_decode(enc1)), ictcp(pq_decode(enc2))
    d = a - b
    return 720 * np.sqrt(d[:, 0] ** 2 + (0.5 * d[:, 1]) ** 2 + d[:, 2] ** 2)
