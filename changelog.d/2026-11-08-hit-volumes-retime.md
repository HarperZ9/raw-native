- **Hit volumes from clip markers, and telegraph retiming as data** (`raw/game/action`,
  `docs/action-hitvol.md`). A move's timing now lives as markers on its 60 Hz clip, and
  `check_markers` fails when a clip and the move table disagree: 230 of 230 Low Water moves agree,
  and a marker moved by one frame is caught. Each hit ring becomes a capsule bound to a bone of a
  swing rig, posed by a raw::anim clip, with reach and arc within 5% of the rings. Telegraph retiming
  is a list of time-scale regions on the presentation clock; hit volumes follow the retimed clip, and
  `action-dump tells` lints every tell for its lead, its keyed hit frames and the slow-then-fast shape.
  The C++ port of `web/anim/clip.mjs` gains `layout`, `keyTime`, `phaseAt` and `warp`. A blind
  readability test (sheet, trials, paired analysis) is built; no human trials have been run.
