<!-- writing-profile: research -->
# Paint-over projection: round-trip and off-axis protocol

Written and committed on 2026-10-15 before the round-trip tool or the off-axis tool ran. The code under test (`web/shaders/paintover/`, `web/shaders/fixtures/harbour*.mjs`) and the analytic stretch prediction (`evidence/paintover-offaxis-prediction.json`, made from the cameras alone by `tools/shaders/paintover_prior.mjs`) exist at this commit. The measurement tools (`paintover_roundtrip.mjs`, `paintover_offaxis.mjs`) do not.

## What is tested

Paint-over projection (EFFECTS-PROGRAM item 1): a block-out of an original harbour, "Lamp Quay", is rendered from a camera-locked isometric camera (orthographic, yaw 45 degrees, elevation 35.264 degrees); a painting is made over that render (procedural brush strokes, `painter.mjs`, no outside art); the painting is projected back onto the block-out and becomes its surface colour. Keyframe projectors blend by view angle (`projector.mjs`, compact windows, theta max 40 degrees). A greedy search suggests secondary projectors from the points the primary cannot reach.

## Part 1: the round trip

Render the block-out from the primary camera at 960 x 540, paint it, project the painting back, render the block-out again from the same camera with the painted surface, and compare the result with the painting on every pixel the block-out covers.

Bounds (fixed now):
- R1. On covered pixels: maximum absolute difference at most 1 code, mean at most 0.02 codes.
- R2. Covered fraction (pixels where some projector claims the surface) at least 0.995 of the block-out's pixels.
- Controls, each of which must fail the round trip as stated, so the check can fail:
  - R3. View camera shifted by exactly one pixel: mean difference at least 2.0 codes.
  - R4. The unpainted lit export used in place of the painting: mean difference at least 5.0 codes.
  - R5. A depth map offset by 1 world unit toward the camera (every surface now looks hidden): covered fraction at most 0.01.

## Part 2: off-axis views

Views are orthographic at 480 x 270 with the same target and scale as the primary: yaw offsets 0, 5, 10, 20, 30, 45, 60, 90 degrees at the primary elevation, and elevation offsets -10 and +10 degrees at yaw offset 0. Per view, with the primary projector alone:
- holes: the fraction of block-out pixels no projector claims (a hole shows the unpainted block-out);
- stretch: from the 2 x 2 Jacobian of view pixels to painting pixels, texel magnification M (view pixels per painting pixel along the worst direction) and anisotropy A, mean and 95th percentile, on the quay ground (id 0) and on walls (id 2).
Then two secondary projectors are chosen greedily from 30 candidate cameras (yaw offsets of plus or minus 20 to 150 degrees in steps of 30, elevation offsets -10, 0, 15 degrees) to cover the points the primary misses in 12 required views (yaw offsets plus or minus 20 and 30 degrees, elevation offsets -10, 0, 10 degrees). Each is painted over its own render (a different seed) and the holes are measured again with all three.

Predictions and bounds (fixed now):
- P1, ground stretch: mean M and A on the quay pixels the primary covers are within 3% of the analytic values in `evidence/paintover-offaxis-prediction.json` (M 1.00, 1.05, 1.11, 1.22, 1.33, 1.49, 1.62 and A 1.00, 1.11, 1.22, 1.48, 1.77, 2.22, 2.62 for yaw offsets 0, 5, 10, 20, 30, 45, 60).
- P2, primary-only holes: at most 0.01 at 5 degrees, 0.03 at 10, 0.10 at 20, 0.20 at 30; at least 0.25 at 90; non-decreasing in the yaw offset within 0.005.
- P3, secondaries: holes with all three projectors at most 0.4 x the primary-only holes at 30 degrees, and at most 0.6 x at 90 degrees.
- P4, the chosen secondaries are each at least 25 degrees of yaw from the primary and at least 30 degrees from each other.
- P5, walls at a 30 degree yaw offset: mean M at most 1.9 and mean A at most 2.6.
- P6, a view at a projector's own camera shows no blend contamination: with the primary and one secondary 60 degrees apart, the view at the primary matches the primary round trip (R1 holds).

## Decision

No design decision hangs on the result except one default: theta max stays at 40 degrees unless P6 fails or a view between two projectors shows a pop (a pixel changing by more than 8 codes between views 1 degree apart); then it is changed and the change recorded.

## Limits

One scene, SDF block-outs (not triangle meshes), orthographic views only (a perspective camera uses the same code path, unmeasured here), a procedural painting (not a human painter's), software adapters only. No human has rated the look.
