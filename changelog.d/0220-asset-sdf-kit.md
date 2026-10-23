- **`tools/asset` gains an SDF kit and adaptive LODs.** New stroke kinds `rbox`, `rcone`, `cone`, `torx`,
  `ext` and `ellb` have exact or bounded distances and analytic normals. Modifiers `round`, `shell`,
  `mirror`, `repeat`, `twist`, `bend`, `displace` and `relief` carry stated Lipschitz bounds. Cubic smooth
  blends and subtraction come with an exact fold gradient. Quadric decimation against the field meets the
  40000, 9000 and 3000 triangle budgets inside the error bounds in `tools/asset/DESIGN.md` section 10.
  `tools/asset/test/sdfkit.test.mjs` checks each kind against a dense reference, with controls that must fail.
