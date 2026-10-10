#!/usr/bin/env python3
"""Render a fixed matrix of cases and record the SHA-256 of every file written,
so two builds can be compared byte for byte (stdlib only).

A refactor that must not change output runs this on the build before and the
build after, then compares the two manifests. Every certificate, receipt,
channel summary and image file is hashed as written, with two exceptions that
carry wall-clock time and nothing else:
  gpu_certificate.json  the "timing_ms" object is replaced by a fixed string
                        before hashing (it records how long the renders took);
  gpu_receipt.json      its digest of gpu_certificate.json and its own seal are
                        replaced by the normalized certificate hash, because
                        both follow from the timing above.
Every other byte is compared as written.

Usage:
  identity_matrix.py run <raw_native_cli> <manifest.json> <work dir> [--gpu | --no-gpu-build] [--quick] [--models DIR]
  identity_matrix.py compare <before.json> <after.json> [--except <file name>]...
--gpu renders every case on the GPU backend (each also writes the CPU reference
under cpu/). --no-gpu-build adds one --gpu case that a build without a GPU
backend must answer with exit code 4 and an unverifiable certificate. run exits
0 when every case rendered (exit code 0, or 4 for --gpu with no adapter, which
is recorded). compare exits 0 only when both manifests list the
same cases, the same files and the same hashes. --models DIR runs only the glTF
model cases of ROADMAP M2 criterion 1 (evidence/m2-gltf-models.json; fetch them with
tools/assets/fetch_gltf.py), each at 128 x 128 without the ray-traced reference (--no-rt). --except leaves out a file
name that is expected to differ between the two manifests: arena_certificate.json
between toolchains, whose standard libraries allocate differently (MSVC and
libstdc++ give different allocation counts for the same render).
"""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

# The CPU cases: the default render, the README views, the matrix sizes, the
# raster-only path, motion and a multi-threaded run (output must not depend on
# the thread count).
CASES = {
    "default": [],
    "default-512": ["--width", "512", "--height", "512"],
    "high-512": ["--width", "512", "--height", "512", "--eye", "0,9,3", "--target", "0,0.5,0"],
    "low-512": ["--width", "512", "--height", "512", "--eye", "6,1.5,2", "--target", "0,1,0"],
    "close-512": ["--width", "512", "--height", "512", "--eye", "2,2.5,3", "--target", "0,0.8,0", "--fovy", "0.7"],
    "wide-512": ["--width", "512", "--height", "512", "--eye", "5,5,8", "--target", "0,0.5,0", "--fovy", "1.2"],
    "motion-256": ["--width", "256", "--height", "256", "--prev-eye", "4.3,4,5.7"],
    "nort-256": ["--no-rt"],
    "threads4-256": ["--threads", "4"],
    "wide-1440x900": ["--width", "1440", "--height", "900"],
}
NO_GPU_CASE = ["--gpu", "--width", "64", "--height", "64"]
QUICK = ("default", "motion-256", "nort-256", "threads4-256")
MODELS_LIST = Path(__file__).resolve().parents[1] / "evidence" / "m2-gltf-models.json"


def model_cases(models_dir):
    """One case per model: its first .gltf or .glb file, at 128 x 128."""
    cases = {}
    for m in json.loads(MODELS_LIST.read_text(encoding="utf-8"))["models"]:
        main = next(f["path"] for f in m["files"] if f["path"].endswith((".gltf", ".glb")))
        # --no-rt: the ray-traced AO reference uses a linear accelerator (raw/renderer/accel.hpp),
        # which a 109,000-triangle model makes impractically slow; the criterion's bounds (CPU
        # goldens, GPU against CPU) do not need it.
        cases["model-" + m["name"]] = ["--model", str(Path(models_dir) / main), "--width", "128", "--height", "128", "--no-rt"]
    return cases
