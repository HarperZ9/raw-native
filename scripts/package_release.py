#!/usr/bin/env python3
"""Pack release archives with fixed timestamps, sorted entries and fixed modes,
so the same binaries always give the same archive bytes. Writes SHA256SUMS.

Usage: package_release.py <version> <dist> --windows <exe> --linux <bin> --wasm <dir>
                          [--windows-d3d12 <exe>]
--windows-d3d12 adds raw-native-<version>-windows-x64-d3d12.zip: the CLI built
with RAW_NATIVE_GPU_D3D12=ON, which renders --gpu on D3D12. The plain Windows
archive stays the CPU-only, dependency-free build.
The wasm dir must hold raw-native.mjs and raw-native.wasm. raw-loader.mjs is
taken from wasm/ in this repo; raw-native-gpu.mjs and raw-native-gpu.wasm are
included when present. The wasm files are also published loose,
so a web page can fetch them directly and check each against SHA256SUMS."""
import argparse, gzip, hashlib, io, shutil, tarfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EPOCH = (1980, 1, 1, 0, 0, 0)
TEXT = (".md", ".mjs", ".txt", "LICENSE")


def read(src):
    """File bytes; text files with LF line endings whatever the checkout uses."""
    data = Path(src).read_bytes()
    return data.replace(b"\r\n", b"\n") if str(src).endswith(TEXT) else data


def zip_pack(dest, prefix, files):
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, src, mode in sorted(files):
            info = zipfile.ZipInfo(f"{prefix}/{name}", EPOCH)
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, read(src))


def tar_pack(dest, prefix, files):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as t:
        d = tarfile.TarInfo(prefix)
        d.type, d.mode, d.mtime = tarfile.DIRTYPE, 0o755, 0
        t.addfile(d)
        for name, src, mode in sorted(files):
            data = read(src)
            info = tarfile.TarInfo(f"{prefix}/{name}")
            info.size, info.mode, info.mtime = len(data), mode, 0
            t.addfile(info, io.BytesIO(data))
    with open(dest, "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", mtime=0, filename="") as g:
        g.write(raw.getvalue())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version")
    ap.add_argument("dist")
    ap.add_argument("--windows", required=True)
    ap.add_argument("--linux", required=True)
    ap.add_argument("--wasm", required=True)
    ap.add_argument("--windows-d3d12")
    a = ap.parse_args()
    v, dist = a.version, Path(a.dist)
    dist.mkdir(parents=True, exist_ok=True)
    # The CLI links superstack.hpp (MIT); its notice travels with every binary.
    docs = [("LICENSE", ROOT / "LICENSE", 0o644), ("README.md", ROOT / "README.md", 0o644),
            ("LICENSE-superstack.txt", ROOT / "third_party" / "superstack" / "LICENSE.txt", 0o644)]
    wasm_dir = Path(a.wasm)
    wasm_files = [("raw-native.mjs", wasm_dir / "raw-native.mjs", 0o644),
                  ("raw-native.wasm", wasm_dir / "raw-native.wasm", 0o644),
                  ("raw-loader.mjs", ROOT / "wasm" / "raw-loader.mjs", 0o644)]
    # From 0.4.0 the WebGPU build sits beside the CPU build when it was built
    # with the wasm-gpu preset (which writes both into one directory).
    for name in ("raw-native-gpu.mjs", "raw-native-gpu.wasm"):
        if (wasm_dir / name).exists():
            wasm_files.append((name, wasm_dir / name, 0o644))
    names = []
    p = f"raw-native-{v}-windows-x64"
    zip_pack(dist / f"{p}.zip", p, docs + [("raw_native_cli.exe", a.windows, 0o755)]); names.append(f"{p}.zip")
    if a.windows_d3d12:
        p = f"raw-native-{v}-windows-x64-d3d12"
        zip_pack(dist / f"{p}.zip", p, docs + [("raw_native_cli.exe", a.windows_d3d12, 0o755)]); names.append(f"{p}.zip")
    p = f"raw-native-{v}-linux-x64"
    tar_pack(dist / f"{p}.tar.gz", p, docs + [("raw_native_cli", a.linux, 0o755)]); names.append(f"{p}.tar.gz")
    p = f"raw-native-{v}-wasm"
    zip_pack(dist / f"{p}.zip", p, docs + wasm_files); names.append(f"{p}.zip")
    for name, src, _ in wasm_files:
        (dist / name).write_bytes(read(src)); names.append(name)
    lines = [f"{hashlib.sha256((dist / n).read_bytes()).hexdigest()}  {n}" for n in names]
    (dist / "SHA256SUMS").write_text("\n".join(lines) + "\n", newline="\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
