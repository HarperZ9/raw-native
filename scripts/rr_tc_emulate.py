#!/usr/bin/env python3
"""HW H2 tensor-core parity diagnosis (evidence/rr-tc-note-2026-10-15.json): v4's forward pass with the cooperative-
matrix path's fp16 roundings emulated in PyTorch on the CPU, against the fp32 reference.

Modes: F fp32; W the convolution weights rounded to fp16; A every convolution's input rounded to fp16; WA both; S the
split arithmetic of evidence/rr-tc-split-note-2026-10-15.json (each operand as fp16 hi plus fp16 lo, three products
hi x hi + hi x lo + lo x hi summed in fp32, the bias added once). The
bias stays fp32 and accumulation stays fp32, as on the tensor cores. --layer K rounds only layer K (per-layer ablation; K in
the order of rr_export.LAYERS); --fp32-layers keeps the listed layers in fp32 (a mixed-precision path).

Usage: rr_tc_emulate.py MODEL.pt SEQ_DIR --mode WA [--frames N] [--layer K] [--worst 100] --out OUT.json
"""
import argparse
import os

os.environ.setdefault("FOR_DISABLE_CONSOLE_CTRL_HANDLER", "1")
import json
import sys
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_data  # noqa: E402
from rr_export import LAYERS  # noqa: E402
from rr_model import RrNet, step  # noqa: E402


def half(t):
    return t.half().float()


OVER = [0]   # convolution inputs above fp16's largest finite value (65,504)


def split(t):
    hi = half(t)
    return hi, half(t - hi)


def split_forward(conv):
    whi, wlo = split(conv.weight.data)

    def fwd(x):
        xhi, xlo = split(x)
        f = lambda a, w, b: torch.nn.functional.conv2d(a, w, b, conv.stride, conv.padding)  # noqa: E731
        return f(xhi, whi, conv.bias) + f(xhi, wlo, None) + f(xlo, whi, None)
    return fwd


def count_over(m, inp):
    OVER[0] += int((inp[0].abs() > 65504).sum())


def build(ck, mode, layer, keep=()):
    net = RrNet(ck.get("eps", 1e-3), ck.get("version", 1))
    net.load_state_dict(ck["model"])
    net.eval()
    convs = dict(net.named_modules())
    for i, name in enumerate(LAYERS):
        conv = convs[name]
        if i in keep:   # --fp32-layers: these layers stay fp32 (a mixed-precision path)
            continue
        if mode == "S" and (layer is None or layer == i):
            conv.register_forward_pre_hook(count_over)
            conv.forward = split_forward(conv)
            continue
        if "W" in mode and (layer is None or layer == i):
            conv.weight.data = half(conv.weight.data)
        if "A" in mode and (layer is None or layer == i):
            conv.register_forward_pre_hook(lambda m, inp: (half(inp[0]),))
    return net


@torch.no_grad()
def run(net, seq, frames):
    outs, prev, pdep = [], None, None
    for k in range(frames):
        x = torch.from_numpy(rr_data.load_frame(seq, k)[1]).permute(2, 0, 1).unsqueeze(0).contiguous()
        out, prev = step(net, x, prev, pdep)
        pdep = x[:, 9:10]
        outs.append(out[0].permute(1, 2, 0).numpy())
    return np.stack(outs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("seq")
    ap.add_argument("--mode", required=True, choices=["W", "A", "WA", "S"])
    ap.add_argument("--frames", type=int, default=64)
    ap.add_argument("--layer", type=int, default=None)
    ap.add_argument("--worst", type=int, default=100)
    ap.add_argument("--fp32-layers", default="", help="comma-separated layer indices kept in fp32")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    torch.set_num_threads(int(os.environ.get("OMP_NUM_THREADS", "6")))
    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    n = min(a.frames, rr_data.frame_count(a.seq))
    ref = run(build(ck, "", None), a.seq, n)
    keep = tuple(int(v) for v in a.fp32_layers.split(",") if v != "")
    emu = run(build(ck, a.mode, a.layer, keep), a.seq, n)
    d = np.abs(emu.astype(np.float64) - ref.astype(np.float64)).max(-1)   # per pixel, worst channel
    h, w = d.shape[1:]
    order = np.argsort(d.ravel())[::-1][:a.worst]
    worst = [{"frame": int(i // (h * w)), "pixel": int(i % (h * w)), "x": int(i % w), "y": int((i // w) % h), "abs": float(d.ravel()[i])} for i in order]
    res = {"model": a.model, "sequence": Path(a.seq).name, "mode": a.mode, "layer": a.layer, "fp32_layers": list(keep), "frames": n,
           "max_abs": float(d.max()), "mean_abs": float(d.mean()), "p99_99": float(np.quantile(d, 0.9999)),
           "frames_over_1e-2": int((d.reshape(n, -1).max(1) > 1e-2).sum()), "pixels_over_1e-2": int((d > 1e-2).sum()),
           "inputs_over_65504": OVER[0] if a.mode == "S" else None,
           "worst": worst}
    Path(a.out).write_text(json.dumps(res, indent=1) + "\n")
    print(json.dumps({k: res[k] for k in res if k != "worst"} | {"worst_top3": worst[:3]}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
