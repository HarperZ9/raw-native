- **Low Water host rules in C++** (`raw/game/lowwater`). Checks, skills, the character sheet and
  meters computed in C++ behind a plain C ABI (`rules_c.h`); `lw_trace_replay --compute` matches
  80 of 80 golden traces byte for byte. Community, world record, lore, dream hooks and
  interrogations are not ported yet. `scripts/check_layers.py` now accepts `.h` public headers.
