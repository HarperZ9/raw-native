#!/usr/bin/env python3
"""Golden renders for Threads on a hardware adapter. Standard library only.

  threads_golden.py CLI [--update] [--out DIR]

CLI is a raw_native_cli with a GPU backend. Six worlds render at 320 x 180 with
65,536 nodes for 60 frames and are compared with evidence/threads/golden-w<N>.png:
a world passes when its mean absolute error is at most 1/255 per channel and at
least 99.5% of its pixels sit within 4/255 on every channel. The tolerance
allows driver-to-driver float drift; on one adapter and driver the render is
bit-identical run to run (checked 2026-10-23 on an RTX 4090).

The second check is structural: every golden must stay far from the ported
version's render of the same world (evidence/threads/ported-w<N>.png, from the
shader at f369fdc). The worlds' compositions are kept on purpose, so the check
reads stroke structure, not layout: the luma minus its 5 x 5 box mean (a high
pass), correlated between the two pictures. Far means a correlation of at most
0.5. The control renders the same world two frames earlier; it must correlate
above 0.7 with this run, or the measure separates everything and proves
nothing.

Correction, 2026-10-23: the first form of this check (mean error of at least
6/255 and plain luma correlation of at most 0.5) was set before any render and
missed on five of six worlds. Plain luma measures layout, which the
resynthesis keeps, so it could not tell the methods apart. Measured with the
high pass: new against ported 0.00 to 0.44, controls 0.76 to 0.98 (and 0.77 to
0.98 for the ported shader against its own near frame).

--update writes the goldens from this run instead of checking them. Exit 0 when
every check holds, 1 otherwise, 2 on a usage or render error.
"""
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "evidence" / "threads"
WORLDS = (0, 1, 3, 5, 6, 13)
W, H = 320, 180


def read_ppm(p):
    data = Path(p).read_bytes()
    parts = data.split(maxsplit=4)
    if parts[0] != b"P6" or int(parts[1]) != W or int(parts[2]) != H:
        raise ValueError(f"{p}: not a {W} x {H} P6 image")
    return parts[4][: W * H * 3]


def write_png(p, rgb):
    rows = b"".join(b"\x00" + rgb[y * W * 3:(y + 1) * W * 3] for y in range(H))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    Path(p).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                        + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def read_png(p):
    """An 8-bit RGB PNG with filter 0 on every row, as write_png makes it."""
    data, i, idat = Path(p).read_bytes(), 8, b""
    while i < len(data):
        n, kind = struct.unpack(">I4s", data[i:i + 8])
        if kind == b"IDAT":
            idat += data[i + 8:i + 8 + n]
        i += 12 + n
    raw = zlib.decompress(idat)
    stride = W * 3 + 1
    if len(raw) != stride * H or any(raw[y * stride] for y in range(H)):
        raise ValueError(f"{p}: not a filter-0 {W} x {H} RGB PNG")
    return b"".join(raw[y * stride + 1:(y + 1) * stride] for y in range(H))


def luma(rgb):
    return [0.25 * rgb[k] + 0.5 * rgb[k + 1] + 0.25 * rgb[k + 2] for k in range(0, len(rgb), 3)]


def high_pass(l, r=2):
    """Luma minus its (2r+1)^2 box mean, edges clamped, via a summed-area table."""
    k = 2 * r + 1
    sat = [[0.0] * (W + 1) for _ in range(H + 1)]
    for y in range(H):
        row = 0.0
        for x in range(W):
            row += l[y * W + x]
            sat[y + 1][x + 1] = sat[y][x + 1] + row
    out = []
    for y in range(H):
        y0, y1 = max(0, y - r), min(H, y + r + 1)
        for x in range(W):
            x0, x1 = max(0, x - r), min(W, x + r + 1)
            box = (sat[y1][x1] - sat[y0][x1] - sat[y1][x0] + sat[y0][x0]) / ((y1 - y0) * (x1 - x0))
            out.append(l[y * W + x] - box)
    return out


def corr(a, b):
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    cov = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    return cov / ((va * vb) ** 0.5 or 1.0)


def compare(a, b):
    n = len(a)
    mae = sum(abs(x - y) for x, y in zip(a, b)) / n
    within = sum(1 for k in range(0, n, 3) if max(abs(a[k + c] - b[k + c]) for c in range(3)) <= 4) / (n // 3)
    return {"mae": mae, "within4": within}


def structure(a, b):
    return corr(high_pass(luma(a)), high_pass(luma(b)))


def render(cli, world, out, frames=60):
    cmd = [cli, "threads", "--out", str(out), "--world", str(world), "--width", str(W), "--height", str(H),
           "--particles", "65536", "--frames", str(frames)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"world {world}: exit {r.returncode}: {r.stdout.strip()}")
    return read_ppm(Path(out) / "threads.ppm"), r.stdout.strip()


def main(a):
    if not a or a[0].startswith("-"):
        print(__doc__)
        return 2
    cli, update = a[0], "--update" in a
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        for w in WORLDS:
            try:
                img, line = render(cli, w, Path(tmp) / f"w{w}")
                near, _ = render(cli, w, Path(tmp) / f"w{w}-near", 58)
            except (RuntimeError, ValueError, OSError) as e:
                print(f"threads_golden: {e}")
                return 2
            golden, ported = EVIDENCE / f"golden-w{w}.png", EVIDENCE / f"ported-w{w}.png"
            if update:
                write_png(golden, img)
                print(f"  wrote {golden.name}: {line}")
            else:
                g = compare(img, read_png(golden))
                holds = g["mae"] <= 1.0 and g["within4"] >= 0.995
                ok &= holds
                print(f"  {'holds' if holds else 'MISSES'}: world {w} golden mae {g['mae']:.3f} within4 {g['within4']:.4f}")
            s = structure(read_png(golden), read_png(ported))
            c = structure(img, near)
            far, control = s <= 0.5, c > 0.7
            ok &= far and control
            print(f"  {'holds' if far else 'MISSES'}: world {w} stroke structure against the ported render {s:.3f}"
                  f"; control {c:.3f}{'' if control else ' (CONTROL FAILED)'}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
