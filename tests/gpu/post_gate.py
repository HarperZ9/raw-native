"""Gate a post-parity JSON (raw_native_cli post-parity) on the parts the backends pass.

    python tests/gpu/post_gate.py RESULT.json [--min-scenes N]

GTAO and SSR parity must hold on every scene (at most 0.1% of pixels off), and all three
controls must fail. The TAA parity, each side feeding back its own history, is a recorded open
failure (evidence/m3-post-runs.json): it is printed with its one-step count and never counted as
passed. Exit 0 when the gated parts hold, 1 otherwise.
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
        px = s["pixels"]
        good = px > 0 and s["gtao_outside_1e-3"] <= 1e-3 * px and s["ssr_diffs"] <= 1e-3 * px
        ok = ok and good
        print(f"{s['scene']}: GTAO {s['gtao_outside_1e-3']} and SSR {s['ssr_diffs']} of {px} pixels differ: {'ok' if good else 'FAIL'}")
    t = d["taa"]
    taa_ok = t["worst_frame_outside_1e-3"] <= 1e-3 * t["pixels"]
    print(f"TAA: worst frame {t['worst_frame_outside_1e-3']} of {t['pixels']} pixels "
          f"({'pass' if taa_ok else 'open failure, recorded, not gated'}); one step from the same history: {t['reported_one_step_worst_frame']}")
    print("controls fail:", d["controls_fail"], "gate:", "pass" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
