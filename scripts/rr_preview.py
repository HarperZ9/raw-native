#!/usr/bin/env python3
"""Tone-mapped PNG previews of HW H2 dataset frames (numpy and zlib only).

Usage: rr_preview.py DIR OUT.png [--frame K] [--width W --height H]
Writes one row per frame: 1 spp input, reference, albedo. Tone map: x / (1 + x), then sRGB.
"""
import argparse
import json
import struct
import zlib
from pathlib import Path

import numpy as np


def srgb(x):
    x = np.clip(x / (1.0 + x), 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def write_png(path, img):
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + (img[y] * 255 + 0.5).astype(np.uint8).tobytes() for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def load(d, k):
    meta = json.loads((d / f"f{k:03d}.json").read_text())
    w, h = meta["width"], meta["height"]
    x = np.fromfile(d / f"f{k:03d}_in.f32", np.float32).reshape(h, w, 17)
    r = np.fromfile(d / f"f{k:03d}_ref.f32", np.float32).reshape(h, w, 7)
    return x, r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", type=int, default=0)
    a = ap.parse_args()
    rows = []
    for d in a.dirs:
        x, r = load(Path(d), a.frame)
        rows.append(np.concatenate([srgb(x[..., 0:3]), srgb(r[..., 0:3]), np.clip(r[..., 4:7], 0, 1)], axis=1))
    write_png(a.out, np.concatenate(rows, axis=0))


if __name__ == "__main__":
    main()
