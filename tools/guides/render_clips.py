#!/usr/bin/env python3
"""Render the guides' motion clips (templates/guides/clips/clip.mjs) as short videos, and a reel.

    python tools/guides/render_clips.py --out DIR [--adapter gpu] [--only tallow-idle ...]

Each guide gets an idle loop, a talk loop and its signature action, in its home world: Tallow
in the retro thriller office, Pylon on the painted harbour, Rerun on the pixel shore. Frames
come from scripts/motion_render.py (raw transport, H.264 by NVENC on the GPU, so the encoder keeps up with the page; libx264 at its slow preset fell behind and the page's frame posts timed out). --adapter gpu
is for use under the D:/gpu.lock.d protocol only. The reel joins every clip in order into
DIR/guides-reel.mp4 with ffmpeg.
"""

from __future__ import annotations

import argparse
import os
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLIPS = {"idle": 2.4, "talk": 2.4, "action": 3.2}
# The module query is percent-encoded: the capture page takes the scene as one parameter.
# (guide, clip, framing): talk is close on the face, idle and action are medium.
# Each entry: (name, module query, seconds). The two camera moves come last.
PLAN = [(f"{g}-{c}", f"guide={g}&clip={c}&frame={'close' if c == 'talk' else 'medium'}", CLIPS[c]) for g in ("tallow", "pylon", "rerun") for c in ("idle", "talk", "action")]
# The narrated intros, each in its guide's home world, close; the narration (private, rendered by
# the author's voice model, never in this repository) is muxed in from $GUIDES_VOICE/GUIDE/paced.wav.
# Without GUIDES_VOICE the intros are skipped. Their length comes from the mouth curve.
VOICE = Path(os.environ["GUIDES_VOICE"]) if os.environ.get("GUIDES_VOICE") else None
PLAN += [(f"{g}-intro", f"guide={g}&clip=intro&frame=close", 0) for g in ("tallow", "pylon", "rerun")]
PLAN += [("rerun-pan", "guide=rerun&clip=idle&frame=wide&move=pan&mode=sharp", 4.8),
         ("pylon-push", "guide=pylon&clip=idle&frame=close&move=push&ease=inout", 4.8)]


