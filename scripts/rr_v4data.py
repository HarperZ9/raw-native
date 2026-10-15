#!/usr/bin/env python3
"""HW H2 v4 dataset (evidence/rr-bounds-amendment-6.json): the 1 spp inputs of every frame of an existing dataset
rendered again on the CPU reference path tracer with interface v2's split radiance (`rr-render --input-only --cpu`),
with the existing references linked in unchanged.

For each sequence of OLD/{train,val,test}: render every frame into NEW with the same scene, path and frame count;
check that each frame's seed, camera and previous camera equal the old frame's; hard-link the old fKKK_ref.f32
(a copy when a link is impossible); merge the old frame's reference fields into the new JSON; check that the split
parts add up to the input radiance within 1e-4 (1 + |r|). Frames already done are skipped, so the job resumes.

Usage: rr_v4data.py OLD NEW --cli raw_native_cli.exe
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

NOWIN = 0x08000000 if os.name == "nt" else 0
REF_FIELDS = ("ref_spp", "ref_chunk", "ms_ref", "ref_mean_var_of_mean", "ref_fraction_se_under_1pct", "backend", "adapter")


def frames_of(d):
    n = 0
    while (d / f"f{n:03d}.json").exists():
        n += 1
    return n


def sequence(cli, old, new):
    n = frames_of(old)
    m0 = json.loads((old / "f000.json").read_text())
    new.mkdir(parents=True, exist_ok=True)
    todo = [k for k in range(n) if not (new / f"f{k:03d}.done").exists()]
    if todo:
        r = subprocess.run([cli, "rr-render", "--scene", m0["scene"], "--path", str(m0["path"]), "--frames", str(m0["frames"]),
                            "--first", str(todo[0]), "--count", str(n - todo[0]), "--width", str(m0["width"]),
                            "--height", str(m0["height"]), "--cpu", "--input-only", "--out", str(new)],
                           capture_output=True, text=True, creationflags=NOWIN)
        if r.returncode != 0:
            raise SystemExit(f"rr-render failed on {old}: {r.stdout[-500:]} {r.stderr[-500:]}")
    worst = 0.0
    for k in range(n):
        mo = json.loads((old / f"f{k:03d}.json").read_text())
        mn = json.loads((new / f"f{k:03d}.json").read_text())
        for f in ("seed", "camera", "previous", "width", "height", "scene", "path", "frame"):
            if mo[f] != mn[f]:
                raise SystemExit(f"{new}/f{k:03d}: {f} differs from the old frame")
        h, w = mn["height"], mn["width"]
        x = np.fromfile(new / f"f{k:03d}_in.f32", np.float32).reshape(h, w, 17)
        sp = np.fromfile(new / f"f{k:03d}_split.f32", np.float32).reshape(h, w, 15)
        r = x[..., 0:3]
        err = float((np.abs(sp[..., 0:3] + sp[..., 3:6] + sp[..., 6:9] - r) / (1e-4 * (1 + np.abs(r)))).max())
        worst = max(worst, err)
        if err > 1.0:
            raise SystemExit(f"{new}/f{k:03d}: split does not add up to the radiance ({err:.3g} of the bound)")
        ref_old, ref_new = old / f"f{k:03d}_ref.f32", new / f"f{k:03d}_ref.f32"
        if not ref_new.exists():
            try:
                os.link(ref_old, ref_new)
            except OSError:
                shutil.copyfile(ref_old, ref_new)
        mn["input_backend"] = "cpu"
        mn["reference_from"] = str(ref_old).replace("\\", "/")
        for f in REF_FIELDS:
            if f in mo:
                mn["reference_" + f if f in ("backend", "adapter") else f] = mo[f]
        (new / f"f{k:03d}.json").write_text(json.dumps(mn) + "\n")
        (new / f"f{k:03d}.done").write_text("")
    return n, worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--cli", required=True)
    a = ap.parse_args()
    report = {}
    for split in ("train", "val", "test"):
        for d in sorted(p for p in (Path(a.old) / split).iterdir() if p.is_dir()):
            n, worst = sequence(a.cli, d, Path(a.new) / split / d.name)
            report[f"{split}/{d.name}"] = {"frames": n, "split_sum_worst_ratio_to_bound": worst}
            print(json.dumps({f"{split}/{d.name}": report[f"{split}/{d.name}"]}), flush=True)
    (Path(a.new) / "v4data.json").write_text(json.dumps(report, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
