#!/usr/bin/env python3
"""HW H2 evaluation on the held-out test scenes (evidence/rr-bounds.json, amendments 1 and 2).

Methods: the 1 spp input; ours (best.pt); ours with motion zeroed (control); the shuffled-target
network (control); SVGF (rr_svgf.py); SVGF then M3's TAA (raw_native_cli rr-taa). Metrics per
frame against the reference: PSNR linear (peak: the 99.9th percentile of the reference's values),
PSNR on the tone-mapped frame (x / (1 + x), then sRGB; peak 1), FLIP on the tone-mapped frame
(our implementation, UNCHECKED against the reference test pair), temporal instability and ghosting.

Choices fixed here before any test result was computed: temporal instability and ghosting are
measured on the tone-mapped frame; temporal instability skips frame 0 and pixels whose history is
disoccluded at that frame; the ghosting trail is the pixels whose history age (frames since their
last disocclusion, carried along the motion) is below 8; frames with no trail pixel give no
ghosting sample. Paired bootstrap: 10,000 resamples of frames pooled over the test scenes, seed 7.

Usage: rr_eval.py ROOT --model best.pt --shuffled best.pt --cli raw_native_cli.exe --work DIR --out OUT.json
"""
import argparse
import os

os.environ.setdefault("FOR_DISABLE_CONSOLE_CTRL_HANDLER", "1")
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_data  # noqa: E402
import rr_flip  # noqa: E402
import rr_svgf  # noqa: E402
from rr_model import RrNet, step  # noqa: E402

TRAIL_AGE, BOOT, BOOT_SEED = 8, 10000, 7


def tonemap(x):
    return rr_flip_srgb(np.clip(x, 0, None) / (1 + np.clip(x, 0, None)))


def rr_flip_srgb(v):
    return np.where(v <= 0.0031308, 12.92 * v, 1.055 * np.power(np.maximum(v, 1e-12), 1 / 2.4) - 0.055)


@torch.no_grad()
def run_net(path, x, zero_motion=False):
    ck = torch.load(path, map_location="cpu", weights_only=False)
    net = RrNet(ck.get("eps", 1e-3), ck.get("version", 1))
    net.load_state_dict(ck["model"])
    net.eval()
    xt = torch.from_numpy(x).permute(0, 3, 1, 2).contiguous()
    if zero_motion:
        n, _, h, w = xt.shape
        yy, xx = torch.meshgrid(torch.arange(h, dtype=torch.float32), torch.arange(w, dtype=torch.float32), indexing="ij")
        xt[:, 10:12] = 0
        xt[:, 12] = xx + 0.5
        xt[:, 13] = yy + 0.5
        xt[:, 14] = xt[:, 9]
    prev, pdep, outs = None, None, []
    for t in range(xt.shape[0]):
        out, prev = step(net, xt[t:t + 1], prev, pdep)
        pdep = xt[t:t + 1, 9:10]
        outs.append(out[0].permute(1, 2, 0).numpy())
    return np.stack(outs)


def run_svgf(x):
    n, h, w, _ = x.shape
    f = rr_svgf.Svgf(h, w)
    outs = []
    for t in range(n):
        alb, dep = x[t, ..., 3:6], x[t, ..., 9]
        hit = (dep > 0)[..., None]
        alb_m = np.where(hit, alb, 1.0)
        demod = x[t, ..., 0:3] / (alb_m + 1e-3)
        out = f.frame(demod.astype(np.float64), x[t, ..., 10:12], np.where(dep > 0, dep, 1e6), x[t, ..., 6:9])
        outs.append((out * alb_m).astype(np.float32))
    return np.stack(outs)


def run_taa(cli, seq_dir, frames, work):
    src, dst = Path(work) / "svgf", Path(work) / "taa"
    src.mkdir(parents=True, exist_ok=True)
    for t, f in enumerate(frames):
        f.astype(np.float32).tofile(src / f"f{t:03d}.f32")
    subprocess.run([cli, "rr-taa", "--seq", str(seq_dir), "--in", str(src), "--out", str(dst)], check=True, capture_output=True)
    h, w = frames.shape[1:3]
    return np.stack([np.fromfile(dst / f"f{t:03d}.f32", np.float32).reshape(h, w, 3) for t in range(len(frames))])