def join(pieces: list, out: Path, audio: str | None) -> None:
    """Join video pieces losslessly, and mux the narration in (AAC) when there is one."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import motion_render as mr
    ff = mr.find_ffmpeg(None)
    lst = out.with_suffix(".pieces.txt")
    lst.write_text("".join(f"file '{p.as_posix()}'\n" for p in pieces), encoding="utf-8")
    src = ["-f", "concat", "-safe", "0", "-i", str(lst)] + (["-i", audio] if audio else [])
    maps = ["-map", "0:v"] + (["-map", "1:a", "-c:a", "aac", "-b:a", "192k"] if audio else [])
    subprocess.run([ff, "-y", "-loglevel", "error", *src, *maps, "-c:v", "copy", "-movflags", "+faststart", str(out)], check=True)


def reel(out: Path, made: list, loops: int) -> None:
    """Join every clip, loops twice, into DIR/guides-reel.mp4 with sound: clips without
    narration get silence, the joined sound is brought to -16 LUFS with a -1.5 dBTP ceiling
    (ffmpeg loudnorm, two passes, linear), and the result is measured again."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import motion_render as mr
    ff = mr.find_ffmpeg(None)
    parts = []
    for name, mp4, c in made:
        for k in range(loops if c == "loop" else 1):
            p = out / "reel-parts" / f"{name}-{k}.mp4"
            p.parent.mkdir(exist_ok=True)
            has_audio = "Audio:" in subprocess.run([ff, "-i", str(mp4)], capture_output=True, text=True).stderr
            src = ["-i", str(mp4)] + ([] if has_audio else ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"])
            subprocess.run([ff, "-y", "-loglevel", "error", *src, "-map", "0:v", "-map", "0:a" if has_audio else "1:a", "-shortest",
                            "-c:v", "copy", "-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2", str(p.with_suffix(".mov"))], check=True)
            parts.append(p.with_suffix(".mov"))
    lst = out / "reel.txt"
    lst.write_text("".join(f"file '{p.as_posix()}'\n" for p in parts), encoding="utf-8")
    joined = out / "reel-joined.mov"
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(lst), "-c", "copy", str(joined)], check=True)
    probe = subprocess.run([ff, "-i", str(joined), "-af", "loudnorm=I=-16:TP=-1.5:LRA=11:print_format=json", "-f", "null", "-"], capture_output=True, text=True).stderr
    m = json.loads(probe[probe.rindex("{"):probe.rindex("}") + 1])
    ln = (f"loudnorm=I=-16:TP=-1.5:LRA=11:measured_I={m['input_i']}:measured_TP={m['input_tp']}:measured_LRA={m['input_lra']}"
          f":measured_thresh={m['input_thresh']}:offset={m['target_offset']}:linear=true")
    final = out / "guides-reel.mp4"
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(joined), "-af", ln + ",aresample=48000", "-c:v", "libx264", "-crf", "18", "-preset", "slow",
                    "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", str(final)], check=True)
    print(final)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--adapter", choices=["swiftshader", "gpu"], default="swiftshader")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--loops", type=int, default=2, help="times each loop clip repeats in the reel")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--extra", nargs="*", default=[], help="NAME=QUERY=SECONDS clips to render instead of the plan (tests)")
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    made = []
    plan = [tuple(e.split("=", 1)[0:1] + e.split("=", 1)[1].rsplit("=", 1)) for e in a.extra] if a.extra else PLAN
    plan = [(n, q, float(d)) for n, q, d in plan]
    for name, query, dur in plan:
        audio = []
        if "clip=intro" in query and VOICE is None:
            print(f"{name}: skipped (set GUIDES_VOICE to the private narration folder)")
            continue
        if "clip=intro" in query:
            g = query.split("guide=")[1].split("&")[0]
            m = json.loads((ROOT / "templates" / "guides" / "intros" / f"{g}.mouth.json").read_text(encoding="utf-8"))
            dur = dur or m["seconds"]
            query += f"&dur={dur}"
            audio = ["--audio", str(VOICE / g / "paced.wav")]
        c = "action" if ("clip=action" in query or "move=" in query or "clip=intro" in query) else "loop"
        if a.only and name not in a.only:
            continue
        t0 = time.time()
        mp4 = out / f"{name}.mp4"
        # Long clips render in 6 s pieces and are joined: on this machine a single page render
        # past about 8 s lost its frame posts to the local server (ERR_CONNECTION_TIMED_OUT), while
        # every piece of 8 s or less rendered cleanly. Clips are stateless in time, so the pieces join exactly.
        pieces, t = [], 0.0
        while t < dur - 1e-6:
            t1 = min(dur, t + 6.0)
            piece = out / "pieces" / f"{name}-{len(pieces):02d}.mp4"
            piece.parent.mkdir(exist_ok=True)
            cmd = [sys.executable, str(ROOT / "scripts" / "motion_render.py"), "--root", str(ROOT), "--scene",
                   "../../templates/guides/clips/clip.mjs%3F" + query.replace("&", "%26"), "--out", str(piece), "--from", f"{t:.4f}", "--to", f"{t1:.4f}",
                   "--fps", str(a.fps), "--transport", "raw", "--codec", "h264_nvenc", "--quality", "18", "--adapter", a.adapter]
            for _ in range(3):
                try:
                    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
                except subprocess.TimeoutExpired:
                    continue
                if r.returncode == 0 or "Failed to fetch" not in (r.stdout + r.stderr):
                    break
            else:
                print(f"{name}: piece {len(pieces)} failed three times or timed out", file=sys.stderr)
                return 1
            if r.returncode != 0:
                print(f"{name}: failed\n{r.stdout[-1500:]}\n{r.stderr[-1500:]}", file=sys.stderr)
                return 1
            pieces.append(piece)
            t = t1
        join(pieces, mp4, audio[1] if audio else None)
        print(f"{name}: {time.time() - t0:.1f} s", flush=True)
        made.append((name, mp4, c))
    if not a.only and not a.extra:
        reel(out, made, a.loops)
    return 0


if __name__ == "__main__":
    sys.exit(main())
