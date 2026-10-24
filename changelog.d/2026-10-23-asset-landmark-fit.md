- **`tools/asset` fits a sculpt to anthropometric landmarks.** `tools/asset/bin/fit.mjs` takes a landmark set built from
  ANSUR II, Farkas and Loomis proportions and optimises named stroke parameters until the skin passes within 2 mm of each
  landmark, with a crease check. An eye record can be seated on a lid stroke. The first Ned fit missed the 2 mm bound at
  the jaw angle (4.4 mm) and raised the crease p99 by 11 degrees; see `tools/asset/DESIGN.md` section 11.
