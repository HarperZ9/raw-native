#!/usr/bin/env python3
"""Gate for N2 native presentation (evidence/n2-presentation-bounds.json). Standard library only.

  present_gate.py same A.json B.json [C.json ...]   every frame list identical (index, size, hash)
  present_gate.py control REF.json CONTROL.json     the control must differ on at least 99% of frames
  present_gate.py lifecycle PRESENTED.json OFFSCREEN.json [--no-minimise]
                                                    lists identical, one loss and one recovery, no size
                                                    mismatch, every scheduled event seen; --no-minimise
                                                    for a window system that never reports a minimise
                                                    (Wayland): the record must say the step was skipped
  present_gate.py hdr HDR.json                      within one 10-bit code, the control outside
  present_gate.py pacing PACING.json [--warp]       --warp (or --harness): the harness wrote finite
                                                    positive intervals (any software adapter);
                                                    otherwise the RTX bounds
Exit 0 when the check holds, 1 when it does not, 2 on usage.
"""
import json
import math
import sys


def load(p):
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def frames(d):
    return [(f["i"], f["w"], f["h"], f["sha256"]) for f in d["frames"]]


def same(paths):
    lists = [frames(load(p)) for p in paths]
    ref = lists[0]
    ok = len(ref) > 0 and all(f[3] for f in ref)
    for p, lst in zip(paths[1:], lists[1:]):
        diff = sum(1 for a, b in zip(ref, lst) if a != b) + abs(len(ref) - len(lst))
        print(f"{paths[0]} vs {p}: {len(ref)} and {len(lst)} frames, {diff} differ")
        ok = ok and diff == 0
    return ok


def control(ref_path, ctl_path):
    ref, ctl = frames(load(ref_path)), frames(load(ctl_path))
    n = min(len(ref), len(ctl))
    diff = sum(1 for a, b in zip(ref, ctl) if a != b)
    print(f"control: {diff} of {n} frames differ from {ref_path} (must be at least 99%)")
    return n > 0 and len(ref) == len(ctl) and diff >= math.ceil(0.99 * n)


def lifecycle(pres_path, off_path, no_minimise=False):
    p = load(pres_path)
    ok = same([pres_path, off_path])
    ev = " | ".join(p["events"])
    print(f"recoveries {p['recoveries']}, size mismatches {p['size_mismatches']}, events: {ev}")
    want = ["resized 256x144", "resized 320x180", "device removed", "device lost", "recovered"]
    want += ["minimise skipped"] if no_minimise else ["minimised", "restored"]
    seen = all(any(w in e for e in p["events"]) for w in want)
    if not seen:
        print("missing events:", [w for w in want if not any(w in e for e in p["events"])])
    return ok and p["lifecycle"] and p["recoveries"] == 1 and p["size_mismatches"] == 0 and seen and not p["error"]


def hdr(path):
    d = load(path)
    m, c = d["presented"], d["control_m1_2610_over_4096"]
    print(f"hdr: max {m['max_abs']:.6g} ({m['max_code_diff']} codes) over {m['over_bound']} of {m['channels']}; "
          f"control max {c['max_abs']:.6g} over {c['over_bound']}; colour space applied {d['colour_space_applied']}")
    return (d["pass"] and m["over_bound"] == 0 and m["channels"] == 3 * d["inputs"] and m["max_abs"] <= 1 / 1023
            and c["over_bound"] > 0)


def pacing(path, warp):
    d = load(path)
    iv = d["intervals_ms"]
    finite = all(isinstance(v, (int, float)) and math.isfinite(v) and v > 0 for v in iv)
    s = d["summary_ms"]
    print(f"pacing: {len(iv)} intervals, refresh {d['refresh_hz']} Hz, median {s['median']} ms, p99 {s['p99']} ms, "
          f"above 1.5T {s['above_1_5_period']}, missed refreshes {d['dxgi']['missed_refreshes']}")
    if warp:
        return finite and len(iv) == d["frames"] - 1
    T = d["period_ms"]
    if not T:
        print("no refresh period recorded; the bound cannot be read")
        return False
    checks = {
        "median within 1% of T": abs(s["median"] - T) <= 0.01 * T,
        "p99 at most 1.25 T": s["p99"] <= 1.25 * T,
        "at most 0.2% above 1.5 T": s["above_1_5_period"] <= 0.002 * len(iv),
        "intervals finite and positive": finite and len(iv) == d["frames"] - 1,
    }
    for k, v in checks.items():
        print(f"  {'holds' if v else 'FAILS'}: {k}")
    return all(checks.values())


def main(a):
    if len(a) < 2:
        print(__doc__)
        return 2
    cmd, rest = a[0], a[1:]
    if cmd == "same" and len(rest) >= 2:
        ok = same(rest)
    elif cmd == "control" and len(rest) == 2:
        ok = control(*rest)
    elif cmd == "lifecycle" and len(rest) in (2, 3):
        ok = lifecycle(rest[0], rest[1], "--no-minimise" in rest[2:])
    elif cmd == "hdr" and len(rest) == 1:
        ok = hdr(rest[0])
    elif cmd == "pacing" and rest:
        ok = pacing(rest[0], "--warp" in rest[1:] or "--harness" in rest[1:])
    else:
        print(__doc__)
        return 2
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
