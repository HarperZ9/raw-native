#!/usr/bin/env python3
"""HW H2 conditioning check (evidence/rr-bounds-amendment-3.json): the PyTorch forward pass in
float32 against float64 over whole sequences, max absolute difference in linear radiance.

Usage: rr_condition.py MODEL.pt SEQ_DIR [SEQ_DIR ...] [--frames N]
"""
import argparse
import os

os.environ.setdefault("FOR_DISABLE_CONSOLE_CTRL_HANDLER", "1")
import json
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_data  # noqa: E402
from rr_model import RrNet, step  # noqa: E402


@torch.no_grad()
def gap(ck, seq, frames):
    nets = {}
    for dt in (torch.float32, torch.float64):
        net = RrNet(ck.get("eps", 1e-3), ck.get("version", 1))
        net.load_state_dict(ck["model"])
        nets[dt] = net.to(dt).eval()
    state = {dt: (None, None) for dt in nets}
    worst = []
    n = min(frames, rr_data.frame_count(seq)) if frames > 0 else rr_data.frame_count(seq)
    for k in range(n):
        x = torch.from_numpy(rr_data.load_frame(seq, k)[1]).permute(2, 0, 1).unsqueeze(0)
        outs = {}
        for dt, net in nets.items():
            xd = x.to(dt)
            prev, pd = state[dt]
            out, prev = step(net, xd, prev, pd)
            state[dt] = (prev, xd[:, 9:10])
            outs[dt] = out.double()
        worst.append(float((outs[torch.float32] - outs[torch.float64]).abs().max()))
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("seqs", nargs="+")
    ap.add_argument("--frames", type=int, default=0)
    a = ap.parse_args()
    torch.set_num_threads(4)
    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    res = {Path(s).name: gap(ck, s, a.frames) for s in a.seqs}
    print(json.dumps({"model": a.model, "eps": ck.get("eps", 1e-3), "max_gap": max(max(v) for v in res.values()),
                      "per_sequence_max": {k: max(v) for k, v in res.items()}, "per_frame": res}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
