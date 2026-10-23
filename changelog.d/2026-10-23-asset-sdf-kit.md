- **`tools/asset` gains an SDF kit and adaptive LODs.** New stroke kinds `rbox`, `rcone`, `cone`, `torx`,
  `ext` and `ellb` have exact or bounded distances and analytic normals. Modifiers `round`, `shell`, `mirror`, `repeat`,
  `twist`, `bend`, `displace` and `relief` carry stated Lipschitz bounds. Cubic smooth blends and subtraction come with an
  exact fold gradient. Quadric decimation against the field meets the 40000, 9000 and 3000 triangle budgets. Bounds are in
  `tools/asset/DESIGN.md` section 10. The far LOD error bound is not yet held on every build (8.3 mm max seen
  against 6 mm).
