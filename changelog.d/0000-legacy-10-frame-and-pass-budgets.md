- **Frame and pass budgets** (roadmap M1, criterion 7). `docs/media/media.json` held only the smoke
  wall-clock budget, so `tools/media/budget.py` could not fail a local GPU run: a stats file with a
  900 ms p95 frame and a 40 ms draw pass read "within budget". The file now sets one frame at 60 Hz
  (16.7 ms p95) for both media scenes and per-pass budgets (draw 4 ms; particles, bloom and finish
  2 ms) on GPU adapters. The RTX 4090 record of 2026-10-10 is within them; a CI step checks that
  record and an over-budget control, which must fail.
