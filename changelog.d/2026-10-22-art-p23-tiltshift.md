- **P23, a tilt-shift lens for the art track** (`raw_native_cli art tiltshift`). The plane of
  focus is tilted in the world (Scheimpflug), so the in-focus wedge follows a street and the walls
  that cross it. Out of focus the frame is repainted coarser by an anisotropic Kuwahara over pigment
  latents, which repaints the area; no blur is applied. A focus director racks by story intent, with an observer's wandering
  attention that never leaves a speaker or a fight. An optional lens and print effect stack obeys
  the flash and red limits. People in the wedge keep their ink exactly, and the screen band fails
  the same check. The orbit is 53% steadier than the base paint. The GPU chain costs about 0.86 ms
  at 1080p on an RTX 4090 (D3D12 median, Vulkan about 0.85 ms), inside the 1.0 ms bound but with
  run-to-run spread from 0.80 to 1.08 ms on a shared GPU. The painted-versus-blurred check keeps
  its first form as a recorded miss and gates on a relative form (ours against a gaussian of the
  same radius). The dream look still shows blocky patches at its large radius and stays behind its
  flag.
