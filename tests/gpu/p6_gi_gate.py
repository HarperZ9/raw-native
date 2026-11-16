#!/usr/bin/env python3
"""The gate of evidence/p6-radiance-cascades-bounds.json on one adapter (stdlib only).

Reads DIR/<case>/gi.json written by `raw_native_cli gi --gpu` and checks:
  g2   control-sphere: I is 0 on the CPU and the GPU, and model 3 is model 2 to the bit;
  g5   raw-hall-nave, leak-room and flooded-arcade (addendum A, b3): the GPU's slot tables equal the
       CPU's, I within relative RMSE 2e-3 and the model 3 frame within 1e-3 of the CPU reference;
  g5c  the nomerge control on leak-room: I must miss g5's bound.
Writes a summary JSON with --out. Exit 0 when every check holds, 1 otherwise.
  p6_gi_gate.py DIR [--out FILE]
"""
import json
import sys
from pathlib import Path

I_BOUND, FRAME_BOUND = 2e-3, 1e-3


def load(d, case):
    return json.loads((Path(d) / case / "gi.json").read_text())


def gate(d):
    res = {}
    s = load(d, "sphere")
    res["g2_zero_on_convex"] = (s["cpu"]["indirect_max"] == 0 and s["gpu"]["indirect_max"] == 0
                                and s["cpu"]["model3_equals_model2"])
    for case in ("hall", "leak", "arcade"):
        g = load(d, case)["gpu"]
        res[f"g5_{case}"] = {"pass": bool(g["slots_match"] and g["indirect_rel_rmse"] <= I_BOUND
                                          and g["frame_rel_rmse"] <= FRAME_BOUND),
                             "indirect_rel_rmse": g["indirect_rel_rmse"], "frame_rel_rmse": g["frame_rel_rmse"],
                             "slots_match": g["slots_match"], "adapter": g["adapter"]}
    c = load(d, "control")["gpu"]
    res["g5c_control"] = {"pass": bool(c["control"] and c["indirect_rel_rmse"] > I_BOUND),
                          "indirect_rel_rmse": c["indirect_rel_rmse"]}
    ok = res["g2_zero_on_convex"] and all(v["pass"] for k, v in res.items() if isinstance(v, dict))
    return ok, res


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    ok, res = gate(argv[1])
    text = json.dumps({"pass": ok, "checks": res}, indent=1)
    if "--out" in argv:
        Path(argv[argv.index("--out") + 1]).write_text(text + "\n")
    print(text)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
