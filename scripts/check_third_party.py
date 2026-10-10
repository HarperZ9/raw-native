#!/usr/bin/env python3
"""ROADMAP M2 criterion 6: every vendored or ported third-party file is listed with its hash.

    python scripts/check_third_party.py            # verify third_party/MANIFEST.md against the tree
    python scripts/check_third_party.py --print    # print the hash block for the listed files
    python scripts/check_third_party.py --selftest # the check catches a changed byte and an unlisted file

The manifest's hash block is a fenced block opened with ```sha256, one "<sha256>  <path>"
line per file. The check fails when a listed file is missing or its hash differs, when a
file under third_party/ is not listed, or when superstack's own SUPERSTACK.sha256 disagrees
with the manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = "third_party/MANIFEST.md"


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def listed(text: str) -> dict[str, str]:
    m = re.search(r"```sha256\n(.*?)```", text, re.S)
    if not m:
        raise SystemExit(f"{MANIFEST}: no ```sha256 block")
    out = {}
    for line in m.group(1).splitlines():
        if line.strip():
            h, path = line.split(None, 1)
            out[path.strip()] = h
    return out


def tracked(root: Path, prefix: str) -> list[str]:
    r = subprocess.run(["git", "ls-files", prefix], cwd=root, capture_output=True, text=True, check=True)
    return [p for p in r.stdout.splitlines() if p]


def problems(root: Path, files: list[str] | None = None) -> list[str]:
    want = listed((root / MANIFEST).read_text(encoding="utf-8"))
    out = []
    for path, h in want.items():
        p = root / path
        if not p.is_file():
            out.append(f"{path}: listed but missing")
        elif sha(p) != h:
            out.append(f"{path}: hash {sha(p)[:12]}... differs from the manifest's {h[:12]}...")
    for path in files if files is not None else tracked(root, "third_party"):
        if path != MANIFEST and path not in want:
            out.append(f"{path}: under third_party/ but not in the manifest")
    pin = root / "third_party/superstack/SUPERSTACK.sha256"
    if pin.is_file():
        for line in pin.read_text(encoding="utf-8").splitlines():
            if line.strip():
                h, rel = line.split(None, 1)
                path = f"third_party/superstack/{rel.strip()}"
                if want.get(path) != h:
                    out.append(f"{path}: SUPERSTACK.sha256 and the manifest disagree")
    return out


def selftest() -> list[str]:
    """Copy the listed files to a scratch tree, then break it twice; each break must be caught."""
    failures = []
    want = listed((ROOT / MANIFEST).read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        for path in [MANIFEST, *want]:
            (tmp / path).parent.mkdir(parents=True, exist_ok=True)
            (tmp / path).write_bytes((ROOT / path).read_bytes())
        files = [p for p in want if p.startswith("third_party/")]
        if problems(tmp, files):
            failures.append("the clean copy does not pass")
        victim = tmp / next(iter(want))
        data = bytearray(victim.read_bytes())
        data[len(data) // 2] ^= 1
        victim.write_bytes(bytes(data))
        if not any("differs" in p for p in problems(tmp, files)):
            failures.append("a flipped bit was not caught")
        victim.write_bytes((ROOT / next(iter(want))).read_bytes())
        if not any("not in the manifest" in p for p in problems(tmp, files + ["third_party/new/lib.h"])):
            failures.append("an unlisted file was not caught")
    return failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--print", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.print:
        for path in listed((ROOT / MANIFEST).read_text(encoding="utf-8")):
            print(f"{sha(ROOT / path)}  {path}")
        return 0
    found = selftest() if a.selftest else problems(ROOT)
    print("\n".join(found) or ("self-test passed" if a.selftest else f"{MANIFEST}: every file matches"))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
