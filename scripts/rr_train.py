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

With --lock DIR the script takes the GPU lock itself (atomic mkdir, OWNER JSON with pid, token,
purpose and since) only after the data is loaded, trains for --session-minutes, checkpoints,
releases the lock by token, waits 30 s so another job can take the GPU, and repeats until done.

Usage: rr_train.py ROOT OUTDIR [--steps N] [--minutes M] [--resume] [--shuffle-targets] [--cpu]
                   [--lock DIR --session-minutes M]
"""
import argparse
import os

os.environ.setdefault("FOR_DISABLE_CONSOLE_CTRL_HANDLER", "1")   # before numpy and torch load the Intel Fortran runtime
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

# Optional shutdown guard (a long-job survival module outside the repo, named by RR_GUARD_DIR): on a session end or a
# console close it sets a flag; the loop checkpoints and exits with code 75 ("resume later").
GUARD = None
if os.environ.get("RR_GUARD_DIR"):
    sys.path.insert(0, os.environ["RR_GUARD_DIR"])
    import shutdown_guard as GUARD  # noqa: E402
EXIT_RESUME = 75
from rr_model import RrNet, step, warp  # noqa: E402

B, T, CROP, LR, LR_MIN, WD, SEED, TEMP_W = 4, 6, 128, 1e-3, 1e-5, 1e-4, 1, 0.25
# --energy-weight: weight of an energy term, |mean luminance of the output - of the reference| / (mean of the
# reference + 0.01) over each crop; 0 (the default) leaves the loss as it was.
ENERGY_W = 0.0


def load_split(root, split, max_frames=None):
    seqs = []
    for d in rr_data.sequences(root, split):
        n = rr_data.frame_count(d) if max_frames is None else min(max_frames, rr_data.frame_count(d))
        xs = [rr_data.load_frame(d, k) for k in range(n)]
        x = torch.from_numpy(np.stack([f[1] for f in xs])).permute(0, 3, 1, 2).contiguous()
        r = torch.from_numpy(np.stack([f[2][..., 0:3] for f in xs])).permute(0, 3, 1, 2).contiguous()
        seqs.append({"name": d.name, "x": x.half(), "r": r.half()})
    return seqs


def energy_term(out, ref):
    lum = lambda c: 0.2126 * c[:, 0] + 0.7152 * c[:, 1] + 0.0722 * c[:, 2]
    mo, mr = lum(out).flatten(1).mean(1), lum(ref).flatten(1).mean(1)
    return ((mo - mr).abs() / (mr + 0.01)).mean()


def lossfn(out, ref, prev_out, prev_ref, pos, valid, energy_w=None):
    l = F.l1_loss(torch.log1p(out), torch.log1p(ref))
    w = ENERGY_W if energy_w is None else energy_w
    if w > 0:
        l = l + w * energy_term(out, ref)
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


def train_session(a, net, opt, train, val, perm, dev, out, start, best, log, minutes):
    ck = out / "last.pt"
    rng = np.random.default_rng(SEED + start)
    t_begin = time.time()
    last_save = t_begin
    done = start
    for it in range(start, a.steps):
        for g in opt.param_groups:
            g["lr"] = LR_MIN + 0.5 * (a.lr - LR_MIN) * (1 + math.cos(math.pi * it / a.steps))
        x, r = batch(train, rng, dev, perm)
        _, loss = run_sequence(net, x, r)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
        opt.step()
        done = it + 1
        stop = GUARD is not None and GUARD.requested()
        timeout = (time.time() - t_begin) / 60 > minutes or stop
        if done % 1000 == 0 or done == a.steps:
            vl = validate(net, val, dev)
            log.append({"step": done, "train_loss": float(loss.detach()), "val_loss": vl, "minutes": (time.time() - t_begin) / 60})
            print(json.dumps(log[-1]), flush=True)
            if vl < best:
                best = vl
                torch.save({"model": net.state_dict(), "step": done, "val_loss": vl, "eps": net.eps, "version": net.version}, out / "best.pt")
        # A checkpoint at every validation, every 5 minutes and at the time limit, written atomically, so a
        # shutdown loses at most 5 minutes; --resume continues from it.
        if done % 1000 == 0 or done == a.steps or timeout or time.time() - last_save > 300:
            tmp = out / "last.pt.tmp"
            torch.save({"model": net.state_dict(), "opt": opt.state_dict(), "step": done, "best": best, "log": log, "eps": net.eps, "version": net.version}, tmp)
            tmp.replace(ck)
            (out / "progress.json").write_text(json.dumps({"step": done, "steps": a.steps, "best_val": best}))
            last_save = time.time()
        if timeout:
            if stop:
                GUARD.done()
                print(json.dumps({"shutdown_save": done, "reason": GUARD.reason()}), flush=True)
                sys.exit(EXIT_RESUME)
            break
    return done, best


class GpuLock:
    def __init__(self, path, purpose):
        self.path, self.purpose, self.token = Path(path), purpose, None

    def take(self):
        import os
        while True:
            try:
                self.path.mkdir()
                break
            except FileExistsError:
                time.sleep(10)
        self.token = f"engine-hw-rr-train-{os.getpid()}-{int(time.time())}"
        tmp = self.path / "OWNER.tmp"
        tmp.write_text(json.dumps({"pid": os.getpid(), "purpose": self.purpose, "session": "Engine HW agent (Claude Code)",
                                   "token": self.token, "since": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}))
        tmp.replace(self.path / "OWNER")
        print(json.dumps({"lock": "taken", "token": self.token}), flush=True)

    def release(self):
        try:
            if self.token and json.loads((self.path / "OWNER").read_text()).get("token") == self.token:
                (self.path / "OWNER").unlink()
                self.path.rmdir()
                print(json.dumps({"lock": "released", "token": self.token}), flush=True)
        except FileNotFoundError:
            print(json.dumps({"lock": "already gone", "token": self.token}), flush=True)
        self.token = None


def main():
    if GUARD is not None:
        print(json.dumps({"guard": GUARD.install()}), flush=True)
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("out")
    ap.add_argument("--steps", type=int, default=6000)
    ap.add_argument("--minutes", type=float, default=60.0)
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--shuffle-targets", action="store_true")
    ap.add_argument("--cpu", action="store_true")
    ap.add_argument("--val-frames", type=int, default=32)
    ap.add_argument("--eps", type=float, default=1e-3)
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--lock", default="")
    ap.add_argument("--session-minutes", type=float, default=8.0)
    ap.add_argument("--energy-weight", type=float, default=0.0)
    ap.add_argument("--init", default="", help="start from this checkpoint's model weights (strict=False; amendment 7)")
    ap.add_argument("--lr", type=float, default=LR, help="peak learning rate of the cosine schedule (amendment 7)")
    a = ap.parse_args()
    global ENERGY_W
    ENERGY_W = a.energy_weight
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
    lock = GpuLock(a.lock, f"raw-native HW H2 training{' (shuffled-target control)' if a.shuffle_targets else ''}, a session of at most {a.session_minutes:g} minutes") if a.lock else None
    net = RrNet(a.eps, a.version).to(dev)
    if a.init and not (a.resume and (Path(a.out) / "last.pt").exists()):
        missing = net.load_state_dict(torch.load(a.init, map_location=dev, weights_only=False)["model"], strict=False)
        print(json.dumps({"init": a.init, "missing": missing.missing_keys, "unexpected": missing.unexpected_keys}), flush=True)
    opt = torch.optim.AdamW(net.parameters(), lr=a.lr, weight_decay=WD)
    start, best, log = 0, math.inf, []
    ck = out / "last.pt"
    t_all = time.time()
    while True:
        if a.resume and ck.exists():
            st = torch.load(ck, map_location=dev, weights_only=False)
            net.load_state_dict(st["model"]), opt.load_state_dict(st["opt"])
            start, best, log = st["step"], st["best"], st["log"]
        if start >= a.steps or (time.time() - t_all) / 60 > a.minutes:
            break
        if lock:
            lock.take()
        try:
            start, best = train_session(a, net, opt, train, val, perm, dev, out, start, best, log,
                                        a.session_minutes if lock else a.minutes)
        finally:
            if lock:
                lock.release()
        a.resume = True
        if not lock or start >= a.steps:
            break
        time.sleep(30)
    meta = {"torch": torch.__version__, "cuda": torch.version.cuda, "device": str(dev),
            "gpu": torch.cuda.get_device_name(0) if dev.type == "cuda" else "", "shuffle_targets": a.shuffle_targets,
            "train_sequences": [s["name"] for s in train], "val_sequences": [s["name"] for s in val],
            "hyper": {"B": B, "T": T, "crop": CROP, "lr": LR, "lr_min": LR_MIN, "wd": WD, "seed": SEED, "steps": a.steps,
                      "temporal_weight": TEMP_W, "energy_weight": ENERGY_W, "eps": a.eps, "version": a.version, "peak_lr": a.lr, "init": a.init}, "log": log, "best_val_loss": best,
            "parameters": sum(p.numel() for p in net.parameters())}
    (out / "train.json").write_text(json.dumps(meta, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
