#!/usr/bin/env python3
"""HW H2 dataset provenance and the train-on-test guard (evidence/rr-bounds.json), stdlib only.

  rr_manifest.py manifest ROOT OUT.json --generator-commit SHA
      every frame: split, scene, path, frame, seed, cameras, spp, renderer, and SHA-256 of each file
  rr_manifest.py guard ROOT [--inject-test-frame]
      fails (exit 1) when any training or validation file's SHA-256 equals any test file's, or a
      training scene name equals a test scene name; --inject-test-frame adds one test frame to the
      training list first (the control that must fail)
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

KINDS = ("_in.f32", "_ref.f32", "_tri.i32", "_split.f32", ".json")


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def frames(root, split):
    for d in sorted((Path(root) / split).iterdir()):
        if not d.is_dir():
            continue
        k = 0
        while (d / f"f{k:03d}.json").exists():
            yield d, k
            k += 1


def manifest(root, out, commit):
    rows = []
    for split in ("train", "val", "test"):
        for d, k in frames(root, split):
            m = json.loads((d / f"f{k:03d}.json").read_text())
            rows.append({"split": split, "sequence": d.name, "scene": m["scene"], "path": m["path"], "frame": k,
                         "seed": m["seed"], "camera": m["camera"], "previous": m["previous"], "input_spp": m["input_spp"],
                         "ref_spp": m["ref_spp"], "renderer": f'rr-render at {commit}, path tracer interface v{m["interface_version"]}',
                         "adapter": m["adapter"], "ref_fraction_se_under_1pct": m["ref_fraction_se_under_1pct"],
                         "sha256": {kind: sha(d / f"f{k:03d}{kind}") for kind in KINDS}})
    doc = {"schema": "raw-native.evidence/1", "bounds": "evidence/rr-bounds.json with amendments 1 and 2",
           "generator_commit": commit, "root": "the dataset root on the rendering machine (not in the repo)",
           "frames": len(rows), "by_split": {s: sum(r["split"] == s for r in rows) for s in ("train", "val", "test")},
           "rows": rows}
    Path(out).write_text(json.dumps(doc, indent=0))
    print(json.dumps(doc["by_split"]))


def guard(root, inject):
    test = {}
    test_scenes = set()
    for d, k in frames(root, "test"):
        test_scenes.add(json.loads((d / f"f{k:03d}.json").read_text())["scene"])
        for kind in KINDS[:3]:
            test[sha(d / f"f{k:03d}{kind}")] = f"{d.name}/f{k:03d}{kind}"
    train = [(d, k) for s in ("train", "val") for d, k in frames(root, s)]
    if inject:
        train.append(next(iter(frames(root, "test"))))
    bad = []
    for d, k in train:
        if json.loads((d / f"f{k:03d}.json").read_text())["scene"] in test_scenes:
            bad.append(f"{d.name}/f{k:03d}: test scene")
        for kind in KINDS[:3]:
            h = sha(d / f"f{k:03d}{kind}")
            if h in test:
                bad.append(f"{d.name}/f{k:03d}{kind} equals {test[h]}")
    print(json.dumps({"checked_training_frames": len(train), "test_files": len(test), "matches": bad[:10], "pass": not bad}))
    return 0 if not bad else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["manifest", "guard"])
    ap.add_argument("root")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--generator-commit", default="")
    ap.add_argument("--inject-test-frame", action="store_true")
    a = ap.parse_args()
    if a.cmd == "manifest":
        manifest(a.root, a.out, a.generator_commit)
        return 0
    return guard(a.root, a.inject_test_frame)


if __name__ == "__main__":
    sys.exit(main())
