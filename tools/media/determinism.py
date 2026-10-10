#!/usr/bin/env python3
"""Render one scene twice and compare every frame's hash (M1 exit criterion 6).

    python tools/media/determinism.py SCENE [--spec docs/media/media.json] [--adapter gpu|swiftshader]
        [--width 960 --height 540] [--max-seconds N] [--out evidence.json]

Each run reads every frame back from the GPU and hashes its RGBA rows, before any
encoder sees it, so the comparison is of what the engine drew. Identity is claimed
for one device class only: the adapter and browser named in the result.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def render(scene: str, a, out: Path) -> dict:
    cmd = [sys.executable, str(HERE / "raw_native_media.py"), "render", scene, "--spec", a.spec, "--out", str(out),
           "--width", str(a.width), "--height", str(a.height), "--adapter", a.adapter, "--hash-frames"]
    if a.max_seconds:
        cmd += ["--max-seconds", str(a.max_seconds)]
    subprocess.run(cmd, check=True)
    d = out / scene
    m = json.loads((d / "media.json").read_text(encoding="utf-8"))
    return {"hashes": (d / "frames.sha256").read_text(encoding="utf-8").split("\n")[:-1], "manifest": m}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("scene")
    ap.add_argument("--spec", default="docs/media/media.json")
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="swiftshader")
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--height", type=int, default=540)
    ap.add_argument("--max-seconds", type=float)
    ap.add_argument("--out")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        r1 = render(a.scene, a, Path(tmp) / "a")
        r2 = render(a.scene, a, Path(tmp) / "b")
    h1, h2 = r1["hashes"], r2["hashes"]
    diff = [i for i, (x, y) in enumerate(zip(h1, h2)) if x != y]
    m = r1["manifest"]
    res = {"scene": a.scene, "commit": m["commit"], "adapter": m.get("adapter"), "adapter_kind": a.adapter, "user_agent": m.get("user_agent"),
           "size": f"{a.width}x{a.height}", "frames": len(h1), "frames_second_run": len(h2),
           "identical": len(h1) == len(h2) and not diff and len(h1) > 0, "differing_frames": diff[:20], "differing_count": len(diff),
           "chain_sha256": [r1["manifest"].get("frame_chain_sha256"), r2["manifest"].get("frame_chain_sha256")],
           "does_not_prove": "Identity across two runs on this adapter and browser. Not across GPUs, drivers or browsers."}
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 0 if res["identical"] else 1


if __name__ == "__main__":
    sys.exit(main())