def history_age(tri, motion):
    n = tri.shape[0]
    age = np.zeros(tri.shape, np.int32)
    for t in range(1, n):
        dis = rr_data.disocclusion(tri[t], tri[t - 1], motion[t])
        warped, _ = rr_data.warp_nearest(age[t - 1], motion[t])
        age[t] = np.where(dis, 0, warped + 1)
    return age


def metrics(out, ref, x, tri, age, var):
    n = out.shape[0]
    rows = []
    tm_o, tm_r = tonemap(out), tonemap(ref)
    for t in range(n):
        peak = np.percentile(ref[t], 99.9)
        mse_lin = float(np.mean((out[t] - ref[t]) ** 2))
        mse_tm = float(np.mean((tm_o[t] - tm_r[t]) ** 2))
        row = {"psnr_linear": 10 * np.log10(peak * peak / max(mse_lin, 1e-20)),
               "psnr_tonemapped": 10 * np.log10(1.0 / max(mse_tm, 1e-20)),
               "flip_unchecked": float(np.mean(rr_flip.flip_map(tm_r[t], tm_o[t]))),
               "mse_lum_linear": float(np.mean((rr_data.lum(out[t]) - rr_data.lum(ref[t])) ** 2)),
               "target_noise": float(np.mean(var[t]))}
        if t >= 1:
            m = x[t, ..., 10:12]
            po, _ = rr_data.warp_nearest(tm_o[t - 1], m)
            pr, _ = rr_data.warp_nearest(tm_r[t - 1], m)
            keep = age[t] > 0
            d = np.abs((tm_o[t] - po) - (tm_r[t] - pr)).mean(-1)
            row["temporal_instability"] = float(d[keep].mean()) if keep.any() else None
        trail = (age[t] < TRAIL_AGE) if t > 0 else np.zeros(age[t].shape, bool)
        row["ghosting"] = float(np.abs(tm_o[t] - tm_r[t]).mean(-1)[trail].mean()) if trail.any() else None
        rows.append(row)
    return rows


