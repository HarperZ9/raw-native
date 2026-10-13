#!/usr/bin/env python3
"""HW H2: export a trained RrNet to the flat weight file our Vulkan kernels read, and write the
PyTorch forward pass of a sequence as the parity reference (evidence/rr-inference-bounds.json).

Weight file: little-endian float32. For each layer in LAYERS order: weights [cout][cin][k][k], then
bias [cout]. A JSON sidecar lists the layers, shapes and the file's SHA-256.

Usage: rr_export.py MODEL.pt OUT.bin [--ref-seq DIR --ref-out DIR [--frames N]]
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_data  # noqa: E402
from rr_model import RrNet, step  # noqa: E402

LAYERS = ["e0.0.0", "e0.1.0", "e1.0.0", "e1.1.0", "e2.0.0", "e2.1.0", "m1.0", "d1.0", "m0.0", "d0.0", "head", "head_q"]


def export(sd, out):
    blob, rows = bytearray(), []
    for name in LAYERS:
        w, b = sd[name + ".weight"].float().numpy(), sd[name + ".bias"].float().numpy()
        rows.append({"name": name, "cout": int(w.shape[0]), "cin": int(w.shape[1]), "k": int(w.shape[2])})
        blob += w.astype("<f4").tobytes() + b.astype("<f4").tobytes()
    Path(out).write_bytes(bytes(blob))
    meta = {"layers": rows, "bytes": len(blob), "sha256": hashlib.sha256(bytes(blob)).hexdigest()}
    Path(str(out) + ".json").write_text(json.dumps(meta, indent=1))
    return meta


@torch.no_grad()
def reference(sd, seq, out_dir, frames, eps=1e-3, version=1):
    net = RrNet(eps, version)
    net.load_state_dict(sd)
    net.eval()
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    n = rr_data.frame_count(seq) if frames <= 0 else min(frames, rr_data.frame_count(seq))
    prev, pdep = None, None
    for k in range(n):
        _, x, _, _ = rr_data.load_frame(seq, k)
        xt = torch.from_numpy(x).permute(2, 0, 1).unsqueeze(0).contiguous()
        out, prev = step(net, xt, prev, pdep)
        pdep = xt[:, 9:10]
        out[0].permute(1, 2, 0).numpy().astype("<f4").tofile(out_dir / f"f{k:03d}.f32")
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("out")
    ap.add_argument("--ref-seq", default="")
    ap.add_argument("--ref-out", default="")
    ap.add_argument("--frames", type=int, default=0)
    a = ap.parse_args()
    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    sd, eps, version = ck["model"], ck.get("eps", 1e-3), ck.get("version", 1)
    meta = export(sd, a.out)
    meta["eps"], meta["version"] = eps, version
    Path(str(a.out) + ".json").write_text(json.dumps(meta, indent=1))
    print(json.dumps({"sha256": meta["sha256"], "eps": eps}))
    if a.ref_seq:
        print(json.dumps({"reference_frames": reference(sd, a.ref_seq, a.ref_out, a.frames, eps, version)}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
