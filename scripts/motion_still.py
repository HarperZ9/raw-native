#!/usr/bin/env python3
"""Render one frame of a Motion scene to a lossless RGBA PNG.

    python scripts/motion_still.py --scene ../../templates/retro-thriller/scene.mjs --out still.png \
        [--t 0] [--width 1920 --height 1080] [--fps 30] [--adapter swiftshader|gpu] [--warm 0]

The frame at time T (rounded to the frame grid at FPS) is drawn by the same capture page and
renderer as a film (scripts/motion_render.py, raw transport), and ffmpeg writes it as a PNG
instead of a video. --warm N draws N frames before T first, so history layers settle; it is off
by default because Chrome on SwiftShader under Linux loses the device when warm frames draw
to the page's canvas (CI, 14 October 2026).
--scene is relative to web/motion/capture.html. The default adapter is SwiftShader, Chrome's
CPU WebGPU, so a still needs no GPU.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import motion_render as mr  # noqa: E402


def still_args(scene: str, out: Path, t: float, fps: int, width: int, height: int, adapter: str, warm: int) -> list[str]:
    """motion_render.py arguments for the single frame at time t, snapped to the frame grid."""
    t0 = round(t * fps) / fps
    return ["--root", str(ROOT), "--scene", scene, "--out", str(out), "--transport", "raw", "--codec", "png",
            "--from", f"{t0}", "--to", f"{t0 + 1 / fps}", "--fps", str(fps), "--width", str(width),
            "--height", str(height), "--adapter", adapter, "--warm", str(warm)]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--scene", required=True, help="scene module, relative to web/motion/capture.html")
    ap.add_argument("--out", required=True)
    ap.add_argument("--t", type=float, default=0.0)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--warm", type=int, default=0, help="frames drawn before T, for history layers (Threads, temporal passes)")
    a = ap.parse_args(argv)
    if a.t < 0:
        ap.error("--t must be 0 or later")
    return mr.main(still_args(a.scene, Path(a.out).resolve(), a.t, a.fps, a.width, a.height, a.adapter, a.warm))


if __name__ == "__main__":
    sys.exit(main())