def boot(a, b, seed=BOOT_SEED):
    """Paired bootstrap of mean(a - b): (mean, lo, hi) of the 95% interval."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = a - b
    rng = np.random.default_rng(seed)
    idx = rng.integers(0, len(d), (BOOT, len(d)))
    means = d[idx].mean(1)
    return float(d.mean()), float(np.percentile(means, 2.5)), float(np.percentile(means, 97.5))


METHODS = ("input", "ours", "ours_zero_motion", "shuffled", "svgf", "svgf_taa")


def evaluate_sequence(d, a):
    metas, x, r, tri = rr_data.load_sequence(d)
    ref, var = r[..., 0:3], r[..., 3]
    outs = {"input": x[..., 0:3], "ours": run_net(a.model, x), "ours_zero_motion": run_net(a.model, x, zero_motion=True),
            "shuffled": run_net(a.shuffled, x), "svgf": run_svgf(x)}
    outs["svgf_taa"] = run_taa(a.cli, d, outs["svgf"], Path(a.work) / d.name)
    age = history_age(tri, x[..., 10:12])
    return {m: metrics(o, ref, x, tri, age, var) for m, o in outs.items()}, [m["ref_fraction_se_under_1pct"] for m in metas]


def pooled(per_seq, method, key, frames):
    return [per_seq[s][method][t][key] for s, t in frames]


def verdict(per_seq):
    frames = []   # (sequence, frame) kept by the target-noise validity check
    excluded = []
    for s, res in per_seq.items():
        for t in range(len(res["ours"])):
            noise = res["ours"][t]["target_noise"]
            least = min(res[m][t]["mse_lum_linear"] for m in METHODS if m != "input")
            (frames if noise < 0.1 * least else excluded).append((s, t))
    def pair(m1, m2, key, fr=frames):
        ok = [(s, t) for s, t in fr if per_seq[s][m1][t].get(key) is not None and per_seq[s][m2][t].get(key) is not None]
        return boot(pooled(per_seq, m1, key, ok), pooled(per_seq, m2, key, ok)) + (len(ok),)
    q = {k: pair("ours", "svgf", k) for k in ("flip_unchecked", "psnr_linear", "psnr_tonemapped")}
    quality = q["flip_unchecked"][2] < 0 and q["psnr_linear"][1] > 0 and q["psnr_tonemapped"][1] > 0
    tq = pair("ours", "svgf_taa", "temporal_instability")
    gh = pair("ours", "svgf_taa", "ghosting")
    sq = {k: pair("shuffled", "svgf", k) for k in ("flip_unchecked", "psnr_linear", "psnr_tonemapped")}
    shuffled_quality = sq["flip_unchecked"][2] < 0 and sq["psnr_linear"][1] > 0 and sq["psnr_tonemapped"][1] > 0
    zg = pair("ours_zero_motion", "svgf_taa", "ghosting")
    return {"frames_kept": len(frames), "frames_excluded_by_target_noise": excluded,
            "quality_vs_svgf": q, "quality_pass": quality,
            "temporal_vs_svgf_taa": tq, "temporal_pass": tq[2] <= 0,
            "ghosting_vs_svgf_taa": gh, "ghosting_pass": gh[2] <= 0,
            "control_shuffled_vs_svgf": sq, "control_shuffled_fails": not shuffled_quality,
            "control_zero_motion_ghosting_vs_svgf_taa": zg, "control_zero_motion_fails": not (zg[2] <= 0),
            "format": "each comparison: [mean of (first - second), bootstrap 2.5%, 97.5%, frames]"}


def summary(per_seq):
    out = {}
    for s, res in per_seq.items():
        out[s] = {m: {k: float(np.mean([r[k] for r in rows if r.get(k) is not None]))
                      for k in ("psnr_linear", "psnr_tonemapped", "flip_unchecked", "temporal_instability", "ghosting")}
                  for m, rows in res.items()}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--model", required=True)
    ap.add_argument("--shuffled", required=True)
    ap.add_argument("--cli", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--split", default="test")
    ap.add_argument("--max-frames", type=int, default=0)
    a = ap.parse_args()
    torch.set_num_threads(8)
    per_seq, se = {}, {}
    for d in rr_data.sequences(a.root, a.split):
        cache = Path(a.work) / f"{d.name}.metrics.json"   # skip-done on resume
        if cache.exists():
            c = json.loads(cache.read_text())
            per_seq[d.name], se[d.name] = c["metrics"], c["se"]
        else:
            per_seq[d.name], se[d.name] = evaluate_sequence(d, a)
            Path(a.work).mkdir(parents=True, exist_ok=True)
            tmp = cache.with_suffix(".tmp")
            tmp.write_text(json.dumps({"metrics": per_seq[d.name], "se": se[d.name]}))
            tmp.replace(cache)
        print(d.name, json.dumps(summary({d.name: per_seq[d.name]})[d.name]["ours"]), flush=True)
    v = verdict(per_seq)
    doc = {"schema": "raw-native.evidence/1", "bounds": "evidence/rr-bounds.json with amendments 1 and 2",
           "split": a.split, "flip": "UNCHECKED against the reference implementation's test pair",
           "per_sequence_means": summary(per_seq), "ref_fraction_se_under_1pct_mean": {k: float(np.mean(v_)) for k, v_ in se.items()},
           "verdict": v, "pass": bool(v["quality_pass"] and v["temporal_pass"] and v["ghosting_pass"]
                                      and v["control_shuffled_fails"] and v["control_zero_motion_fails"])}
    Path(a.out).write_text(json.dumps(doc, indent=1))
    print(json.dumps(v, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
