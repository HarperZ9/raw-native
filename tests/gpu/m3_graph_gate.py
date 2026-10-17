#!/usr/bin/env python3
"""Gate for M3 criterion 3 (evidence/m3-aliasing-validation-bounds.json). Standard library only.

  m3_graph_gate.py same A.json B.json
      two JSON records equal after dropping wall-clock fields (any key that is "timing", "timing_ms",
      ends in "_ms" or is "ms"): the corpus with aliasing off and on, or with and without the layer
  m3_graph_gate.py stats ON.jsonl [--out SUMMARY.json]
      the RAW_NATIVE_GRAPH_STATS lines of an aliasing-on run: aliased bytes never above unaliased on
      any execute, strictly lower in total; per-graph sums go to SUMMARY.json
Exit 0 when the check holds, 1 when it does not, 2 on usage.
"""
import json
import sys


def strip(v):
    if isinstance(v, dict):
        return {k: strip(x) for k, x in v.items() if not (k in ("timing", "timing_ms", "ms") or k.endswith("_ms"))}
    if isinstance(v, list):
        return [strip(x) for x in v]
    return v


def same(a, b):
    with open(a, encoding="utf-8") as f:
        x = strip(json.load(f))
    with open(b, encoding="utf-8") as f:
        y = strip(json.load(f))
    ok = x == y
    print(f"{a} vs {b}: {'identical' if ok else 'DIFFER'} (wall-clock fields left out)")
    return ok


def stats(path, out=None):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    graphs = {}
    worse = 0
    for r in rows:
        if r["aliased_bytes"] > r["unaliased_bytes"]:
            worse += 1
        g = graphs.setdefault(r["graph"], {"executes": 0, "unaliased_bytes": 0, "aliased_bytes": 0, "max_saved_bytes": 0})
        g["executes"] += 1
        g["unaliased_bytes"] += r["unaliased_bytes"]
        g["aliased_bytes"] += r["aliased_bytes"]
        g["max_saved_bytes"] = max(g["max_saved_bytes"], r["unaliased_bytes"] - r["aliased_bytes"])
    tu = sum(g["unaliased_bytes"] for g in graphs.values())
    ta = sum(g["aliased_bytes"] for g in graphs.values())
    on = all(r.get("aliasing") for r in rows)
    summary = {"executes": len(rows), "graphs": len(graphs), "unaliased_bytes": tu, "aliased_bytes": ta,
               "saved_fraction": (tu - ta) / tu if tu else 0.0, "executes_worse": worse, "by_graph": graphs}
    if out:
        with open(out, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1)
    print(f"{len(rows)} executes over {len(graphs)} graphs: unaliased {tu} bytes, aliased {ta} bytes "
          f"({summary['saved_fraction']:.3%} saved), {worse} execute(s) worse")
    return bool(rows) and on and worse == 0 and ta < tu


def main(a):
    if len(a) == 3 and a[0] == "same":
        ok = same(a[1], a[2])
    elif len(a) >= 2 and a[0] == "stats":
        ok = stats(a[1], a[a.index("--out") + 1] if "--out" in a[:-1] else None)
    else:
        print(__doc__)
        return 2
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
