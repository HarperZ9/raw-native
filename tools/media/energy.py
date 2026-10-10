#!/usr/bin/env python3
"""Joules per frame for a media render, from the GPU's own power telemetry (M1 criterion 7).

    python tools/media/energy.py SCENE [--spec ...] [--width 1920 --height 1080] [--out evidence.json]

Method, written before the first measurement:
1. Read the board power with `nvidia-smi --query-gpu=power.draw` every 100 ms for 10 s
   with the GPU idle. The median is the idle power.
2. Render the scene on the GPU and keep sampling, at the same rate, until the render ends.
3. Energy is the trapezoid sum of power over the render's wall time. Energy above idle
   subtracts idle power times the same time. Each is divided by the frame count.
The telemetry is the board's reported power, not a meter on the rail, and a render
includes the browser, readback and encoding, not the draw alone. Where nvidia-smi or
power telemetry is missing, the result says "unavailable" and gives no number.
Take the GPU lock (D:/gpu.lock.d on the reference machine) before running.
"""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def sample(stop: threading.Event, out: list, period: float = 0.1) -> None:
    while not stop.is_set():
        r = subprocess.run(["nvidia-smi", "--query-gpu=power.draw", "--format=csv,noheader,nounits"], capture_output=True, text=True)
        try:
            out.append((time.monotonic(), float(r.stdout.strip().splitlines()[0])))
        except (ValueError, IndexError):
            pass
        stop.wait(period)


def integrate(samples: list) -> float:
    return sum((t1 - t0) * (p0 + p1) / 2 for (t0, p0), (t1, p1) in zip(samples, samples[1:]))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("scene")
    ap.add_argument("--spec", default="docs/media/media.json")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--out")
    a = ap.parse_args()
    res = {"scene": a.scene, "size": f"{a.width}x{a.height}", "method": __doc__.split("Method, written before the first measurement:")[1].split("Take the GPU")[0].strip()}
    if not shutil.which("nvidia-smi"):
        res["result"] = "unavailable: no nvidia-smi"
    else:
        idle, stop = [], threading.Event()
        th = threading.Thread(target=sample, args=(stop, idle)); th.start(); time.sleep(10); stop.set(); th.join()
        idle_w = statistics.median(p for _, p in idle) if idle else None
        run, stop = [], threading.Event()
        th = threading.Thread(target=sample, args=(stop, run)); th.start()
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([sys.executable, str(HERE / "raw_native_media.py"), "render", a.scene, "--spec", a.spec, "--out", tmp,
                            "--width", str(a.width), "--height", str(a.height), "--adapter", "gpu"], check=True)
            m = json.loads((Path(tmp) / a.scene / "media.json").read_text(encoding="utf-8"))
        stop.set(); th.join()
        if len(run) < 2 or idle_w is None:
            res["result"] = "unavailable: no power readings"
        else:
            j = integrate(run)
            span = run[-1][0] - run[0][0]
            res.update({"adapter": m.get("adapter"), "frames": m["frames"], "wall_seconds_sampled": round(span, 2), "samples": len(run),
                        "idle_watts": round(idle_w, 2), "joules": round(j, 1), "joules_per_frame": round(j / m["frames"], 4),
                        "joules_above_idle_per_frame": round((j - idle_w * span) / m["frames"], 4),
                        "frames_per_joule": round(m["frames"] / j, 4), "commit": m["commit"],
                        "does_not_prove": "Board-reported power over the whole render (browser, readback, encode), not the draw alone, and not a meter reading."})
    print(json.dumps(res, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
