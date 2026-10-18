#!/usr/bin/env python3
"""The render tool's blank-frame check (scripts/motion_render.py blank_check), with its control.

    python tests/web/motion_blank_check.py

Makes two 2-second clips with ffmpeg: one black, one with a test pattern. The check must fail
the black clip and pass the pattern. Exit 0 when both hold.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import motion_render  # noqa: E402


def clip(ffmpeg: str, src: str, out: Path) -> None:
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-f", "lavfi", "-i", src, "-t", "2", "-pix_fmt", "yuv420p",
                    "-c:v", "libx264", str(out)], check=True)


def main() -> int:
    ffmpeg = motion_render.find_ffmpeg(None)
    with tempfile.TemporaryDirectory() as d:
        black, pattern = Path(d) / "black.mp4", Path(d) / "pattern.mp4"
        clip(ffmpeg, "color=c=black:s=640x360:r=30", black)
        clip(ffmpeg, "testsrc2=s=640x360:r=30", pattern)
        b = motion_render.blank_check(ffmpeg, black, 2.0)
        p = motion_render.blank_check(ffmpeg, pattern, 2.0)
    print("black clip:", b)
    print("pattern clip:", p)
    ok = not b["pass"] and p["pass"]
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
