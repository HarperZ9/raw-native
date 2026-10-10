"""Does float32 evaluation with GPU-like transcendental precision reproduce the failing channels?

    python compare.py CLI F32DIAG GPU_RUN.json OUT.json      (REF=diag: the tool's exact mode is the reference)

GPU_RUN.json is a tests/web/hdr_signal.py result with its list of failing channels.
Build f32diag with: cmake -S tools/colour/f32diag -B build-f32diag -DSRC=<repo root>.
A diagnostic, not part of the build or of CI (evidence/m1-hdr-f32-diagnosis.json).
"""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

cli, diag, run3, out = sys.argv[1:5]
PIPES = {"aces2-hdr1000/srgb-extended": lambda c: max(1 / 1023, 0.005 * abs(c)),
         "aces2-hdr1000/rec2100-pq": lambda c: 1 / 1023}
fail = json.loads(Path(run3).read_text(encoding="utf-8"))["gpu"]["pipelines"]["aces2-hdr1000/srgb-extended"]["failing"]
gpu_fail = {f["index"] for f in fail}


def read(p):
    b = Path(p).read_bytes()
    return struct.unpack(f"<{len(b) // 4}f", b)


def compare(a, b, bound):
    over = [i for i, (x, y) in enumerate(zip(a, b)) if abs(x - y) > bound(y)]
    worst = max((abs(x - y) / bound(y) for x, y in zip(a, b)), default=0)
    return over, worst


res = {}
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    subprocess.run([cli, "colour", "grid", str(t / "grid.f32")], check=True)
    for pipe, bound in PIPES.items():
        ref_p = t / "ref.f32"
        subprocess.run([cli, "colour", "apply", pipe, str(t / "grid.f32"), str(ref_p)], check=True)
        ref = read(ref_p)
        import os
        old = ref
        if os.environ.get("REF") == "diag":
            subprocess.run([diag, pipe, "0", "0", "0", str(t / "grid.f32"), str(ref_p)], check=True)
            ref = read(ref_p)
            print(pipe, "fixed exact vs old reference: max abs", max(abs(x - y) for x, y in zip(ref, old)), file=sys.stderr)
        rows = {}
        variants = [("exact", 0, 0, 0)] + [("pow-exp2-log2", 1, 0, 0)] + [(f"noise-{u}ulp-seed{s}", 2, s, u) for u in (2, 8) for s in (1, 2, 3)] + [(f"wgsl-limits-seed{s}", 3, s, 8) for s in (1, 2, 3)] + [(f"limits-no-atan2-seed{s}", 4, s, 8) for s in (1, 2, 3)] + [(f"atan2-only-seed{s}", 5, s, 0) for s in (1, 2, 3)]
        for name, mode, seed, ulps in variants:
            o = t / "v.f32"
            subprocess.run([diag, pipe, str(mode), str(seed), str(ulps), str(t / "grid.f32"), str(o)], check=True)
            v = read(o)
            over, worst = compare(v, ref, bound)
            rows[name] = {"over": len(over), "worstRatio": round(worst, 4),
                          "bitIdentical": v == ref if mode == 0 else None,
                          "overlapWithGpuFailures": len(set(over) & gpu_fail) if pipe.endswith("extended") else None}
        res[pipe] = rows
Path(out).write_text(json.dumps({"gpuFailures": len(gpu_fail), "pipelines": res}, indent=1) + "\n", encoding="utf-8")
print(json.dumps(res, indent=1))
