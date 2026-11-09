# Story VM: bounds and measurements (2026-10-23)

Limit up front: the bounds below were fixed after the first full measurement, not before it. The
measurement is one machine (Windows 11, MinGW g++ Release, other builds running at the same
time), so treat the times as upper-quartile noisy and the bounds as loose.

| Bound | Value | Measured |
|---|---|---|
| Parse the full Quota slice (27 files, about 4,900 lines) with regex-based step parsing | 100 ms or less | 37 ms (parse and book build, includes reading the files) |
| Step cost, replaying a recorded run (one trace record, including parsing the golden record) | 100 us or less | 36 us mean over 106,317 records in 80 runs |
| Replay of one full run | 100 ms or less | 48 ms mean (3.8 s for 80 runs) |
| Equivalence | every trace byte-identical | 80 of 80 |

What the equivalence shows: given the host answers recorded from the JS engine, the C++ VM makes the
same host calls in the same order and writes the same nodes, lines, menus, variable and flag changes,
clock moves and RNG draws. It does not show that the C++ side can play a game alone: the rules,
world record and combat answers are replayed, not computed (steps 3 and later).
The step cost includes JSON parsing of the recorded host answers, so it overstates the VM.
