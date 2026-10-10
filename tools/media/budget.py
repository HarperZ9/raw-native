#!/usr/bin/env python3
"""Check rendered media against the budgets in its spec (M1 exit criterion 7).

    python tools/media/budget.py MEDIA_OUT [--spec docs/media/media.json] [--smoke]

The spec's "budgets" block:

    "budgets": {
      "smoke": { "seconds": 4, "wall_seconds": 180 },
      "frame_ms_p95": { "gpu": { "ao-check": 30 }, "swiftshader": { "ao-check": 2500 } },
      "pass_ms": { "gpu": { "draw": 4.0, "finish": 2.0 } }
    }

--smoke checks each scene's wall time against smoke.wall_seconds (the CI job renders
smoke.seconds of each scene). Without --smoke, each scene's p95 frame time is checked
for its adapter kind, and each pass's GPU time where the adapter recorded timestamps.
Exits 1 and names every budget a scene exceeds.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def check(out: Path, spec: dict, smoke: bool) -> list[str]:
    b = spec.get("budgets", {})
    problems = []
    for mpath in sorted(out.glob("*/media.json")):
        m = json.loads(mpath.read_text(encoding="utf-8"))
        sid, kind = m["scene"], m.get("adapter_kind", "gpu")
        if smoke:
            lim = b.get("smoke", {}).get("wall_seconds")
            if lim is not None and m["wall_seconds"] > lim:
                problems.append(f"{sid}: smoke render took {m['wall_seconds']} s, over its {lim} s budget")
            continue
        lim = b.get("frame_ms_p95", {}).get(kind, {}).get(sid)
        if lim is not None and m["frame_ms_p95"] > lim:
            problems.append(f"{sid}: p95 frame {m['frame_ms_p95']} ms on {kind}, over its {lim} ms budget")
        for name, ms in (m.get("gpu_ms") or {}).items():
            plim = b.get("pass_ms", {}).get(kind, {}).get(name)
            if plim is not None and ms > plim:
                problems.append(f"{sid}: pass {name} took {ms:.3f} ms of GPU time on {kind}, over its {plim} ms budget")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--spec", default="docs/media/media.json")
    ap.add_argument("--smoke", action="store_true")
    a = ap.parse_args()
    spec = json.loads(Path(a.spec).read_text(encoding="utf-8"))
    problems = check(Path(a.out), spec, a.smoke)
    print("\n".join(problems) or "within budget")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
