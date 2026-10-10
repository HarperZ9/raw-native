"""Gate a shadow-parity JSON (raw_native_cli shadow-parity) on the parts the backends pass.

    python tests/gpu/shadow_gate.py RESULT.json [--min-scenes N]

The shadow-map identity has recorded failures (evidence/m3-shadows-runs.json): a WARP depth
value on a sub-texel triangle, texels where the cleared far value and a surface tie, and
SwiftShader's 4-bit edge precision. This gate requires, for every scene, the lookups within
1e-3 on all but 0.1% of points, contact shadows differing on at most 0.1% of pixels, shadow
map coverage, and all three controls failing. It prints the map identity per scene and never
counts it as passed. Exit 0 when the gated parts hold, 1 otherwise.
"""
import json
import sys


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    d = json.load(open(argv[0], encoding="utf-8"))
    need = int(argv[argv.index("--min-scenes") + 1]) if "--min-scenes" in argv else 1
    ok = not d["error"] and len(d["scenes"]) >= need and all(d["controls_fail"].values())
    for s in d["scenes"]:
        pts, px = s["points"], s["contact_pixels"]
        covered = sum(m["same_triangle_texels"] for m in s["maps"])
        within = pts > 0 and all(v <= 1e-3 * pts for v in s["outside_1e-3"].values())
        contact = px > 0 and s["contact_diffs"] <= 1e-3 * px
        maps = all(m["differing_texels"] == m["explained"] and m["worst_depth"] <= 1e-5 for m in s["maps"])
        ok = ok and within and contact and covered > 0
        print(f"{s['scene']}: lookups {'ok' if within else 'FAIL'}, contact {'ok' if contact else 'FAIL'}, "
              f"map identity {'pass' if maps else 'fail (recorded, not gated)'}: "
              + json.dumps([[m["differing_texels"], m["explained"], m["depth_ties"], m["worst_depth"]] for m in s["maps"]]))
    print("controls fail:", d["controls_fail"], "gate:", "pass" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
