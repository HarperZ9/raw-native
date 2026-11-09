#!/usr/bin/env python3
"""Scores the G5 blind readability test (docs/action-hitvol.md), stdlib only.

  python analyse.py key.json responses.json [more responses.json ...]
  python analyse.py --selftest

Each move is shown twice to a viewer, once flat-timed and once retimed, in a shuffled order. A pair
is one move and one viewer. The table counts pairs where both, only the retimed, only the flat, or
neither showing was named correctly. It reports each condition's share correct, the paired
difference (retimed minus flat) with Newcombe's hybrid score interval for paired proportions
(Newcombe 1998, Statistics in Medicine 17:2635, method 10), and the exact two-sided McNemar p.
The claim "retimed tells are named correctly more often" holds only when the interval's lower end
is above 0. Under 20 pairs the script says so and does not judge.
"""
import json
import math
import sys

Z = 1.959963984540054


def wilson(k, n):
    if n == 0:
        return (0.0, 1.0)
    p = k / n
    d = 1 + Z * Z / n
    c = (p + Z * Z / (2 * n)) / d
    h = Z * math.sqrt(p * (1 - p) / n + Z * Z / (4 * n * n)) / d
    return (c - h, c + h)


def newcombe_paired(a, b, c, d):
    """Difference p1 - p2 for paired data; p1 = (a + b) / n (retimed), p2 = (a + c) / n (flat)."""
    n = a + b + c + d
    p1, p2 = (a + b) / n, (a + c) / n
    l1, u1 = wilson(a + b, n)
    l2, u2 = wilson(a + c, n)
    den = (a + b) * (c + d) * (a + c) * (b + d)
    phi = (a * d - b * c) / math.sqrt(den) if den > 0 else 0.0
    dl = math.sqrt(max(0.0, (p1 - l1) ** 2 - 2 * phi * (p1 - l1) * (u2 - p2) + (u2 - p2) ** 2))
    du = math.sqrt(max(0.0, (u1 - p1) ** 2 - 2 * phi * (u1 - p1) * (p2 - l2) + (p2 - l2) ** 2))
    return p1 - p2, p1 - p2 - dl, p1 - p2 + du


def mcnemar_exact(b, c):
    n = b + c
    if n == 0:
        return 1.0
    k = min(b, c)
    tail = sum(math.comb(n, i) for i in range(k + 1)) / 2 ** n
    return min(1.0, 2 * tail)


def table(key, responses):
    """Counts (a, b, c, d) over move-viewer pairs. responses: list of response lists, one a viewer."""
    by_id = {k["id"]: k for k in key["key"]}
    a = b = c = d = 0
    for viewer in responses:
        got = {r["id"]: r["answer"] for r in viewer}
        moves = {}
        for tid, ans in got.items():
            k = by_id[tid]
            moves.setdefault(k["move"], {})[k["condition"]] = ans == k["answer"]
        for m, cond in moves.items():
            if set(cond) != {"flat", "retimed"}:
                raise ValueError(f"move {m} lacks a pair")
            r, f = cond["retimed"], cond["flat"]
            a += r and f; b += r and not f; c += f and not r; d += not r and not f
    return a, b, c, d


def report(a, b, c, d):
    n = a + b + c + d
    diff, lo, hi = newcombe_paired(a, b, c, d)
    lines = [f"pairs {n}: both {a}, retimed only {b}, flat only {c}, neither {d}",
             f"retimed correct {(a + b) / n:.3f}, flat correct {(a + c) / n:.3f}",
             f"difference {diff:+.3f}, 95% interval {lo:+.3f} to {hi:+.3f}, McNemar exact p {mcnemar_exact(b, c):.4f}"]
    if n < 20:
        lines.append("fewer than 20 pairs: no judgement")
    else:
        lines.append("retimed named more often" if lo > 0 else "no difference shown (interval reaches 0 or below)")
    return "\n".join(lines)


def selftest():
    # Planted response tables with known answers.
    # every retimed right, every flat wrong: phi is 0 and both Wilson margins are z^2 / (n + z^2), so the
    # lower end is 1 - sqrt(2) z^2 / (n + z^2) = 0.7813 at n = 21 (derived by hand from the method)
    d1, lo1, hi1 = newcombe_paired(0, 21, 0, 0)
    assert d1 == 1 and abs(lo1 - (1 - math.sqrt(2) * Z * Z / (21 + Z * Z))) < 1e-12 and hi1 == 1, (d1, lo1, hi1)
    d2, lo2, hi2 = newcombe_paired(10, 0, 0, 11)         # identical answers in both conditions
    assert d2 == 0 and lo2 < 0 < hi2 and abs(lo2 + hi2) < 1e-12, (d2, lo2, hi2)
    d3, lo3, hi3 = newcombe_paired(8, 3, 3, 7)           # balanced discordance
    assert d3 == 0 and lo3 < 0 < hi3
    assert mcnemar_exact(0, 0) == 1.0 and abs(mcnemar_exact(0, 6) - 2 / 64) < 1e-15
    # a planted key and responses: 21 moves, viewer right on retimed and wrong on flat
    key = {"key": [{"id": f"t{2 * i + j}", "move": f"m{i}", "answer": "swing", "condition": ["flat", "retimed"][j]} for i in range(21) for j in range(2)]}
    resp = [[{"id": k["id"], "answer": "swing" if k["condition"] == "retimed" else "slam"} for k in key["key"]]]
    assert table(key, resp) == (0, 21, 0, 0)
    print("selftest: planted tables give the known intervals")
    print(report(0, 21, 0, 0))


def main(argv):
    if argv[1:] == ["--selftest"]:
        selftest()
        return 0
    if len(argv) < 3:
        print(__doc__)
        return 2
    with open(argv[1], encoding="utf-8") as f:
        key = json.load(f)
    viewers = []
    for p in argv[2:]:
        with open(p, encoding="utf-8") as f:
            viewers.append(json.load(f)["responses"])
    print(report(*table(key, viewers)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
