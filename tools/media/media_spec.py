"""The media spec (docs/media/media.json): setup, facts and walkthrough recordings.

Facts are values read from the checkout at render time, so a video shows the numbers
of the commit it was rendered at. Kinds:
  {"json": "path", "pointer": "/a/0/b"}       a JSON value
  {"text": "path", "regex": "(...)"}          the first group of a regex match in a file
  {"cmd": [...], "regex": "(...)"}            a command's output (optionally a regex group)
  {"cmd": [...], "exit": true}                the command's exit code
  {"cmd": [...], "stdin": "path"}             (either form) with a file as its input
A cmd fact may set "cwd", relative to the repository root.
  {"image": "path.pgm", "size": 64}           a P5 PGM or Pf/PF PFM image, averaged to size x size, values 0..1
A fact that cannot be read fails the render.
"""

from __future__ import annotations

import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path


def load(spec_path: Path) -> dict:
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    if spec.get("schema") != "raw-native.media/1":
        sys.exit(f"{spec_path}: schema must be raw-native.media/1")
    return spec


def repo_root(start: Path) -> Path:
    r = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=start, capture_output=True, text=True)
    return Path(r.stdout.strip()) if r.returncode == 0 else start


def commit(root: Path) -> str:
    r = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def resolve_vars(spec: dict, root: Path) -> dict:
    """A var names its candidates; the first that exists (or the last) wins: platform paths."""
    out = {}
    for k, cands in spec.get("vars", {}).items():
        cands = [cands] if isinstance(cands, str) else cands
        out[k] = next((c for c in cands if (root / c).exists()), cands[-1])
    return out


def expand(argv: list[str], vars_: dict) -> list[str]:
    return [re.sub(r"\{(\w+)\}", lambda m: vars_.get(m.group(1), m.group(0)), a) for a in argv]


def local_exe(name: str, root: Path) -> str:
    """A path in the repo (build/..., scripts/...) runs from the root; a bare name comes from PATH."""
    return str(root / name) if ("/" in name or "\\" in name) and (root / name).is_file() else name


def run(argv: list[str], root: Path, timeout: float = 1800, cwd: str = "", stdin: str = "") -> subprocess.CompletedProcess:
    where = root / cwd if cwd else root
    exe = local_exe(argv[0], where)
    data = (root / stdin).read_text(encoding="utf-8") if stdin else None
    return subprocess.run([exe, *argv[1:]], cwd=where, capture_output=True, text=True, timeout=timeout, input=data,
                          env={**os.environ, "PYTHONIOENCODING": "utf-8", "NO_COLOR": "1"})


def setup(spec: dict, root: Path) -> None:
    for cmd in spec.get("setup", []):
        argv = expand(cmd, resolve_vars(spec, root))
        print("setup:", " ".join(argv), flush=True)
        r = run(argv, root)
        if r.returncode != 0:
            sys.exit(f"setup failed ({r.returncode}): {' '.join(argv)}\n{r.stdout[-2000:]}{r.stderr[-2000:]}")


def _pointer(doc, pointer: str):
    for part in [p for p in pointer.split("/") if p != ""]:
        part = part.replace("~1", "/").replace("~0", "~")
        doc = doc[int(part)] if isinstance(doc, list) else doc[part]
    return doc


def _image(path: Path, size: int) -> list[list[float]]:
    data = path.read_bytes()
    head = re.match(rb"(P5|Pf|PF)\s+(\d+)\s+(\d+)\s+([-\d.]+)\s", data)
    if not head:
        raise ValueError(f"{path.name}: not a P5 PGM or PFM")
    kind, w, h, scale = head.group(1), int(head.group(2)), int(head.group(3)), float(head.group(4))
    body = data[head.end():]
    if kind == b"P5":
        px = [b / scale for b in body[: w * h]]
        rows = [px[y * w:(y + 1) * w] for y in range(h)]
    else:
        ch = 3 if kind == b"PF" else 1
        fmt = ("<" if scale < 0 else ">") + "f" * (w * h * ch)
        vals = struct.unpack(fmt, body[: 4 * w * h * ch])
        rows = [[vals[(y * w + x) * ch] for x in range(w)] for y in range(h)]
        rows.reverse()   # PFM rows run bottom to top
    out = []
    for gy in range(size):
        y0, y1 = gy * h // size, max(gy * h // size + 1, (gy + 1) * h // size)
        line = []
        for gx in range(size):
            x0, x1 = gx * w // size, max(gx * w // size + 1, (gx + 1) * w // size)
            cell = [rows[y][x] for y in range(y0, y1) for x in range(x0, x1)]
            line.append(round(sum(cell) / len(cell), 4))
        out.append(line)
    return out


