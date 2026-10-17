- **Skeletons, skinning and phase-timed clips** (`web/anim`). A CPU reference for animated
  characters, which the engine did not have: joint hierarchies with rest frames and per-pose
  rotations (`skeleton.mjs`), linear-blend skinning with up to four influences a vertex,
  phase-timed clips whose startup, active and recovery lengths come from data so a move is
  retimed without re-keying, sampled with a monotone cubic that never overshoots a key, and a
  piecewise time warp that keeps a clip's phases in step with gameplay frames (`clip.mjs`), and
  two-bone IK (`ik.mjs`). Eleven node tests (`anim.test.mjs`). No GPU path yet: WGSL skinning
  and a glTF skin import are not in this change.
