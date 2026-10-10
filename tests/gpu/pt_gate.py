#!/usr/bin/env python3
"""Gate the path tracer's GPU checks (evidence/rt-r2-bounds.json, P1 to P4) from the JSON of
`raw_native_cli rt-pt-parity`.

P4's albedo and depth parts are a recorded open failure (evidence/rt-r2-runs.json, runs 8 to
10: the R1 reference's vertex snapping, not the path tracer, departs from float64 there).
This gate prints them and gates everything else: P1 within and its control outside, P2 within
and its control outside, P3, and P4's triangles and normals.

    python tests/gpu/pt_gate.py result.json
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
        c = r["check"]
        if c == "P1":
            want = not r["control_drop_cosine"]
            if r["within"] != want:
                bad.append(r)
        elif c == "P2":
            want = not r["case"].startswith("control")
            if r["within"] != want:
                bad.append(r)
        elif c == "P3":
            if not r["pass"]:
                bad.append(r)
        elif c == "P4":
            if r["triangle_differences"] != 0 or r["normal_over_1e-5"] != 0:
                bad.append(r)
            print(f"P4 {r['scene']}: open failure, albedo {r['albedo_differences']} pixels, depth {r['depth_over_1e-4']} pixels;"
                  f" float64 sides with the path tracer on {r['diagnosis_albedo_failures_matching_float64']['path_tracer']} albedo pixels")
    for r in bad:
        print("FAILED:", json.dumps(r))
    print("pt gate:", "pass" if not bad else f"{len(bad)} failing entries")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
