- **Start visibility for the 3D cascade merges** (`web/shaders/cascades3d/world.mjs`,
  `wgsl3d-world.mjs`). Opt-in `sv` weighs every merge by whether a neighbour's interval start lies
  behind a wall, and `fz` drops merges that trust no neighbour. Measuring them showed that the probe
  fix's closed-room result (0.1%) holds only at one grid placement; four others leak 10% to 37%.
  `sv` cuts that to 0.2% to 4.8%, and with `fz` every placement is under 1%, but `fz` darkens
  raw-hall by 21%, so neither is on by default.
