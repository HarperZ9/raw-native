#!/usr/bin/env python3
"""Fetch the glTF models of ROADMAP M2 criterion 1 and verify every file.

    python tools/assets/fetch_gltf.py <dir> [--manifest PATH] [--all-candidates]

Reads evidence/m2-gltf-models.json, or with --manifest an M3 roles manifest such as
evidence/m3-scene-models.json (the candidate each role uses, or every candidate with
--all-candidates), for the repository, commit, and each file's path and git blob hash; downloads each file from that commit into <dir>, keeping the repository's paths, and
checks it against the blob hash (the SHA-1 of "blob <size>\\0" + bytes, as git computes it).
A file already present with the right hash is not fetched again. Exits 1 on any mismatch,
and removes the bad file. The models are not stored in this repository.
"""

from __future__ import annotations

import hashlib
import json
import sys
import urllib.request
from pathlib import Path

LIST = Path(__file__).resolve().parents[2] / "evidence" / "m2-gltf-models.json"


def blob_hash(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:] if not a.startswith("--")]
    manifest = argv[argv.index("--manifest") + 1] if "--manifest" in argv else None
    if manifest:
        args = [a for a in args if a != manifest]
    if len(args) != 1:
        print(__doc__)
        return 2
    spec = json.loads((Path(manifest) if manifest else LIST).read_text(encoding="utf-8"))
    if "roles" in spec:                                   # an M3 roles manifest
        every = "--all-candidates" in argv
        spec["models"] = [c for r in spec["roles"] for c in r["candidates"] if every or c["name"] == r["use"]]
    if "generator" in spec["source"]:                     # owned assets: generated, never fetched
        root, bad, total = Path(args[0]), 0, 0
        for model in spec["models"]:
            for f in model["files"]:
                total += 1
                dest = root / f["path"]
                if not dest.is_file() or hashlib.sha256(dest.read_bytes()).hexdigest() != f["sha256"]:
                    print(f"MISSING OR CHANGED {f['path']}: generate with {spec['source']['generator']}", file=sys.stderr)
                    bad += 1
        print(f"fetch_gltf: {total - bad} of {total} generated files verified, none fetched")
        return 1 if bad else 0
    repo, commit = spec["source"]["repository"].rstrip("/"), spec["source"]["commit"]
    raw = repo.replace("https://github.com/", "https://raw.githubusercontent.com/")
    root, bad, fetched = Path(args[0]), 0, 0
    for model in spec["models"]:
        for f in model["files"]:
            dest = root / f["path"]
            if dest.is_file() and blob_hash(dest.read_bytes()) == f["git_blob"]:
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            with urllib.request.urlopen(f"{raw}/{commit}/{f['path']}", timeout=120) as r:
                data = r.read()
            if blob_hash(data) != f["git_blob"] or len(data) != f["bytes"]:
                print(f"MISMATCH {f['path']}: blob {blob_hash(data)}, {len(data)} bytes", file=sys.stderr)
                dest.unlink(missing_ok=True)
                bad += 1
                continue
            dest.write_bytes(data)
            fetched += 1
    total = sum(len(m["files"]) for m in spec["models"])
    print(f"fetch_gltf: {total - bad} of {total} files verified ({fetched} fetched) at {commit[:7]}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
