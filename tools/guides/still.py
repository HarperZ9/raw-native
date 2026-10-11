#!/usr/bin/env python3
"""Render one frame of a Motion scene to a lossless PNG, through scripts/motion_render.py.

    python tools/guides/still.py --scene ../../templates/guides/shots/tallow-sheet.mjs --out still.png \\
        [--t 0] [--width 1920 --height 1080] [--adapter swiftshader|gpu] [--warm 2]

The frame at time T is drawn by the same capture page and renderer as a film. Instead of a
video encoder, ffmpeg writes the frame as a PNG (and, with --raw, its RGBA bytes beside it).
The default adapter is SwiftShader, Chrome's software WebGPU, so no GPU lock is needed. The
GPU adapter is only for use under the D:/gpu.lock.d protocol.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import motion_render as mr  # noqa: E402


def png_encoder(ffmpeg: str, w: int, h: int, fps: int, codec: str, quality: int, out: Path) -> list[str]:
    return [ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", f"{w}x{h}", "-r", str(fps), "-i", "-",
            "-c:v", "png", "-pix_fmt", "rgba", "-update", "1", "-f", "image2", str(out)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--scene", required=True, help="scene module, relative to web/motion/capture.html")
    ap.add_argument("--out", required=True)
    ap.add_argument("--t", type=float, default=0.0)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--warm", type=int, default=2)
    a = ap.parse_args()
    mr.encoder = png_encoder
    t0 = round(a.t * a.fps) / a.fps
    return mr.main(["--root", str(ROOT), "--scene", a.scene, "--out", str(Path(a.out).resolve()), "--transport", "raw",
                    "--from", f"{t0}", "--to", f"{t0 + 1 / a.fps}", "--fps", str(a.fps), "--width", str(a.width),
                    "--height", str(a.height), "--adapter", a.adapter, "--warm", str(a.warm)])


if __name__ == "__main__":
    sys.exit(main())
