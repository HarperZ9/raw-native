#!/usr/bin/env python3
"""Energy per frame of a raw_native_cli timing command, from NVML's total-energy counter, stdlib only.

The counter is read through superframe's joules package (packages/joules/nvml.py: Window, millijoules
since driver load, no sampling). Setup costs (scene upload, structure build, readback) are cancelled by
differencing two runs that differ only in frame count: J/frame = (E(n2) - E(n1)) / (n2 - n1). The board's
idle energy over the same seconds is measured right before, beside it, and both are reported: the GPU
draws its idle power whether or not this work runs, so the gross and the idle-subtracted numbers answer
different questions.

Usage: gpu_energy.py --joules DIR --n1 10 --n2 40 --out FILE -- CLI ARGS... (the frame count goes in
as --frames N, appended to ARGS)
"""
import argparse
import json
import subprocess
import sys
import time


def run(cmd):
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"gpu_energy: {' '.join(cmd)} exited {p.returncode}: {p.stdout[-400:]}")
    return json.loads(p.stdout), time.perf_counter() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--joules", required=True, help="directory holding superframe's nvml.py")
    ap.add_argument("--n1", type=int, default=10)
    ap.add_argument("--n2", type=int, default=40)
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--out", required=True)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    sys.path.insert(0, a.joules)
    import nvml  # noqa: E402  (superframe packages/joules/nvml.py)
    if not nvml.supported():
        sys.exit("gpu_energy: NVML total energy is not supported here")
    rows = []
    for _ in range(a.repeats):
        with nvml.Window() as idle:
            time.sleep(3.0)
        idle_w = idle.joules / idle.seconds
        res = {}
        for n in (a.n1, a.n2):
            with nvml.Window() as w:
                out, _ = run(cmd + ["--frames", str(n)])
            res[n] = (w.joules, w.seconds, out)
        de = res[a.n2][0] - res[a.n1][0]
        ds = res[a.n2][1] - res[a.n1][1]
        rows.append({"idle_watts": idle_w, "joules_per_frame_gross": de / (a.n2 - a.n1),
                     "joules_per_frame_above_idle": (de - idle_w * ds) / (a.n2 - a.n1),
                     "seconds_per_frame_wall": ds / (a.n2 - a.n1), "timing_n2": res[a.n2][2]})
    gross = sorted(r["joules_per_frame_gross"] for r in rows)
    above = sorted(r["joules_per_frame_above_idle"] for r in rows)
    report = {"method": "NVML total energy, differenced between runs of n1 and n2 frames (scripts/gpu_energy.py)",
              "n1": a.n1, "n2": a.n2, "repeats": a.repeats, "command": cmd,
              "median_joules_per_frame_gross": gross[len(gross) // 2],
              "median_joules_per_frame_above_idle": above[len(above) // 2], "runs": rows}
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    print(json.dumps({k: v for k, v in report.items() if k != "runs"}, indent=1))


if __name__ == "__main__":
    main()
