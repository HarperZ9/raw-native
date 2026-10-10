#!/usr/bin/env python3
"""CI gate for HW H1.3 on WARP (evidence/hw-h1-3-bounds.json), stdlib only.

Gates: the visibility and depth targets bit-equal on every view, culling exercised on the close
cameras, and the forced-off run taking the vertex path. The flipped-cone control is a recorded
miss on one view (evidence/hw-h1-3-runs.json); it is printed per view and never reported as met
where it is not.
Usage: hw_mesh_gate.py MAIN.json FORCED_OFF.json
"""
import json
import sys


def main():
    run = json.load(open(sys.argv[1], encoding="utf-8"))
    off = json.load(open(sys.argv[2], encoding="utf-8"))
    ok = run["culling_exercised_on_close_cameras"]
    for s in run["scenes"]:
        for v in s["views"]:
            same = v["id_differences"] == 0 and v["depth_differences"] == 0
            ctl = v["control_id_differences"]
            print(f'{s["scene"]} {v["camera"]}: identity {"met" if same else "MISSED"}; kept {v["kept"]} of {v["meshlets"]}; '
                  f'control differs on {ctl} pixels {"(fails, as required)" if ctl else "(does NOT fail: recorded miss)"}')
            ok = ok and same
    fb = all(v["kept"] is None and v["id_differences"] is None for s in off["scenes"] for v in s["views"])
    print(f'forced off: {"vertex path only" if fb else "mesh path still ran"}')
    return 0 if ok and fb else 1


if __name__ == "__main__":
    sys.exit(main())
