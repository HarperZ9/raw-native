- **Held-time clock and closed loops** (`raw/scene/held_time.hpp`, `raw/scene/loop.hpp`,
  `raw/core/loop_media.hpp`, `web/anim/heldtime.mjs`, `raw_native_cli held-time`). One clock samples
  each layer at an authored rate (ones, twos, threes, a named rate such as 12 Hz, or continuous for
  the camera), in integer ticks, so 30, 60 and 144 fps show the same poses on the same frames. A
  layer's boil seed hashes its pose buffer, so ink and paint boil step only when the pose changes;
  the hatch pass takes any field and camera with boil, and paint takes a boil canvas offset. A loop
  contract refuses any animated term whose period does not divide the loop, and first-party GIF
  and lossless animated WebP writers (with `scripts/loop_finish.py` for MP4 through an external
  ffmpeg) write loops at whole-number scales. Measured: held ratios 0.50 and 0.67, zero boil
  exceptions, frame N equal to frame 0 at 8 bits, a 12 s 480x270 GIF at 3.2 MB
  (`evidence/heldtime-loop-runs.json`). A first scene's GIF was 5.1 MB, over the bound, and stays on
  record.
