- **Action timeline for fighting games** (`raw/game/action`, `raw_native_cli action-dump`). A fixed
  60 Hz integer-tick timeline for any number of actors: typed window sections (cancel by action-class
  mask, invulnerable, parry, armor tiers, charge, tap-release, buffer, tell), an 8-frame buffer tried
  newest first, a press made while free starting its move on the same tick, hit-stop as a per-actor
  hold, and two receiving meters (a fast interrupt with a flinch guard, a slow poise meter that
  staggers and knocks down). It loads Low Water's lw-moves v1 data and ports its tool strike
  generator. Checked against Low Water's JS engine: all 230 moves (104 base, 126 tool strikes) give
  the same startup, active, recovery, cancel and free frames, the whole expansion is byte-identical to
  the JS golden, 40 of 40 action-timeline traces recorded from the JS replay byte-identical, and the
  shared math matches DETMATH's 12 golden bit patterns and a 1,001-input sweep of every function.
  Whole-fight traces (AI, field, hit geometry) are not replayed: that needs the later ports.
  `action-dump` prints a move as a frame strip and diffs two frame tables.
