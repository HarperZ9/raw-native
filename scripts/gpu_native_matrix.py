#!/usr/bin/env python3
"""Run the GPU check matrix and the GPU timing matrix on a native GPU build
(raw_native_cli built with RAW_NATIVE_GPU_D3D12=ON), the same matrix
bench/gpu.html runs for WebGPU.

Checks: three frame sizes and four more views at 512 x 512, plus a moving
camera, each with and without the ray-traced pass (16 renders). Each render
writes its GPU and CPU files, gpu_certificate.json and both AO certificates;
both output directories are rechecked with `raw_native_cli verify`.
Timing: `--gpu --bench 5` at three sizes, with and without the ray-traced pass.

Usage: gpu_native_matrix.py <raw_native_cli> <checks.json> <bench.json> [work dir]
Exit code 0 only when every render exits 0, every GPU certificate says
verified and every directory verifies.
"""
import hashlib
import json
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bench_native import run_unthrottled  # noqa: E402

SIZES = [(256, 256), (512, 512), (1440, 900)]
VIEWS = {  # the views of the README and bench/gpu.html
    "high-512": ["--width", "512", "--height", "512", "--eye", "0,9,3", "--target", "0,0.5,0"],
    "low-512": ["--width", "512", "--height", "512", "--eye", "6,1.5,2", "--target", "0,1,0"],
    "close-512": ["--width", "512", "--height", "512", "--eye", "2,2.5,3", "--target", "0,0.8,0", "--fovy", "0.7"],
    "wide-512": ["--width", "512", "--height", "512", "--eye", "5,5,8", "--target", "0,0.5,0", "--fovy", "1.2"],
    "motion-256": ["--width", "256", "--height", "256", "--prev-eye", "4.3,4,5.7"],
}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git_commit():
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True,
                              cwd=Path(__file__).resolve().parents[1], check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def check(cli, name, args, rt, work):
    out = work / f"{name}-{'rt' if rt else 'nort'}"
    cmd = [cli, "--gpu", "--out", str(out)] + args + ([] if rt else ["--no-rt"])
    proc = subprocess.run(cmd, capture_output=True, text=True)
    load = lambda p: json.loads(p.read_text()) if p.exists() else None
    entry = {"case": name, "rt": rt, "exitCode": proc.returncode,
             "gpu_certificate": load(out / "gpu_certificate.json"),
             "gpu_ao_certificate": load(out / "certificate.json"),
             "cpu_ao_certificate": load(out / "cpu" / "certificate.json")}
    entry["verify"] = {d: subprocess.run([cli, "verify", str(out / d)], capture_output=True).returncode
                       for d in (".", "cpu")} if proc.returncode == 0 else None
    g = entry["gpu_certificate"] or {}
    worst = max(g.get("channels", []), key=lambda c: c["rmse"], default=None)
    print(f"{name} rt={rt}: exit {proc.returncode}, {g.get('verdict')}"
          + (f", worst {worst['name']} rmse {worst['rmse']:.3g}" if worst else "")
          + f", verify {entry['verify']}", flush=True)
    return entry


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    cli, checks_out, bench_out = str(Path(sys.argv[1]).resolve()), Path(sys.argv[2]), Path(sys.argv[3])
    work = Path(sys.argv[4]) if len(sys.argv) > 4 else Path(tempfile.mkdtemp(prefix="raw-gpu-"))
    cases = [(f"{w}x{h}", ["--width", str(w), "--height", str(h)]) for w, h in SIZES] + list(VIEWS.items())
    checks = [check(cli, name, args, rt, work) for name, args in cases for rt in (True, False)]
    adapter = next((c["gpu_certificate"]["adapter"] for c in checks if c["gpu_certificate"]), None)
    host = {"platform": platform.platform(), "processor": platform.processor(),
            "binary_sha256": sha256(cli), "source_commit": git_commit()}
    ok = all(c["exitCode"] == 0 and (c["gpu_certificate"] or {}).get("verdict") == "verified"
             and c["verify"] == {".": 0, "cpu": 0} for c in checks)
    summary = {"renders": len(checks), "verified": sum((c["gpu_certificate"] or {}).get("verdict") == "verified" for c in checks),
               "all_pass": ok}
    checks_out.write_text(json.dumps({"backend": "d3d12", "adapter": adapter, "host": host, "summary": summary,
                                      "checks": checks}, indent=1) + "\n")
    print(f"checks: {summary['verified']} of {summary['renders']} verified", flush=True)

    bench = []
    for w, h in SIZES:
        for rt in (True, False):
            args = [cli, "--gpu", "--bench", "5", "--width", str(w), "--height", str(h)] + ([] if rt else ["--no-rt"])
            r = json.loads(run_unthrottled(args).strip())
            if "error" in r:
                raise RuntimeError(f"bench {w}x{h} rt={rt}: {r['error']}")
            r["variant"] = "d3d12"
            bench.append(r)
            print(f"bench {w}x{h} rt={rt}: {r['median_ms']} ms every channel, "
                  f"{r['frame_only_median_ms']} ms frame only", flush=True)
    bench_out.write_text(json.dumps({
        "note": "Median of 5 renders after one untimed warm-up, per mode. runs_ms reads back every channel; "
                "frame_only_runs_ms reads back only the shaded frame. Both include buffer creation, upload, "
                "every pass and the wait for the GPU. High process priority, power throttling off.",
        "adapter": adapter, "host": host, "results": bench}, indent=2) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
