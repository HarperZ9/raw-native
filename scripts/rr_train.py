#!/usr/bin/env python3
"""HW H2 training (evidence/rr-bounds.json and amendments): our recurrent kernel-predicting U-Net
(rr_model.py) on the training split, model selection on the validation split only.

Fixed before the first run: AdamW, learning rate 1e-3 with cosine decay to 1e-5, weight decay
1e-4, batch 4 sequences of 6 frames, 128 x 128 crops, 6,000 steps, seed 1. Loss on frames 1 to 5:
L1 of log1p(output) against log1p(reference), plus 0.25 times the L1 of the difference between the
output's and the reference's frame-to-frame change after warping (history-valid pixels only).
A checkpoint every 1,000 steps (each well under 10 minutes on the RTX 4090) holds the model, the
optimiser and the step; --resume continues from it. The checkpoint with the lowest validation loss
is kept as best.pt. --shuffle-targets trains the control that must fail: every frame's target is
another training frame's, by a fixed permutation.

Usage: rr_train.py ROOT OUTDIR [--steps N] [--minutes M] [--resume] [--shuffle-targets] [--cpu]
"""
import argparse
import json
import math
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_data  # noqa: E402
from rr_model import RrNet, step, warp  # noqa: E402

B, T, CROP, LR, LR_MIN, WD, SEED, TEMP_W = 4, 6, 128, 1e-3, 1e-5, 1e-4, 1, 0.25


def load_split(root, split, max_frames=None):
    seqs = []
    for d in rr_data.sequences(root, split):
        n = rr_data.frame_count(d) if max_frames is None else min(max_frames, rr_data.frame_count(d))
        xs = [rr_data.load_frame(d, k) for k in range(n)]
        x = torch.from_numpy(np.stack([f[1] for f in xs])).permute(0, 3, 1, 2).contiguous()
        r = torch.from_numpy(np.stack([f[2][..., 0:3] for f in xs])).permute(0, 3, 1, 2).contiguous()
        seqs.append({"name": d.name, "x": x.half(), "r": r.half()})
    return seqs


def lossfn(out, ref, prev_out, prev_ref, pos, valid):
    l = F.l1_loss(torch.log1p(out), torch.log1p(ref))
    if prev_out is None:
        return l
    do = torch.log1p(out) - torch.log1p(warp(prev_out, pos).clamp_min(0))
    dr = torch.log1p(ref) - torch.log1p(warp(prev_ref, pos).clamp_min(0))
    return l + TEMP_W * ((do - dr).abs() * valid).mean()


