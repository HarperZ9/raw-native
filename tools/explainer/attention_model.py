#!/usr/bin/env python3
"""Train a tiny attention-only transformer on the CPU and export what the explainer draws.

    python tools/explainer/attention_model.py --out docs/explainer/attention [--seed 7] [--steps 2500]

The task: 64 random characters from a 64-character alphabet, with one stretch of 12 to 28
characters repeated straight after itself, at a random place. The model predicts each next
character. Outside the repeat it can only guess (1 in 64). Inside the repeat it can look back:
find the earlier copy of the current character and read what came after it. The stretch length
and place vary, so a fixed positional offset does not work; two attention layers have to learn
it together (a previous-token head, then an induction head).

Writes, into --out:
  data.json      the example sequence, the two heads' attention rows, accuracies, ablations,
                 the training settings and the SHA-256 of the array file
  residual.npy   float16 [256, 64, 64]: the final residual stream for 256 held-out sequences
                 (1,048,576 numbers), the point cloud the film opens on

Deterministic: seeded, deterministic algorithms, a fixed thread count. Running it twice on the
same torch build writes the same bytes (data.json records the torch version).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789#&"
V, T, D, HEADS = 64, 64, 64, 2
LMIN, LMAX = 12, 28


def batch(gen, n, torch):
    """Random strings with one repeated stretch each; returns (idx, start, length)."""
    idx = torch.randint(0, V, (n, T), generator=gen)
    L = torch.randint(LMIN, LMAX + 1, (n,), generator=gen)
    start = (torch.rand(n, generator=gen) * (T - 2 * L + 1).float()).long()
    for b in range(n):
        s, l = int(start[b]), int(L[b])
        idx[b, s + l:s + 2 * l] = idx[b, s:s + l]
    return idx, start, L


def rope(x, torch):
    """Rotary position on queries and keys: [B, H, T, d]. Position enters attention only as an
    offset between query and key, so the residual stream carries no absolute position."""
    d = x.shape[-1]
    inv = 1.0 / (10000 ** (torch.arange(0, d, 2, dtype=torch.float32) / d))
    ang = torch.arange(x.shape[-2], dtype=torch.float32)[:, None] * inv[None, :]
    c, s = ang.cos(), ang.sin()
    x1, x2 = x[..., 0::2], x[..., 1::2]
    return torch.stack([x1 * c - x2 * s, x1 * s + x2 * c], -1).flatten(-2)


def build(torch):
    nn = torch.nn

    class Attn(nn.Module):
        def __init__(self):
            super().__init__()
            self.qkv = nn.Linear(D, 3 * D, bias=False)
            self.o = nn.Linear(D, D, bias=False)

        def forward(self, x, mask, ablate=None):
            B, T, _ = x.shape
            q, k, v = self.qkv(x).view(B, T, 3, HEADS, D // HEADS).permute(2, 0, 3, 1, 4)
            q, k = rope(q, torch), rope(k, torch)
            att = (q @ k.transpose(-1, -2)) / math.sqrt(D // HEADS)
            att = att.masked_fill(mask, float("-inf")).softmax(-1)
            z = att @ v
            if ablate is not None:
                z[:, ablate] = 0
            return self.o(z.transpose(1, 2).reshape(B, T, D)), att

    class Model(nn.Module):
        def __init__(self):
            super().__init__()
            self.tok = nn.Embedding(V, D)
            self.layers = nn.ModuleList([Attn(), Attn()])
            self.unembed = nn.Linear(D, V, bias=False)

        def forward(self, idx, ablate=(None, None)):
            n = idx.shape[1]
            mask = torch.triu(torch.ones(n, n, dtype=torch.bool), 1)
            x = self.tok(idx)
            atts = []
            for layer, ab in zip(self.layers, ablate):
                dx, att = layer(x, mask, ab)
                x = x + dx
                atts.append(att)
            return self.unembed(x), atts, x

    return Model()


def repeat_mask(start, L, torch):
    """[n, T-1] True where the next character is predictable: the prediction made at position p
    for p + 1 inside the second copy (p from start + L to start + 2L - 2)."""
    p = torch.arange(T - 1)[None, :]
    s, l = start[:, None], L[:, None]
    return (p >= s + l) & (p <= s + 2 * l - 2)


def accuracy(logits, idx, mask):
    """Share of right next-character predictions inside the repeat and outside it."""
    pred = logits[:, :-1].argmax(-1) == idx[:, 1:]
    return {"repeat": float(pred[mask].float().mean()), "elsewhere": float(pred[~mask].float().mean())}


def train(torch, seed, steps, log):
    torch.manual_seed(seed)
    gen = torch.Generator().manual_seed(seed)
    model = build(torch)
    opt = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=0.01)
    F = torch.nn.functional
    for step in range(steps):
        idx, _, _ = batch(gen, 64, torch)
        logits, _, _ = model(idx)
        loss = F.cross_entropy(logits[:, :-1].reshape(-1, V), idx[:, 1:].reshape(-1))
        opt.zero_grad()
        loss.backward()
        opt.step()
        if step % 250 == 0 or step == steps - 1:
            log.append({"step": step, "loss": round(float(loss), 4)})
            print(f"step {step} loss {float(loss):.4f}", flush=True)
    return model


def pick_heads(atts, start, L):
    """Mean weight each layer-0 head puts on the previous token, and each layer-1 head puts on
    the token after the earlier copy of the current one (queries inside the second copy)."""
    rows, cols = list(range(1, T)), list(range(0, T - 1))
    prev = [float(atts[0][:, h, rows, cols].mean()) for h in range(HEADS)]
    ind = [0.0] * HEADS
    for h in range(HEADS):
        acc, n = 0.0, 0
        for b in range(atts[1].shape[0]):
            s, l = int(start[b]), int(L[b])
            for q in range(s + l, s + 2 * l):
                acc += float(atts[1][b, h, q, q - l + 1]); n += 1
        ind[h] = acc / n
    return prev, ind


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def evaluate(model, torch, np, seed, out):
    gen = torch.Generator().manual_seed(seed + 1000)
    with torch.no_grad():
        test, start, L = batch(gen, 256, torch)
        mask = repeat_mask(start, L, torch)
        logits, atts, resid = model(test)
        prev, ind = pick_heads(atts, start, L)
        hp, hi = int(np.argmax(prev)), int(np.argmax(ind))
        acc = accuracy(logits, test, mask)
        abl = {}
        for name, ab in (("induction_head_zeroed", (None, hi)), ("previous_token_head_zeroed", (hp, None)),
                         ("both_layer_1_heads_zeroed", (None, list(range(HEADS))))):
            lg, _, _ = model(test, ablate=ab)
            abl[name] = accuracy(lg, test, mask)
        probs = logits[0, :-1].softmax(-1)
        p_next = [float(probs[i, test[0, i + 1]]) for i in range(T - 1)]
        np.save(out / "residual.npy", resid.numpy().astype(np.float16))
    return {"test": test, "start": int(start[0]), "L": int(L[0]), "atts": atts, "prev": prev, "ind": ind, "hp": hp, "hi": hi, "acc": acc, "abl": abl, "p_next": p_next}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--steps", type=int, default=4000)
    a = ap.parse_args()
    import numpy as np
    import torch
    torch.set_num_threads(4)
    torch.use_deterministic_algorithms(True)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    log: list = []
    model = train(torch, a.seed, a.steps, log)
    model.eval()
    e = evaluate(model, torch, np, a.seed, out)
    r = lambda x: round(float(x), 5)
    hp, hi = e["hp"], e["hi"]
    data = {
        "schema": "raw-native.explainer-data/1",
        "what": "A 2-layer attention-only transformer trained on a CPU on repeated random strings",
        "script": "tools/explainer/attention_model.py",
        "settings": {"seed": a.seed, "steps": a.steps, "alphabet": ALPHABET, "vocab": V, "length": T, "repeat_length": [LMIN, LMAX], "d_model": D,
                     "heads_per_layer": HEADS, "batch": 64, "lr": 3e-3, "test_sequences": 256},
        "torch": torch.__version__,
        "train_log": log,
        "heads": {"previous_token": {"layer": 0, "head": hp, "mean_weight": r(e["prev"][hp]), "all_heads": [r(x) for x in e["prev"]]},
                  "induction": {"layer": 1, "head": hi, "mean_weight": r(e["ind"][hi]), "all_heads": [r(x) for x in e["ind"]]}},
        "accuracy": {k: r(v) for k, v in e["acc"].items()},
        "chance": r(1 / V),
        "ablations": {k: {kk: r(vv) for kk, vv in v.items()} for k, v in e["abl"].items()},
        "example": {
            "index": 0,
            "repeat_start": e["start"],
            "repeat_length": e["L"],
            "text": "".join(ALPHABET[i] for i in e["test"][0].tolist()),
            "p_next": [r(x) for x in e["p_next"]],
            "previous_token_rows": [[r(x) for x in row] for row in e["atts"][0][0, hp].tolist()],
            "induction_rows": [[r(x) for x in row] for row in e["atts"][1][0, hi].tolist()],
        },
        "residual": {"file": "residual.npy", "shape": [256, T, D], "dtype": "float16",
                     "what": "final residual stream: sequence, position, dimension"},
        "files": {"residual.npy": sha(out / "residual.npy")},
    }
    (out / "data.json").write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8", newline="\n")   # LF on every OS
    print(json.dumps({"heads": [hp, hi], "w": [data["heads"]["previous_token"]["mean_weight"], data["heads"]["induction"]["mean_weight"]],
                      "acc": data["accuracy"], "abl": data["ablations"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