TIMING = re.compile(rb'"timing_ms":\{[^}]*\}')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_hash(path):
    data = path.read_bytes()
    if path.name == "gpu_certificate.json":
        return digest(TIMING.sub(b'"timing_ms":"<normalized>"', data))
    if path.name == "gpu_receipt.json":
        cert = path.parent / "gpu_certificate.json"
        receipt = json.loads(data)
        if cert.exists() and "gpu_certificate.json" in receipt.get("outputs", {}):
            receipt["outputs"]["gpu_certificate.json"] = file_hash(cert)
        receipt.pop("receipt_sha256", None)
        return digest(json.dumps(receipt, sort_keys=True, separators=(",", ":")).encode())
    return digest(data)


# A .mjs CLI is the WebAssembly build, run under Node by wasm/run-node.mjs.
NODE_RUNNER = Path(__file__).resolve().parents[1] / "wasm" / "run-node.mjs"


def command(cli, out, args):
    if cli.endswith(".mjs"):
        return ["node", str(NODE_RUNNER), cli, str(out)] + args
    return [cli, "--out", str(out)] + args


def run(cli, manifest, work, gpu, quick, no_gpu_build, models=None):
    work.mkdir(parents=True, exist_ok=True)
    cases = model_cases(models) if models else CASES
    names = [n for n in cases if not quick or n in QUICK or models]
    if no_gpu_build:
        names.append("no-gpu-64")
    out, ok = {}, True
    for name in names:
        variants = [(name, cases.get(name, NO_GPU_CASE))]
        if gpu:
            variants = [(f"{name}.gpu", ["--gpu"] + cases[name])]
        for key, args in variants:
            d = work / key
            proc = subprocess.run(command(cli, d, args), capture_output=True, text=True)
            files = {str(p.relative_to(d)).replace("\\", "/"): file_hash(p)
                     for p in sorted(d.rglob("*")) if p.is_file()}
            out[key] = {"args": args, "exit": proc.returncode, "files": files}
            ok &= proc.returncode == (4 if name == "no-gpu-64" else 0) or (gpu and proc.returncode == 4)
            print(f"{key}: exit {proc.returncode}, {len(files)} files", flush=True)
    version = subprocess.run(command(cli, work / "version", ["--version"]), capture_output=True, text=True).stdout.strip()
    manifest.write_text(json.dumps({"cli_version": version, "gpu": gpu, "cases": out}, indent=1, sort_keys=True) + "\n")
    return 0 if ok else 1


def compare(a_path, b_path, skip=()):
    a, b = (json.loads(Path(p).read_text())["cases"] for p in (a_path, b_path))
    bad = 0
    for key in sorted(set(a) | set(b)):
        if key not in a or key not in b:
            print(f"CASE MISSING  {key}")
            bad += 1
            continue
        if a[key]["exit"] != b[key]["exit"]:
            print(f"EXIT DIFFERS  {key}: {a[key]['exit']} vs {b[key]['exit']}")
            bad += 1
        fa, fb = a[key]["files"], b[key]["files"]
        for f in sorted(set(fa) | set(fb)):
            if f.split("/")[-1] in skip:
                continue
            if fa.get(f) != fb.get(f):
                print(f"DIFFERS       {key}/{f}")
                bad += 1
    total = sum(1 for c in a.values() for f in c["files"] if f.split("/")[-1] not in skip)
    print(f"compared {len(a)} cases, {total} files" + (f" (left out: {', '.join(skip)})" if skip else "") + ": " + ("all identical" if bad == 0 else f"{bad} difference(s)"))
    return 0 if bad == 0 else 3


def main(argv):
    if len(argv) >= 5 and argv[1] == "run":
        models = argv[argv.index("--models") + 1] if "--models" in argv else None
        return run(str(Path(argv[2]).resolve()), Path(argv[3]), Path(argv[4]), "--gpu" in argv, "--quick" in argv,
                   "--no-gpu-build" in argv, models)
    if len(argv) >= 4 and argv[1] == "compare":
        skip = [argv[i + 1] for i in range(4, len(argv) - 1) if argv[i] == "--except"]
        return compare(argv[2], argv[3], tuple(skip))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
