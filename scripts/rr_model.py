#!/usr/bin/env python3
"""HW H2 model: a small recurrent U-Net with kernel prediction, our own design (PyTorch, R&D only;
the engine runs its own kernels, never this file).

Per frame, from the 1 spp input and the network's previous output:
  1. features (16 channels): log1p of demodulated radiance (3), albedo (3), normal (3), depth over
     the frame's mean depth (1), roughness (1), metallic (1), log1p of the previous output warped
     by the motion vectors (3), and the history validity mask (1);
  2. a U-Net at 1/2, 1/4 and 1/8 resolution (24, 48, 64 channels) predicts, at full resolution,
     a 5 x 5 kernel, a temporal blend weight and a scale blend weight, and at 1/4 resolution a
     second 5 x 5 kernel;
  3. temporal: acc = a * warped_previous + (1 - a) * demodulated input, with a = 0 where the
     history is invalid;
  4. spatial: the full-resolution kernel filters acc; the quarter-resolution kernel filters
     acc averaged over 4 x 4 blocks; the two combine as in Vogels et al. 2018:
     out = f - w * up(down(f)) + w * up(q).
The output is demodulated linear radiance; the caller multiplies by albedo.

Version 4 (evidence/rr-bounds-amendment-6.json) takes the path tracer's split radiance (interface v2,
input channels 17 to 31): diffuse and specular are demodulated by their own albedos, filtered by their
own kernels and blends, remodulated and added to the primary emission; the history validity is a
continuous function of the warped depth, so float rounding cannot flip it.

Version 5 (evidence/rr-bounds-amendment-7.json) adds a third, coarser scale to v4: a 5 x 5 kernel a signal at 1/8
resolution, predicted from the U-Net's coarsest features, nested into the quarter-resolution stage the way the
quarter stage nests into the full one. Its head starts at zero with its blend near 0, so a v5 started from v4's
weights computes v4.
"""
import torch
import torch.nn as nn
import torch.nn.functional as F

K = 5
EPS = 1e-3
IN_CH = 16
IN_CH_V4 = 25


