#!/usr/bin/env python3
"""Gate for raster lighting model 2 (evidence/m3-lighting-v2-bounds.json). Standard library only.

  lighting_v2_gate.py DIR [--out SUMMARY.json]

DIR holds one adapter's run (.github/workflows/lighting-v2.yml): hero/, hall/ (--gpu renders at
512 x 320 under model 2, each with gpu_certificate.json) and pt-*.json from raw_native_cli pt-error.
Checks l3 to l8; l1 and l2 run in the main CI (the model 1 oracle and the goldens). l4 is recorded
with its bound's verdict, not gated (addendum A). Exit 0 when the gated checks hold, 1 otherwise.
"""
import json
import sys
from pathlib import Path


def load(p):
    try:
        with open(p, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        return {"_error": str(e)}


def cert(path):
    c = load(path)
    frame = next((x for x in c.get("channels", []) if x.get("name") == "frame"), {})
    ok = c.get("verdict") == "verified" and c.get("params", {}).get("rt") is True and frame.get("pass") is True
    return ok, {"verdict": c.get("verdict"), "identity": c.get("identity"), "frame_rmse": frame.get("rmse"),
                "frame_bound": frame.get("bound"), "adapter": c.get("adapter", {}).get("description")}


def main(a):
    if not a:
        print(__doc__)
        return 2
    d = Path(a[0])
    out = a[a.index("--out") + 1] if "--out" in a[:-1] else None
    pt = {k: load(d / f"pt-{k}.json") for k in ("hero", "hero-direct", "hall", "hall-direct", "control", "hall-nosun-direct")}
    m = lambda k: {x: pt[k].get(x) for x in ("relative_rmse", "bias", "noise_floor", "relative_rmse_8x8_blocks")}
    r = {
        "l3_verified_hero": cert(d / "hero/gpu_certificate.json"),
        "l3_verified_hall": cert(d / "hall/gpu_certificate.json"),
        "l4_hall_direct": (pt["hall-direct"].get("relative_rmse", 9) <= 0.2, m("hall-direct")),
        "l5_hall_full": (pt["hall"].get("relative_rmse", 9) <= 0.6, m("hall")),
        "l6_hero": (pt["hero"].get("relative_rmse", 9) <= 0.1, {"full": m("hero"), "direct": m("hero-direct")}),
        "l7_control": (pt["control"].get("relative_rmse", 9) <= 0.02 + 3 * pt["control"].get("noise_floor", 0), m("control")),
        "l8_false_control": (pt["hall-nosun-direct"].get("relative_rmse", 0) > 0.2, m("hall-nosun-direct")),
    }
    recorded = ("l4_hall_direct",)   # addendum A: a recorded miss, not gated
    summary = {k: {"holds": bool(v[0]), "gated": k not in recorded, **v[1]} for k, v in r.items()}
    for k, v in summary.items():
        print(f"  {'holds' if v['holds'] else 'MISSES'}{'' if v['gated'] else ' (recorded)'}: {k} {json.dumps({x: y for x, y in v.items() if x not in ('holds', 'gated')})}")
    if out:
        Path(out).write_text(json.dumps(summary, indent=1), encoding="utf-8")
    ok = all(v["holds"] for v in summary.values() if v["gated"])
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
