"""Float64 reference for M1 exit criterion 3, the HDR signal (evidence/m1-hdr-signal-bounds.json).

Transcribed from the published formulas, standard library only, and not from the
engine's code:
- SMPTE ST 2084:2014 (ITU-R BT.2100-2, Table 4), the PQ curve;
- IEC 61966-2-1, the sRGB curve, extended by sign for an extended-range canvas;
- the ACES 2.0 output transform tonescale (the Daniele Evo curve).
Units: 1.0 linear = 100 nits.
"""

from __future__ import annotations

import math

# SMPTE ST 2084 constants, as the standard writes them.
PQ_M1 = 2610 / 16384
PQ_M2 = 2523 / 4096 * 128
PQ_C1 = 3424 / 4096
PQ_C2 = 2413 / 4096 * 32
PQ_C3 = 2392 / 4096 * 32


def pq_encode(linear100: float, m1: float = PQ_M1) -> float:
    """Linear light (1.0 = 100 nits) to the PQ signal in [0, 1]. `m1` exists for the control."""
    y = max(0.0, linear100) / 100.0  # fraction of 10,000 nits
    p = y ** m1
    return ((PQ_C1 + PQ_C2 * p) / (1.0 + PQ_C3 * p)) ** PQ_M2


def pq_decode(signal: float) -> float:
    """The PQ signal to linear light, 1.0 = 100 nits."""
    p = max(0.0, signal) ** (1.0 / PQ_M2)
    return 100.0 * (max(0.0, p - PQ_C1) / (PQ_C2 - PQ_C3 * p)) ** (1.0 / PQ_M1)


def srgb_encode(x: float) -> float:
    x = max(0.0, x)
    return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055


def srgb_extended(x: float) -> float:
    return math.copysign(srgb_encode(abs(x)), x)


def aces2_tonescale(y_in: float, peak: float = 1000.0) -> float:
    """ACES 2.0 tonescale: scene luminance (100 x scene-linear) to display nits."""
    n, n_r, g, c, c_d, w_g, t_1 = peak, 100.0, 1.15, 0.18, 10.013, 0.14, 0.04
    r_hit = 128.0 + (896.0 - 128.0) * (math.log(n / n_r) / math.log(10000.0 / 100.0))
    m_0 = n / n_r
    m_1 = 0.5 * (m_0 + math.sqrt(m_0 * (m_0 + 4 * t_1)))
    u = ((r_hit / m_1) / ((r_hit / m_1) + 1)) ** g
    m = m_1 / u
    w_i = math.log(n / 100.0) / math.log(2.0)
    c_t = c_d / n_r * (1 + w_i * w_g)
    g_ip = 0.5 * (c_t + math.sqrt(c_t * (c_t + 4 * t_1)))
    g_ipp2 = -(m_1 * (g_ip / m) ** (1 / g)) / ((g_ip / m) ** (1 / g) - 1)
    w_2 = c / g_ipp2
    s_2 = w_2 * m_1 * n_r
    u_2 = ((r_hit / m_1) / ((r_hit / m_1) + w_2)) ** g
    m_2 = m_1 / u_2
    f = m_2 * (max(0.0, y_in) / (y_in + s_2)) ** g
    return max(0.0, f * f / (f + t_1)) * n_r


def grey_display_linear(g: float, peak: float = 1000.0) -> float:
    """A scene-linear grey through the ACES 2.0 HDR transform: display linear, 1.0 = 100 nits."""
    return min(aces2_tonescale(100.0 * g, peak), peak) / 100.0


if __name__ == "__main__":
    # Spot values for a reader: PQ of 100 nits is about 0.508, of 1,000 nits about 0.752.
    for nits in (0.0, 100.0, 1000.0, 10000.0):
        print(f"PQ({nits:g} nits) = {pq_encode(nits / 100.0):.12f}")
    print(f"ACES 2.0 HDR 1000: 0.18 grey -> {aces2_tonescale(18.0):.6f} nits")