def conv(i, o, k=3, s=1):
    return nn.Sequential(nn.Conv2d(i, o, k, s, k // 2), nn.ReLU(inplace=True))


class RrNet(nn.Module):
    def __init__(self, eps=EPS, version=1):
        super().__init__()
        self.eps = eps   # demodulation epsilon: v1 1e-3, v2 0.05 (evidence/rr-bounds-amendment-3.json)
        # version 2 (amendment 4): depth feature log1p(depth / mean depth), and every head output bounded by
        # 8 tanh(z / 8) before its softmax or sigmoid, so the kernels cannot saturate. version 3 (amendment 5): as
        # version 2, demodulated by (1 - m) a + F0 (F0 = 0.04 (1 - m) + a m), epsilon 0.01.
        self.version = version
        cin = IN_CH_V4 if version >= 4 else IN_CH
        heads = 2 if version >= 4 else 1   # v4: one kernel set and blends for diffuse, one for specular
        self.e0 = nn.Sequential(conv(cin, 24, s=2), conv(24, 24))
        self.e1 = nn.Sequential(conv(24, 48, s=2), conv(48, 48))
        self.e2 = nn.Sequential(conv(48, 64, s=2), conv(64, 64))
        self.m1 = conv(64 + 48, 48, k=1)
        self.d1 = conv(48, 48)
        self.m0 = conv(48 + 24, 24, k=1)
        self.d0 = conv(24, 24)
        self.head = nn.Conv2d(24 + cin, heads * (K * K + 2), 1)
        self.head_q = nn.Conv2d(48, heads * K * K, 1)
        if version >= 5:   # the 1/8 scale (v5; v6 = v5b, amendment 8, differs only in the blend's start): a kernel and a blend for each signal (amendment 7)
            self.head_e = nn.Conv2d(64, 2 * K * K + 2, 1)
            nn.init.zeros_(self.head_e.weight)
            nn.init.zeros_(self.head_e.bias)
            if version == 5:   # amendment 7: the blend starts at sigmoid(8 tanh(-1)) = 0.002 (it then barely learned)
                with torch.no_grad():
                    self.head_e.bias[2 * K * K:] = -8.0
            # version 6 (v5b, amendment 8): the blend bias starts at 0, a blend of 0.5

    def forward(self, feat):
        h, w = feat.shape[-2:]
        x0 = self.e0(feat)
        x1 = self.e1(x0)
        x2 = self.e2(x1)
        u1 = self.d1(self.m1(torch.cat([F.interpolate(x2, size=x1.shape[-2:], mode="nearest"), x1], 1)))
        u0 = self.d0(self.m0(torch.cat([F.interpolate(u1, size=x0.shape[-2:], mode="nearest"), x0], 1)))
        full = self.head(torch.cat([F.interpolate(u0, size=(h, w), mode="bilinear", align_corners=False), feat], 1))
        if self.version >= 5:
            return full, self.head_q(u1), self.head_e(x2)
        if self.version >= 4:
            return full, self.head_q(u1)
        return full[:, :K * K], full[:, K * K:K * K + 1], full[:, K * K + 1:], self.head_q(u1)


def apply_kernel(img, logits):
    """Per-pixel normalised 5 x 5 kernel (softmax of logits) applied to img (B, 3, H, W)."""
    b, c, h, w = img.shape
    wts = torch.softmax(logits, 1)
    patches = F.unfold(F.pad(img, (K // 2,) * 4, mode="replicate"), K).view(b, c, K * K, h, w)
    return (patches * wts.unsqueeze(1)).sum(2)


def warp(prev, pos):
    """prev (B, C, H, W) sampled bilinearly at pixel positions pos (B, 2, H, W), centres at +0.5."""
    h, w = prev.shape[-2:]
    gx = pos[:, 0] / w * 2 - 1
    gy = pos[:, 1] / h * 2 - 1
    return F.grid_sample(prev, torch.stack([gx, gy], -1), mode="bilinear", padding_mode="border", align_corners=False)


def features(x, prev_out, prev_depth, eps=EPS, version=1):
    """x: input channels (B, 17, H, W); prev_out: previous demodulated output (B, 3, H, W) or None;
    prev_depth: previous frame's depth (B, 1, H, W) or None. Returns features, demodulated input,
    the albedo used for remodulation, the warped previous output and the validity mask."""
    rad, alb, nrm, dep = x[:, 0:3], x[:, 3:6], x[:, 6:9], x[:, 9:10]
    hit = (dep > 0).float()
    if version >= 3:   # amendment 5: demodulate by an estimate of total reflectance, d = (1 - m) a + F0
        m = x[:, 16:17]
        refl = (1 - m) * alb + 0.04 * (1 - m) + alb * m
        alb_m = refl * hit + (1 - hit)
    else:
        alb_m = alb * hit + (1 - hit)
    demod = rad / (alb_m + eps)
    pos, exp_d = x[:, 12:14], x[:, 14:15]
    if prev_out is None:
        wprev = torch.zeros_like(demod)
        valid = torch.zeros_like(dep)
    else:
        wprev = warp(prev_out, pos)
        h, w = x.shape[-2:]
        inside = ((pos[:, 0:1] >= 0.5) & (pos[:, 0:1] <= w - 0.5) & (pos[:, 1:2] >= 0.5) & (pos[:, 1:2] <= h - 0.5)).float()
        pd = warp(prev_depth, pos)
        same = ((exp_d - pd).abs() <= 0.02 * exp_d.abs() + 0.02).float()
        miss_both = ((dep <= 0) & (pd <= 0)).float()
        valid = inside * torch.clamp(same * hit + miss_both, 0, 1)
    md = dep.flatten(1).mean(1).clamp_min(EPS).view(-1, 1, 1, 1)
    dfeat = dep / md if version < 2 else torch.log1p(dep / md)
    feat = torch.cat([torch.log1p(demod.clamp_min(0)), alb, nrm, dfeat, x[:, 15:17],
                      torch.log1p(wprev.clamp_min(0)), valid], 1)
    return feat, demod, alb_m, wprev, valid


def hard_validity(x, prev_depth, pos, exp_d, dep):
    """Versions 1 to 3's tests (a threshold on the warped depth): the conditioning control of amendment 6 only."""
    h, w = x.shape[-2:]
    inside = ((pos[:, 0:1] >= 0.5) & (pos[:, 0:1] <= w - 0.5) & (pos[:, 1:2] >= 0.5) & (pos[:, 1:2] <= h - 0.5)).to(x.dtype)
    pd = warp(prev_depth, pos)
    same = ((exp_d - pd).abs() <= 0.02 * exp_d.abs() + 0.02).to(x.dtype)
    hit = (dep > 0).to(x.dtype)
    miss_both = ((dep <= 0) & (pd <= 0)).to(x.dtype)
    return inside * torch.clamp(same * hit + miss_both, 0, 1)


def soft_validity(x, prev_depth, pos, exp_d, dep):
    """Version 4: the history validity as a continuous function (method of amendment 6): 1 inside the depth
    tolerance, 0 beyond twice it, linear between; a miss continues a miss with weight clamp(1 - pd / 0.02)."""
    h, w = x.shape[-2:]
    inside = ((pos[:, 0:1] >= 0.5) & (pos[:, 0:1] <= w - 0.5) & (pos[:, 1:2] >= 0.5) & (pos[:, 1:2] <= h - 0.5)).to(x.dtype)
    pd = warp(prev_depth, pos)
    tol = 0.02 * exp_d.abs() + 0.02
    same = torch.clamp(2 - (exp_d - pd).abs() / tol, 0, 1)
    hit = (dep > 0).to(x.dtype)
    miss_both = (dep <= 0).to(x.dtype) * torch.clamp(1 - pd / 0.02, 0, 1)
    return inside * torch.clamp(same * hit + miss_both, 0, 1)


def features_v4(x, prev_out, prev_depth, eps, hard=False):
    """x: (B, 32, H, W), the 17 input channels and the 15 split channels; prev_out: the previous demodulated
    diffuse and specular outputs (B, 6, H, W) or None. Returns features, the two demodulated signals, their two
    albedos, the emission, the warped history and the validity."""
    alb, nrm, dep = x[:, 3:6], x[:, 6:9], x[:, 9:10]
    dif, spe, emi, a_d, a_s = x[:, 17:20], x[:, 20:23], x[:, 23:26], x[:, 26:29], x[:, 29:32]
    demod = torch.cat([dif / (a_d + eps), spe / (a_s + eps)], 1)
    pos, exp_d = x[:, 12:14], x[:, 14:15]
    if prev_out is None:
        wprev = torch.zeros_like(demod)
        valid = torch.zeros_like(dep)
    else:
        wprev = warp(prev_out, pos)
        valid = (hard_validity if hard else soft_validity)(x, prev_depth, pos, exp_d, dep)
    md = dep.flatten(1).mean(1).clamp_min(EPS).view(-1, 1, 1, 1)
    feat = torch.cat([torch.log1p(demod.clamp_min(0)), a_d, a_s, nrm, torch.log1p(dep / md), x[:, 15:17],
                      torch.log1p(wprev.clamp_min(0)), valid], 1)
    return feat, demod, torch.cat([a_d, a_s], 1), emi, wprev, valid


def filter_signal(acc, k_full, k_q, wb):
    f = apply_kernel(acc, k_full)
    h, w = acc.shape[-2:]
    hq, wq = k_q.shape[-2:]
    q = apply_kernel(F.adaptive_avg_pool2d(acc, (hq, wq)), k_q)
    wb = torch.sigmoid(wb)
    up = lambda t: F.interpolate(t, size=(h, w), mode="bilinear", align_corners=False)
    return (f - wb * up(F.adaptive_avg_pool2d(f, (hq, wq))) + wb * up(q)).clamp_min(0)


def filter_signal_v5(acc, k_full, k_q, wb, k_e, wb2):
    """v4's two scales with a third at the 1/8 kernels' size, nested: q2 = q - wb2 up(down(q)) + wb2 up(e)."""
    h, w = acc.shape[-2:]
    hq, wq = k_q.shape[-2:]
    he, we = k_e.shape[-2:]
    acc_q = F.adaptive_avg_pool2d(acc, (hq, wq))
    q = apply_kernel(acc_q, k_q)
    e = apply_kernel(F.adaptive_avg_pool2d(acc, (he, we)), k_e)
    wb2 = torch.sigmoid(F.interpolate(wb2, size=(hq, wq), mode="bilinear", align_corners=False))
    upq = lambda t: F.interpolate(t, size=(hq, wq), mode="bilinear", align_corners=False)
    q = q - wb2 * upq(F.adaptive_avg_pool2d(q, (he, we))) + wb2 * upq(e)
    f = apply_kernel(acc, k_full)
    wb = torch.sigmoid(wb)
    up = lambda t: F.interpolate(t, size=(h, w), mode="bilinear", align_corners=False)
    return (f - wb * up(F.adaptive_avg_pool2d(f, (hq, wq))) + wb * up(q)).clamp_min(0)


def step_v4(net, x, prev_out, prev_depth):
    feat, demod, albs, emi, wprev, valid = features_v4(x, prev_out, prev_depth, net.eps, getattr(net, "hard_validity", False))
    heads = net(feat)
    full, k_q = 8 * torch.tanh(heads[0] / 8), 8 * torch.tanh(heads[1] / 8)
    k_e = 8 * torch.tanh(heads[2] / 8) if net.version >= 5 else None
    kk = K * K
    outs = []
    for i in range(2):   # 0 diffuse, 1 specular
        k_full = full[:, i * kk:(i + 1) * kk]
        a = torch.sigmoid(full[:, 2 * kk + i:2 * kk + i + 1]) * valid
        wb = full[:, 2 * kk + 2 + i:2 * kk + 3 + i]
        acc = a * wprev[:, 3 * i:3 * i + 3] + (1 - a) * demod[:, 3 * i:3 * i + 3]
        if k_e is not None:
            outs.append(filter_signal_v5(acc, k_full, k_q[:, i * kk:(i + 1) * kk], wb, k_e[:, i * kk:(i + 1) * kk],
                                         k_e[:, 2 * kk + i:2 * kk + i + 1]))
        else:
            outs.append(filter_signal(acc, k_full, k_q[:, i * kk:(i + 1) * kk], wb))
    out = torch.cat(outs, 1)
    return outs[0] * albs[:, 0:3] + outs[1] * albs[:, 3:6] + emi, out


def step(net, x, prev_out, prev_depth):
    """One frame. Returns (remodulated output, demodulated output for the next frame)."""
    v = getattr(net, "version", 1)
    if v >= 4:
        return step_v4(net, x, prev_out, prev_depth)
    feat, demod, alb_m, wprev, valid = features(x, prev_out, prev_depth, getattr(net, "eps", EPS), v)
    k_full, a, wb, k_q = net(feat)
    if v >= 2:
        k_full, a, wb, k_q = (8 * torch.tanh(t / 8) for t in (k_full, a, wb, k_q))
    a = torch.sigmoid(a) * valid
    acc = a * wprev + (1 - a) * demod
    f = apply_kernel(acc, k_full)
    h, w = acc.shape[-2:]
    hq, wq = k_q.shape[-2:]
    acc_q = F.adaptive_avg_pool2d(acc, (hq, wq))
    q = apply_kernel(acc_q, k_q)
    wb = torch.sigmoid(wb)
    up = lambda t: F.interpolate(t, size=(h, w), mode="bilinear", align_corners=False)
    out = (f - wb * up(F.adaptive_avg_pool2d(f, (hq, wq))) + wb * up(q)).clamp_min(0)
    return out * alb_m, out
