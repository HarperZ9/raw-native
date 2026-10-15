#!/usr/bin/env python3
"""HW H2 diagnosis, part 2: brightness bias and detail of a model against SVGF and the reference.

Per method, over every frame, on the tone-mapped frames: the mean luminance ratio to the reference (bias); the error
split into a low band (8 x 8 box blur) and the high band (the rest), as mean squared error; and the gradient energy
ratio to the reference (below 1: smoother than the reference, above 1: noisier).

Usage: rr_diag_freq.py MODEL.pt SEQ_DIR [--svgf-cache DIR] --out OUT.json
"""
import argparse
import os

os.environ.setdefault("FOR_DISABLE_CONSOLE_CTRL_HANDLER", "1")
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_data  # noqa: E402
import rr_eval  # noqa: E402


def box8(img):
    c = np.cumsum(np.cumsum(np.pad(img, ((4, 4), (4, 4), (0, 0)), mode="edge"), 0), 1)
    c = np.pad(c, ((1, 0), (1, 0), (0, 0)))
    h, w = img.shape[:2]
    return (c[8:8 + h, 8:8 + w] - c[0:h, 8:8 + w] - c[8:8 + h, 0:w] + c[0:h, 0:w]) / 64.0


def grad_energy(img):
    return float(np.abs(np.diff(img, axis=0)).mean() + np.abs(np.diff(img, axis=1)).mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("seq")
    ap.add_argument("--svgf-cache", default="")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    metas, x, r, tri = rr_data.load_sequence(a.seq)
    ref = r[..., 0:3]
    n, h, w = ref.shape[:3]
    outs = {"ours": rr_eval.run_net(a.model, x), "input": x[..., 0:3]}
    if a.svgf_cache and (Path(a.svgf_cache) / "f000.f32").exists():
        outs["svgf"] = np.stack([np.fromfile(Path(a.svgf_cache) / f"f{t:03d}.f32", np.float32).reshape(h, w, 3) for t in range(n)])
    else:
        outs["svgf"] = rr_eval.run_svgf(x)
    tr = rr_eval.tonemap(ref)
    res = {"sequence": Path(a.seq).name, "model": a.model, "methods": {}}
    for k, o in outs.items():
        to = rr_eval.tonemap(o)
        lo_e = hi_e = ratio = ge = 0.0
        for t in range(n):
            e = to[t] - tr[t]
            lo = box8(e)
            lo_e += float((lo ** 2).mean()); hi_e += float(((e - lo) ** 2).mean())
            ratio += float(rr_data.lum(o[t]).mean() / max(rr_data.lum(ref[t]).mean(), 1e-9))
            ge += grad_energy(to[t]) / max(grad_energy(tr[t]), 1e-9)
        res["methods"][k] = {"luminance_ratio": ratio / n, "mse_low_band": lo_e / n, "mse_high_band": hi_e / n, "gradient_energy_ratio": ge / n}
    Path(a.out).write_text(json.dumps(res, indent=1) + "\n")
    print(json.dumps(res, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
