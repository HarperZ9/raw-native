#!/usr/bin/env python3
"""Gate for M3 criterion 1 step 3 (evidence/m3-verified-scenes-bounds.json). Standard library only.

  m3_scenes_gate.py DIR [--out SUMMARY.json]

DIR holds one adapter's run (.github/workflows/m3-scenes.yml): hero/, hall/, control/ (each a --gpu
render with gpu_certificate.json), gallery/gallery.json, and pt-*.json from raw_native_cli pt-error.
Every check of the bounds file is evaluated and written to the summary. The exit code gates on
v1, v2, v3's RMSE and max_abs, p0, p1, p1b and p2. Recorded with its bound's verdict, not gated: p3
(raw-hall's path-traced error, addendum A). v3's max_abs was recorded only (addendum B) until its lavapipe
miss was traced to llvmpipe's acos and fixed (evidence/gold-highlight-acos-bounds.json, 2026-10-21); it is gated
again. Exit 0 when the gated checks hold, 1 otherwise.
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


def verified(cert, w, h):
    p = cert.get("params", {})
    ok = cert.get("verdict") == "verified" and p.get("rt") is True and (p.get("width"), p.get("height")) == (w, h)
    return ok, {"verdict": cert.get("verdict"), "identity": cert.get("identity"), "rt": p.get("rt"),
                "size": [p.get("width"), p.get("height")], "adapter": cert.get("adapter", {}).get("description"),
                "coverage_mismatch": cert.get("coverage", {}).get("mismatch_fraction"),
                "failing_channels": [c["name"] for c in cert.get("channels", []) if not c.get("pass", True)]}


def main(a):
    if not a:
        print(__doc__)
        return 2
    d = Path(a[0])
    out = a[a.index("--out") + 1] if "--out" in a[:-1] else None
    r = {}
    r["v1_hero"] = verified(load(d / "hero/gpu_certificate.json"), 512, 320)
    r["v2_hall"] = verified(load(d / "hall/gpu_certificate.json"), 512, 320)
    g = load(d / "gallery/gallery.json").get("gpu") or load(d / "gallery/gallery.json")
    rmse, mx = g.get("rmse_exposed"), g.get("max_abs_exposed")
    r["v3_gallery"] = (rmse is not None and rmse <= 5e-4), {"rmse_exposed": rmse}
    r["v3_gallery_max_abs"] = (mx is not None and mx <= 0.05), {"max_abs_exposed": mx}
    pt = {k: load(d / f"pt-{k}.json") for k in ("control", "control-nopi", "hero", "hall", "hero-direct", "hall-direct")}
    r["p0_alignment"] = all(pt[k].get("coverage_agreement", 0) >= 0.99 for k in ("control", "hero", "hall")), \
        {k: pt[k].get("coverage_agreement") for k in ("control", "hero", "hall")}
    c = pt["control"]
    r["p1_control"] = (c.get("relative_rmse", 9) <= 0.02 + 3 * c.get("noise_floor", 9) and abs(c.get("bias", 9)) <= 0.02), \
        {k: c.get(k) for k in ("relative_rmse", "bias", "noise_floor")}
    r["p1b_false_control"] = pt["control-nopi"].get("relative_rmse", 0) > 0.1, {"relative_rmse": pt["control-nopi"].get("relative_rmse")}
    r["p2_hero"] = pt["hero"].get("relative_rmse", 9) <= 0.6, {k: pt["hero"].get(k) for k in ("relative_rmse", "bias", "noise_floor")}
    r["p3_hall"] = pt["hall"].get("relative_rmse", 99) <= 2.0, {k: pt["hall"].get(k) for k in ("relative_rmse", "bias", "noise_floor")}
    r["direct_only_recorded"] = True, {k: {m: pt[k].get(m) for m in ("relative_rmse", "bias")} for k in ("hero-direct", "hall-direct")}
    gated = ("v1_hero", "v2_hall", "v3_gallery", "v3_gallery_max_abs", "p0_alignment", "p1_control", "p1b_false_control", "p2_hero")
    summary = {k: {"holds": bool(v[0]), "gated": k in gated, **v[1]} for k, v in r.items()}
    for k, v in summary.items():
        print(f"  {'holds' if v['holds'] else 'MISSES'}{'' if v['gated'] else ' (recorded)'}: {k} {json.dumps({x: y for x, y in v.items() if x not in ('holds', 'gated')})}")
    if out:
        Path(out).write_text(json.dumps(summary, indent=1), encoding="utf-8")
    ok = all(summary[k]["holds"] for k in gated)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