def batch(seqs, rng, dev, perm):
    xs, rs = [], []
    for _ in range(B):
        s = seqs[rng.integers(len(seqs))]
        n, _, h, w = s["x"].shape
        t0 = rng.integers(0, n - T + 1)
        y0, x0 = rng.integers(0, (h - CROP) // 8 + 1) * 8, rng.integers(0, (w - CROP) // 8 + 1) * 8
        x = s["x"][t0:t0 + T, :, y0:y0 + CROP, x0:x0 + CROP].float().clone()
        x[:, 12] -= float(x0)
        x[:, 13] -= float(y0)
        r = s["r"][t0:t0 + T, :, y0:y0 + CROP, x0:x0 + CROP].float()
        if perm is not None:
            r = torch.stack([perm(s, t0 + t, y0, x0) for t in range(T)])
        xs.append(x), rs.append(r)
    return torch.stack(xs, 1).to(dev), torch.stack(rs, 1).to(dev)   # (T, B, C, H, W)


def run_sequence(net, x, r=None, prev_depth_of=lambda x: x[:, 9:10]):
    """Frames (T, B, C, H, W) through the network; returns the outputs and the mean loss over t >= 1."""
    prev, pdep, prev_out_rem, total, outs = None, None, None, 0.0, []
    for t in range(x.shape[0]):
        out, prev_new = step(net, x[t], prev, pdep)
        if r is not None and t >= 1:
            h, w = x.shape[-2:]
            px, py = x[t][:, 12:13], x[t][:, 13:14]
            valid = ((px >= 0.5) & (px <= w - 0.5) & (py >= 0.5) & (py <= h - 0.5)).float()
            total = total + lossfn(out, r[t], prev_out_rem, r[t - 1], x[t][:, 12:14], valid)
        outs.append(out)
        prev, pdep, prev_out_rem = prev_new, prev_depth_of(x[t]), out
    return outs, total / max(1, x.shape[0] - 1)


@torch.no_grad()
def validate(net, val, dev):
    net.eval()
    losses = []
    for s in val:
        x = s["x"].float().unsqueeze(1).to(dev)
        r = s["r"].float().unsqueeze(1).to(dev)
        _, l = run_sequence(net, x, r)
        losses.append(float(l))
    net.train()
    return float(np.mean(losses))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("out")
    ap.add_argument("--steps", type=int, default=6000)
    ap.add_argument("--minutes", type=float, default=60.0)
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--shuffle-targets", action="store_true")
    ap.add_argument("--cpu", action="store_true")
    ap.add_argument("--val-frames", type=int, default=32)
    a = ap.parse_args()
    dev = torch.device("cpu" if a.cpu else "cuda")
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    torch.manual_seed(SEED)
    train = load_split(a.root, "train")
    val = load_split(a.root, "val", a.val_frames)
    perm = None
    if a.shuffle_targets:
        flat = [(i, t) for i, s in enumerate(train) for t in range(s["r"].shape[0])]
        order = np.random.default_rng(SEED + 99).permutation(len(flat))
        table = {flat[j]: flat[order[j]] for j in range(len(flat))}

        def perm(s, t, y0, x0):
            i = next(k for k, q in enumerate(train) if q is s)
            si, st = table[(i, t)]
            q = train[si]["r"][st]
            return q[:, y0:y0 + CROP, x0:x0 + CROP].float()
    net = RrNet().to(dev)
    opt = torch.optim.AdamW(net.parameters(), lr=LR, weight_decay=WD)
    start, best, log = 0, math.inf, []
    ck = out / "last.pt"
    if a.resume and ck.exists():
        st = torch.load(ck, map_location=dev)
        net.load_state_dict(st["model"]), opt.load_state_dict(st["opt"])
        start, best, log = st["step"], st["best"], st["log"]
    rng = np.random.default_rng(SEED + start)
    t_begin = time.time()
    for it in range(start, a.steps):
        for g in opt.param_groups:
            g["lr"] = LR_MIN + 0.5 * (LR - LR_MIN) * (1 + math.cos(math.pi * it / a.steps))
        x, r = batch(train, rng, dev, perm)
        _, loss = run_sequence(net, x, r)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
        opt.step()
        done = it + 1
        timeout = (time.time() - t_begin) / 60 > a.minutes
        if done % 1000 == 0 or done == a.steps or timeout:
            vl = validate(net, val, dev)
            log.append({"step": done, "train_loss": float(loss), "val_loss": vl, "minutes": (time.time() - t_begin) / 60})
            print(json.dumps(log[-1]), flush=True)
            state = {"model": net.state_dict(), "opt": opt.state_dict(), "step": done, "best": min(best, vl), "log": log}
            torch.save(state, ck)
            if vl < best:
                best = vl
                torch.save({"model": net.state_dict(), "step": done, "val_loss": vl}, out / "best.pt")
        if timeout:
            break
    meta = {"torch": torch.__version__, "cuda": torch.version.cuda, "device": str(dev),
            "gpu": torch.cuda.get_device_name(0) if dev.type == "cuda" else "", "shuffle_targets": a.shuffle_targets,
            "train_sequences": [s["name"] for s in train], "val_sequences": [s["name"] for s in val],
            "hyper": {"B": B, "T": T, "crop": CROP, "lr": LR, "lr_min": LR_MIN, "wd": WD, "seed": SEED, "steps": a.steps,
                      "temporal_weight": TEMP_W}, "log": log, "best_val_loss": best,
            "parameters": sum(p.numel() for p in net.parameters())}
    (out / "train.json").write_text(json.dumps(meta, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
