#!/usr/bin/env python3
"""Gate the ray marcher's GPU checks (evidence/rt-r3-bounds.json, M2 to M7) from the JSON of
`raw_native_cli sdf-parity`.

Two parts are recorded open failures (evidence/rt-r3-runs.json, runs 5 to 9): M2 on the
Mandelbulb (float32 precision in the fractal's iteration) and M4's 256-sample height-fog march
(midpoint-rule error on optically deep rays). This gate prints them and gates everything else,
controls included.

    python tests/gpu/sdf_gate.py result.json
"""
import json
import sys


def main(path):
    d = json.load(open(path, encoding="utf-8"))
    if d.get("error"):
        print("error:", d["error"])
        return 1
    bad = []
    for r in d["results"]:
        c, case = r["check"], r.get("case", "")
        control = case.startswith("control")
        if c == "M2" and r.get("scene") == "bulb" and not control:
            print(f"M2 bulb: open failure, t within on {r['t_within']} of {r['both_hit']} hits, normals {r['normal_within']}")
            continue
        if c == "M4":
            ok = r["homogeneous_worst_rel"] <= 1e-5 and r["height_closed_worst_rel"] <= 1e-4 and r["control_march_4_worst_rel"] > 1e-3
            print(f"M4: closed forms {'within' if ok else 'OUTSIDE'}; 256-sample march open failure at {r['height_march_256_worst_rel']:.3e}")
            if not ok:
                bad.append(r)
            continue
        if c == "M5" and control:
            if not r["control_fails"]:
                bad.append(r)
            continue
        if c == "M7":
            if not r["identical"]:
                bad.append(r)
            continue
        if r["within"] == control:
            bad.append(r)
    for r in bad:
        print("FAILED:", json.dumps(r))
    print("sdf gate:", "pass" if not bad else f"{len(bad)} failing entries")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
