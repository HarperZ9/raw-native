#!/usr/bin/env python3
"""HW H2 model selection by evidence/rr-bounds-amendment-5.json.

A candidate qualifies when its best validation loss is at least 10% below the identity model's (the 1 spp input
returned unchanged, scored by rr_train.lossfn on the same validation frames, both sequences, frames 0 to 31). The
saturated v1 must fail that check, or the selection is invalid. Among qualifying candidates, the lowest validation loss
wins. Usage: rr_select.py DATA_ROOT TRAIN_DIR NAME=CONTROL [NAME=CONTROL ...] --control-must-fail NAME --out FILE
"""
import argparse
import os

os.environ.setdefault("FOR_DISABLE_CONSOLE_CTRL_HANDLER", "1")
import json  # noqa: E402
import sys  # noqa: E402
from pathlib import Path  # noqa: E402

import numpy as np  # noqa: E402
import torch  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_train  # noqa: E402


@torch.no_grad()
def identity_loss(val):
    losses = []
    for s in val:
        x = s["x"].float().unsqueeze(1)
        r = s["r"].float().unsqueeze(1)
        total, prev = 0.0, None
        for t in range(x.shape[0]):
            out = x[t][:, 0:3]
            if t >= 1:
                h, w = x.shape[-2:]
                px, py = x[t][:, 12:13], x[t][:, 13:14]
                valid = ((px >= 0.5) & (px <= w - 0.5) & (py >= 0.5) & (py <= h - 0.5)).float()
                total = total + rr_train.lossfn(out, r[t], prev, r[t - 1], x[t][:, 12:14], valid, energy_w=0.0)
            prev = out
        losses.append(float(total / max(1, x.shape[0] - 1)))
    return float(np.mean(losses))


@torch.no_grad()
def model_loss(ck, val):
    """Amendment 7: a candidate's loss recomputed on the current validation split by its best checkpoint, with the same
    loss as the identity model (amendment 5's lossfn, no energy term)."""
    from rr_model import RrNet
    net = RrNet(ck.get("eps", 1e-3), ck.get("version", 1))
    net.load_state_dict(ck["model"])
    net.eval()
    losses = []
    for s in val:
        x = s["x"].float().unsqueeze(1)
        r = s["r"].float().unsqueeze(1)
        prev, pdep, prev_out, total = None, None, None, 0.0
        for t in range(x.shape[0]):
            out, prev = rr_train.step(net, x[t], prev, pdep)
            if t >= 1:
                h, w = x.shape[-2:]
                px, py = x[t][:, 12:13], x[t][:, 13:14]
                valid = ((px >= 0.5) & (px <= w - 0.5) & (py >= 0.5) & (py <= h - 0.5)).float()
                total = total + rr_train.lossfn(out, r[t], prev_out, r[t - 1], x[t][:, 12:14], valid, energy_w=0.0)
            pdep, prev_out = x[t][:, 9:10], out
        losses.append(float(total / max(1, x.shape[0] - 1)))
    return float(np.mean(losses))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("train")
    ap.add_argument("pairs", nargs="+")
    ap.add_argument("--control-must-fail", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--recompute", action="store_true", help="amendment 7: recompute every candidate's loss on the current validation split")
    a = ap.parse_args()
    torch.set_num_threads(int(os.environ.get("OMP_NUM_THREADS", "4")))
    val = rr_train.load_split(a.root, "val", 32)
    ident = identity_loss(val)
    cands = {}
    for pair in a.pairs:
        name, ctl = pair.split("=")
        ck = torch.load(f"{a.train}/{name}/best.pt", map_location="cpu", weights_only=False)
        vl = model_loss(ck, val) if a.recompute else float(ck["val_loss"])
        cands[name] = {"control": ctl, "val_loss": vl, "step": ck["step"], "eps": ck.get("eps", 1e-3),
                       "version": ck.get("version", 1), "qualifies": vl <= 0.9 * ident}
    ctl_ok = not cands[a.control_must_fail]["qualifies"]
    q = {n: c for n, c in cands.items() if c["qualifies"]}
    chosen = min(q, key=lambda n: q[n]["val_loss"]) if (q and ctl_ok) else None
    doc = {"rule": "evidence/rr-bounds-amendment-5.json" + (" with amendment 7's recomputation" if a.recompute else ""),
           "validation_sequences": [s["name"] for s in val], "identity_val_loss": ident, "threshold": 0.9 * ident,
           "candidates": cands, "control_must_fail": a.control_must_fail, "control_fails": ctl_ok,
           "valid": bool(ctl_ok and q), "chosen": chosen, "control": cands[chosen]["control"] if chosen else None}
    Path(a.out + ".tmp").write_text(json.dumps(doc, indent=1))
    os.replace(a.out + ".tmp", a.out)
    print(json.dumps(doc))
    return 0 if doc["valid"] else 1


if __name__ == "__main__":
    sys.exit(main())
