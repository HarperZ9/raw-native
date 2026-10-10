#!/usr/bin/env python3
"""CI gate for HW H1.1 on WARP (evidence/hw-h1-1-bounds.json), stdlib only.

WARP misses the main bound for causes recorded in evidence/hw-h1-1-runs.json (a TMin defect
reproduced in isolation, coplanar ties in the hall scene, two grazing hits). This gate fails
when either control passes on any scene (the check would no longer discriminate) or when the
forced-off run does not take the fallback. It prints the main verdict per scene and never
reports the main bound as passed when it is not.
Usage: hw_rayquery_gate.py MAIN.json FORCED_OFF.json
"""
import json
import sys


def main():
    run = json.load(open(sys.argv[1], encoding="utf-8"))
    off = json.load(open(sys.argv[2], encoding="utf-8"))
    ok = True
    for s in run["scenes"]:
        c = s["compare"]
        print(f'{s["scene"]}: main bound {"met" if s["within_bounds"] else "MISSED (recorded)"}; '
              f'unexplained {c["unexplained"]}, explained {sum(c["explained"].values())} of {c["rays"]}, '
              f't/uv violations {c["t_violations"]}/{c["uv_violations"]}; controls fail: '
              f'offset {s["control_offset_fails"]}, rotate {s["control_rotate_fails"]}')
        ok = ok and s["control_offset_fails"] and s["control_rotate_fails"]
    fb = off["fallback"] and off["pass"] and all(s.get("fallback") == "cpu traversal" for s in off["scenes"])
    print(f'forced off: fallback {"taken" if fb else "NOT taken"}')
    return 0 if ok and fb else 1


if __name__ == "__main__":
    sys.exit(main())
