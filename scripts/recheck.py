#!/usr/bin/env python3
"""Independent level-1 recheck of a raw-native render directory (stdlib only).

Reads certificate.json (raw-cert/2), re-hashes every listed output, recomputes
the AO reconcile from ao_rt.pfm, ao_ss.pfm and mask.pgm with the same float32
arithmetic the renderer uses, and compares with the recorded exact values.
This shares no code with the C++ verifier, so a bug in one shows up as a
disagreement with the other.

Usage: recheck.py <dir> [--json]      exit 0 match, 3 mismatch, 2 missing
"""
import hashlib, json, math, struct, sys
from pathlib import Path


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def read_header(data, magic):
    parts, pos = [], 0
    for _ in range(3):
        end = data.index(b"\n", pos)
        parts.append(data[pos:end].decode("ascii"))
        pos = end + 1
    if parts[0] != magic:
        raise ValueError(f"expected {magic}, got {parts[0]}")
    w, h = (int(v) for v in parts[1].split())
    return w, h, parts[2], data[pos:]


def read_pfm1(path):
    w, h, scale, body = read_header(path.read_bytes(), "Pf")
    if scale != "-1.0" or len(body) != w * h * 4:
        raise ValueError(f"{path.name}: unexpected scale or size")
    flat = struct.unpack(f"<{w*h}f", body)
    rows = [flat[r * w:(r + 1) * w] for r in range(h)]
    return w, h, rows[::-1]                      # PFM rows are bottom-to-top


def read_mask(path):
    w, h, maxv, body = read_header(path.read_bytes(), "P5")
    if maxv != "255" or len(body) != w * h:
        raise ValueError(f"{path.name}: unexpected maxval or size")
    return w, h, [body[r * w:(r + 1) * w] for r in range(h)]


def reconcile(ss, rt, mask, w, h):
    total, n, max_err = 0.0, 0, 0.0
    for y in range(h):
        for x in range(w):
            if not mask[y][x]:
                continue
            e = abs(f32(ss[y][x] - rt[y][x]))   # float subtraction, correctly rounded
            max_err = max(max_err, e)
            total += e * e                       # double accumulation, row-major order
            n += 1
    rmse = f32(math.sqrt(total / n)) if n else 0.0
    return n, rmse, max_err


def recheck(directory):
    d = Path(directory)
    cert = json.loads((d / "certificate.json").read_text())
    if cert.get("schema") != "raw-cert/2":
        return 2, ["MISSING  certificate is not raw-cert/2"], None
    lines, bad = [], 0
    for name, digest in sorted(cert["outputs"].items()):
        p = d / name
        if not p.exists():
            return 2, lines + [f"MISSING  {name}"], None
        ok = hashlib.sha256(p.read_bytes()).hexdigest() == digest
        bad += not ok
        lines.append(("MATCH    " if ok else "MISMATCH ") + f"sha256 {name}")
    ex = cert["exact"]
    result = None
    if "ao_rt.pfm" in cert["outputs"]:
        w, h, rt = read_pfm1(d / "ao_rt.pfm")
        _, _, ss = read_pfm1(d / "ao_ss.pfm")
        _, _, mask = read_mask(d / "mask.pgm")
        n, rmse, max_err = reconcile(ss, rt, mask, w, h)
        tol = f32(ex["tolerance"])
        verdict = "unverifiable" if n == 0 else ("verified" if rmse <= tol else "refuted")
        checks = [("pixels", n == ex["pixels"]), ("rmse", rmse == f32(ex["rmse"])),
                  ("maxError", max_err == f32(ex["maxError"])), ("verdict", verdict == cert["verdict"])]
        for what, ok in checks:
            bad += not ok
            lines.append(("MATCH    " if ok else "MISMATCH ") + what)
        result = {"pixels": n, "rmse": rmse, "maxError": max_err, "verdict": verdict}
    else:
        ok = cert["verdict"] == "unverifiable"
        bad += not ok
        lines.append(("MATCH    " if ok else "MISMATCH ") + "verdict without reference")
    lines.append("recheck: all checks match" if bad == 0 else f"recheck: {bad} mismatch(es)")
    return (0 if bad == 0 else 3), lines, result


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    code, lines, result = recheck(sys.argv[1])
    print(json.dumps({"exit": code, "result": result}) if "--json" in sys.argv else "\n".join(lines))
    sys.exit(code)