def facts(spec: dict, root: Path) -> dict:
    vars_ = resolve_vars(spec, root)
    for cmd in spec.get("prepare", []):
        argv = expand(cmd, vars_)
        r = run(argv, root)
        if r.returncode not in (0, *spec.get("prepare_ok_codes", [])):
            sys.exit(f"prepare failed ({r.returncode}): {' '.join(argv)}\n{r.stdout[-1500:]}{r.stderr[-1500:]}")
    values = {}
    for fid, f in spec.get("facts", {}).items():
        try:
            if "json" in f:
                v, src = _pointer(json.loads((root / f["json"]).read_text(encoding="utf-8")), f.get("pointer", "")), f"{f['json']}#{f.get('pointer', '')}"
            elif "text" in f:
                m = re.search(f["regex"], (root / f["text"]).read_text(encoding="utf-8"))
                v, src = m.group(1), f"{f['text']} /{f['regex']}/"
            elif "cmd" in f:
                argv = expand(f["cmd"], vars_)
                r = run(argv, root, cwd=f.get("cwd", ""), stdin=f.get("stdin", ""))
                out = (r.stdout + r.stderr).strip()
                if f.get("exit"):
                    v = r.returncode
                else:
                    v = re.search(f["regex"], out, re.M).group(1) if f.get("regex") else out
                src = (f"(in {f['cwd']}) " if f.get("cwd") else "") + " ".join(argv)
            elif "image" in f:
                v, src = _image(root / f["image"], int(f.get("size", 64))), f"{f['image']} averaged to {f.get('size', 64)}^2"
            else:
                raise ValueError("unknown fact kind")
        except Exception as e:  # noqa: BLE001 - a fact that cannot be read must stop the render, with its reason
            sys.exit(f"fact {fid!r} could not be read: {e}")
        values[fid] = {"value": v, "source": src}
    return {"schema": "raw-native.media-facts/1", "commit": commit(root), "platform": sys.platform, "facts": values}


def scrub(line: str, root: Path) -> str:
    """Local paths become paths relative to the repo, so a recording shows no machine's layout."""
    for form in {str(root), root.as_posix(), str(root).lower(), root.as_posix().lower()}:
        for sep in ("/", "\\"):
            line = line.replace(form + sep, "")
        line = line.replace(form, ".")
    home = Path.home()
    for form in {str(home), home.as_posix()}:
        line = line.replace(form, "~")
    return line


def record(scene: dict, spec: dict, root: Path) -> dict:
    """Run a walkthrough's commands and keep every line they print, with its time."""
    vars_ = resolve_vars(spec, root)
    steps = []
    for st in scene["steps"]:
        argv = expand(st["run"], vars_)
        where = root / st["cwd"] if st.get("cwd") else root
        exe = local_exe(argv[0], where)
        t0 = time.monotonic()
        p = subprocess.Popen([exe, *argv[1:]], cwd=where, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             encoding="utf-8", errors="replace", env={**os.environ, "PYTHONIOENCODING": "utf-8", "NO_COLOR": "1"})
        lines = []
        for line in p.stdout:
            lines.append({"t": round(time.monotonic() - t0, 3), "text": scrub(line.rstrip("\r\n"), root).replace("\t", "    ")})
        p.wait()
        ok = st.get("ok_codes", [0])
        if p.returncode not in ok:
            sys.exit(f"walkthrough step failed ({p.returncode}): {' '.join(argv)}")
        steps.append({"cmd": " ".join(st.get("display_argv", argv)), "lines": lines, "exit": p.returncode, "seconds": round(time.monotonic() - t0, 3)})
        print(f"  recorded {len(lines)} lines: {' '.join(argv)}", flush=True)
    return {"schema": "raw-native.media-cast/1", "commit": commit(root), "platform": sys.platform, "steps": steps}
