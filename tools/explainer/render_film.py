#!/usr/bin/env python3
"""Render a Motion scene in resumable chunks on the CPU WebGPU adapter, join them, and write a receipt.

    python tools/explainer/render_film.py --scene ../../docs/explainer/attention.scene.mjs \\
        --out OUTDIR [--seconds 72] [--chunk 6] [--fps 30] [--width 1920 --height 1080] [--vtt FILE]

Each chunk is rendered by scripts/motion_render.py (SwiftShader, raw frames, x264) with
--hash-frames, so every frame's SHA-256 is kept. A chunk whose .mp4 and .frames.sha256 exist is
skipped, so a stopped render resumes where it stopped. The chunks are joined without re-encoding
into OUTDIR/film.mp4, and OUTDIR/receipt.json records the commit, the scene, the settings and the
hash of every frame and of the film. No GPU is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def chunk(a, i: int, t0: float, t1: float, out: Path) -> Path:
    mp4 = out / f"chunk{i:03d}.mp4"
    if mp4.exists() and Path(str(mp4) + ".frames.sha256").exists():
        print(f"chunk {i} done, skipped", flush=True)
        return mp4
    cmd = [sys.executable, str(ROOT / "scripts/motion_render.py"), "--root", str(ROOT), "--scene", a.scene, "--out", str(mp4),
           "--width", str(a.width), "--height", str(a.height), "--fps", str(a.fps), "--from", str(t0), "--to", str(t1),
           "--adapter", "swiftshader", "--transport", "raw", "--codec", "libx264", "--quality", "16", "--hash-frames"]
    for attempt in range(3):   # a long CPU render can drop a localhost request; the chunk restarts whole
        print(f"chunk {i}: {t0}-{t1} s, attempt {attempt + 1}", flush=True)
        if subprocess.run(cmd, cwd=ROOT).returncode == 0:
            return mp4
    raise SystemExit(f"chunk {i} failed three times")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--scene", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=float, required=True)
    ap.add_argument("--chunk", type=float, default=6.0)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--vtt")
    a = ap.parse_args()
    import imageio_ffmpeg
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    # The commit the frames are drawn from: recorded at the start, and kept across resumes.
    start = out / "start.json"
    if not start.exists():
        git = lambda *c: subprocess.run(["git", *c], capture_output=True, text=True, cwd=ROOT).stdout.strip()
        start.write_text(json.dumps({"commit": git("rev-parse", "HEAD"), "dirty_tree": bool(git("status", "--porcelain"))}) + "\n", encoding="utf-8")
    begun = json.loads(start.read_text(encoding="utf-8"))
    parts, t = [], 0.0
    while t < a.seconds - 1e-9:
        parts.append(chunk(a, len(parts), t, min(a.seconds, t + a.chunk), out))
        t += a.chunk
    lst = out / "chunks.txt"
    lst.write_text("".join(f"file '{p.name}'\n" for p in parts), encoding="utf-8")
    film = out / "film.mp4"
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(lst), "-c", "copy", "-movflags", "+faststart", str(film)], check=True)
    frames = []
    for p in parts:
        frames += Path(str(p) + ".frames.sha256").read_text(encoding="utf-8").splitlines()
    receipt = {"schema": "raw-native.film-receipt/1", "scene": a.scene, "commit": begun["commit"], "dirty_tree": begun["dirty_tree"],
               "size": [a.width, a.height], "fps": a.fps, "seconds": a.seconds, "adapter": "swiftshader (CPU)",
               "frames": len(frames), "frames_sha256": hashlib.sha256("\n".join(frames).encode()).hexdigest(),
               "film_sha256": sha(film), "vtt_sha256": sha(Path(a.vtt)) if a.vtt else None,
               "does_not_prove": "That another adapter draws identical bytes; the frame hashes are this adapter's."}
    (out / "receipt.json").write_text(json.dumps(receipt, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
