#!/usr/bin/env python3
"""Gate for N3 criterion 5, Steam Deck class (evidence/n3-deck-class-bounds.json). Standard library only.

  deck_gate.py scene CERT.json --backend B --size WxH [--rt yes|no] [--cpu-equal OTHER.json] [--not-adapter VENDOR]
      a GPU certificate from `raw_native_cli --gpu`: verdict verified, written by backend B, at W x H;
      --cpu-equal: its CPU reference frame (frame_rgb8_sha256.cpu) equals the other certificate's;
      --rt: the certificate records the ray-traced AO reference as on (yes) or skipped (no);
      --not-adapter: the adapter's vendor is not VENDOR (case-insensitive), e.g. microsoft for WARP.
Exit 0 when the check holds, 1 when it does not, 2 on usage.
"""
import json
import sys


def load(p):
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def scene(cert_path, backend, size, cpu_equal=None, not_adapter=None, rt=None):
    try:
        c = load(cert_path)
    except (OSError, ValueError) as e:
        print(f"no certificate: {e}")
        return False
    w, h = (int(v) for v in size.lower().split("x"))
    p, a = c.get("params", {}), c.get("adapter", {})
    checks = {
        "verdict verified": c.get("verdict") == "verified",
        f"backend {backend}": p.get("backend") == backend,
        f"size {w}x{h}": (p.get("width"), p.get("height")) == (w, h),
    }
    if rt in ("yes", "no"):
        checks[f"rt {rt}"] = p.get("rt") is (rt == "yes")
    if not_adapter:
        checks[f"adapter vendor not {not_adapter}"] = str(a.get("vendor", "")).lower() != not_adapter.lower()
    if cpu_equal:
        mine = c.get("frame_rgb8_sha256", {}).get("cpu")
        theirs = load(cpu_equal).get("frame_rgb8_sha256", {}).get("cpu")
        print(f"cpu frame: {mine} here, {theirs} in {cpu_equal}")
        checks["CPU frame equal to the other build's"] = bool(mine) and mine == theirs
    print(f"verdict {c.get('verdict')}, identity {c.get('identity')}, adapter {a.get('description')} ({a.get('backend')}), "
          f"coverage {c.get('coverage', {}).get('mismatch_fraction')}")
    for ch in c.get("channels", []):
        if not ch.get("pass", True):
            print(f"  channel {ch.get('name')}: rmse {ch.get('rmse')}, max {ch.get('max_error')}, bound {ch.get('bound')}")
    for k, v in checks.items():
        print(f"  {'holds' if v else 'FAILS'}: {k}")
    return all(checks.values())


def main(a):
    if len(a) < 2 or a[0] != "scene":
        print(__doc__)
        return 2
    opts, pos, i = {}, [], 1
    while i < len(a):
        if a[i].startswith("--") and i + 1 < len(a):
            opts[a[i][2:]] = a[i + 1]
            i += 2
        else:
            pos.append(a[i])
            i += 1
    if len(pos) != 1 or "backend" not in opts or "x" not in opts.get("size", ""):
        print(__doc__)
        return 2
    ok = scene(pos[0], opts["backend"], opts["size"], opts.get("cpu-equal"), opts.get("not-adapter"), opts.get("rt"))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
